#include "capital.h"

#include <limits.h>
#include <string.h>

static bool checked_add(CascadeAmount a, CascadeAmount b, CascadeAmount *out, CascadeStatus *status) {
    if (!cascade_amount_add(a, b, out)) {
        cascade_status_error(status, "capital arithmetic addition overflow");
        return false;
    }
    return true;
}

static bool mul_div_floor(CascadeAmount value,
                          CascadeAmount multiplier,
                          CascadeAmount divisor,
                          CascadeAmount *out,
                          CascadeStatus *status) {
    CascadeAmount product = 0;
    if (value < 0 || multiplier < 0 || divisor <= 0) {
        cascade_status_error(status, "capital arithmetic received an invalid domain");
        return false;
    }
    if (!cascade_amount_mul(value, multiplier, &product)) {
        cascade_status_error(status, "capital arithmetic multiplication overflow");
        return false;
    }
    *out = product / divisor;
    return true;
}

static bool mul_div_ceil(CascadeAmount value,
                         CascadeAmount multiplier,
                         CascadeAmount divisor,
                         CascadeAmount *out,
                         CascadeStatus *status) {
    CascadeAmount product = 0;
    if (!mul_div_floor(value, multiplier, divisor, &product, status)) {
        return false;
    }
    CascadeAmount raw = 0;
    if (!cascade_amount_mul(value, multiplier, &raw)) {
        cascade_status_error(status, "capital arithmetic multiplication overflow");
        return false;
    }
    *out = product + (raw % divisor == 0 ? 0 : 1);
    return true;
}

bool capital_validate_policy(const CapitalPolicy *policy, CascadeStatus *status) {
    if (policy == NULL) {
        cascade_status_error(status, "capital policy is null");
        return false;
    }
    if (policy->reserve_haircut_bps < 0 || policy->reserve_haircut_bps > CASCADE_BPS) {
        cascade_status_error(status, "reserve haircut must be between 0 and 10000 bps");
        return false;
    }
    if (policy->volatility_bps < 0 || policy->volatility_bps > CASCADE_BPS) {
        cascade_status_error(status, "volatility addon must be between 0 and 10000 bps");
        return false;
    }
    if (policy->liquidity_bps < 0 || policy->liquidity_bps > CASCADE_BPS) {
        cascade_status_error(status, "liquidity addon must be between 0 and 10000 bps");
        return false;
    }
    if (policy->concentration_bps < 0 || policy->concentration_bps > CASCADE_BPS) {
        cascade_status_error(status, "concentration addon must be between 0 and 10000 bps");
        return false;
    }
    if (policy->settlement_horizon_epochs < 1 ||
        policy->settlement_horizon_epochs > CASCADE_MAX_HORIZON_EPOCHS) {
        cascade_status_error(status, "settlement horizon must be between 1 and 365 epochs");
        return false;
    }
    if (policy->target_coverage_bps < CASCADE_BPS || policy->target_coverage_bps > 30000) {
        cascade_status_error(status, "target coverage must be between 10000 and 30000 bps");
        return false;
    }
    if (policy->operational_floor < 0) {
        cascade_status_error(status, "operational floor cannot be negative");
        return false;
    }
    cascade_status_clear(status);
    return true;
}

bool capital_validate_input(const CapitalInput *input, CascadeStatus *status) {
    if (input == NULL) {
        cascade_status_error(status, "capital input is null");
        return false;
    }
    if (input->reserve_available < 0 || input->reserve_locked < 0 ||
        input->pending_gross < 0 || input->expected_fees < 0 ||
        input->largest_counterparty < 0) {
        cascade_status_error(status, "capital input cannot contain negative amounts");
        return false;
    }
    if (input->expected_fees > input->pending_gross) {
        cascade_status_error(status, "expected fees cannot exceed pending gross settlement");
        return false;
    }
    if (input->largest_counterparty > input->pending_gross) {
        cascade_status_error(status, "largest counterparty cannot exceed pending gross settlement");
        return false;
    }
    cascade_status_clear(status);
    return true;
}

bool capital_compute(const CapitalPolicy *policy,
                     const CapitalInput *input,
                     CapitalReport *report,
                     CascadeStatus *status) {
    if (report == NULL) {
        cascade_status_error(status, "capital report is null");
        return false;
    }
    if (!capital_validate_policy(policy, status) || !capital_validate_input(input, status)) {
        return false;
    }
    memset(report, 0, sizeof(*report));

    const int eligible_bps = CASCADE_BPS - policy->reserve_haircut_bps;
    if (!mul_div_floor(input->reserve_available,
                       eligible_bps,
                       CASCADE_BPS,
                       &report->eligible_available,
                       status) ||
        !mul_div_floor(input->reserve_locked,
                       eligible_bps,
                       CASCADE_BPS,
                       &report->eligible_locked,
                       status) ||
        !checked_add(report->eligible_available,
                     report->eligible_locked,
                     &report->eligible_resources,
                     status)) {
        return false;
    }

    if (!mul_div_ceil(input->pending_gross,
                      policy->volatility_bps,
                      CASCADE_BPS,
                      &report->volatility_addon,
                      status)) {
        return false;
    }

    CascadeAmount liquidity_rate = 0;
    if (!cascade_amount_mul((CascadeAmount)policy->liquidity_bps,
                            (CascadeAmount)policy->settlement_horizon_epochs,
                            &liquidity_rate) ||
        !mul_div_ceil(input->pending_gross,
                      liquidity_rate,
                      CASCADE_BPS,
                      &report->liquidity_addon,
                      status) ||
        !mul_div_ceil(input->largest_counterparty,
                      policy->concentration_bps,
                      CASCADE_BPS,
                      &report->concentration_addon,
                      status)) {
        cascade_status_error(status, "capital liquidity arithmetic overflow");
        return false;
    }

    CascadeAmount stressed = input->pending_gross;
    if (!checked_add(stressed, input->expected_fees, &stressed, status) ||
        !checked_add(stressed, report->volatility_addon, &stressed, status) ||
        !checked_add(stressed, report->liquidity_addon, &stressed, status) ||
        !checked_add(stressed, report->concentration_addon, &stressed, status)) {
        return false;
    }
    report->stressed_obligation = stressed;

    CascadeAmount target = 0;
    if (!mul_div_ceil(stressed,
                      policy->target_coverage_bps,
                      CASCADE_BPS,
                      &target,
                      status) ||
        !checked_add(target, policy->operational_floor, &report->required_coverage, status)) {
        return false;
    }

    if (report->eligible_resources >= report->required_coverage) {
        report->surplus = report->eligible_resources - report->required_coverage;
        report->shortfall = 0;
        report->policy_satisfied = 1;
    } else {
        report->surplus = 0;
        report->shortfall = report->required_coverage - report->eligible_resources;
        report->policy_satisfied = 0;
    }

    if (report->required_coverage == 0) {
        report->coverage_bps = report->eligible_resources == 0 ? CASCADE_BPS : 30000;
    } else {
        CascadeAmount coverage = 0;
        if (!mul_div_floor(report->eligible_resources,
                           CASCADE_BPS,
                           report->required_coverage,
                           &coverage,
                           status)) {
            return false;
        }
        report->coverage_bps = coverage > INT_MAX ? INT_MAX : (int)coverage;
    }

    if (input->pending_gross == 0) {
        report->largest_share_bps = 0;
    } else {
        CascadeAmount share = 0;
        if (!mul_div_ceil(input->largest_counterparty,
                          CASCADE_BPS,
                          input->pending_gross,
                          &share,
                          status)) {
            return false;
        }
        report->largest_share_bps = share > INT_MAX ? INT_MAX : (int)share;
    }

    cascade_status_clear(status);
    return true;
}

bool capital_write_json(const CapitalPolicy *policy,
                        const CapitalInput *input,
                        const CapitalReport *report,
                        TextBuffer *out) {
    if (policy == NULL || input == NULL || report == NULL || out == NULL) {
        return false;
    }
    return text_buffer_appendf(
        out,
        "{\"input\":{\"available\":%lld,\"locked\":%lld,\"pendingGross\":%lld,"
        "\"expectedFees\":%lld,\"largestCounterparty\":%lld},"
        "\"policy\":{\"reserveHaircutBps\":%d,\"volatilityBps\":%d,"
        "\"liquidityBps\":%d,\"concentrationBps\":%d,\"horizonEpochs\":%d,"
        "\"targetCoverageBps\":%d,\"operationalFloor\":%lld},"
        "\"report\":{\"eligibleAvailable\":%lld,\"eligibleLocked\":%lld,"
        "\"eligibleResources\":%lld,\"volatilityAddon\":%lld,\"liquidityAddon\":%lld,"
        "\"concentrationAddon\":%lld,\"stressedObligation\":%lld,"
        "\"requiredCoverage\":%lld,\"surplus\":%lld,\"shortfall\":%lld,"
        "\"coverageBps\":%d,\"largestShareBps\":%d,\"policySatisfied\":%s}}\n",
        (long long)input->reserve_available,
        (long long)input->reserve_locked,
        (long long)input->pending_gross,
        (long long)input->expected_fees,
        (long long)input->largest_counterparty,
        policy->reserve_haircut_bps,
        policy->volatility_bps,
        policy->liquidity_bps,
        policy->concentration_bps,
        policy->settlement_horizon_epochs,
        policy->target_coverage_bps,
        (long long)policy->operational_floor,
        (long long)report->eligible_available,
        (long long)report->eligible_locked,
        (long long)report->eligible_resources,
        (long long)report->volatility_addon,
        (long long)report->liquidity_addon,
        (long long)report->concentration_addon,
        (long long)report->stressed_obligation,
        (long long)report->required_coverage,
        (long long)report->surplus,
        (long long)report->shortfall,
        report->coverage_bps,
        report->largest_share_bps,
        report->policy_satisfied ? "true" : "false");
}
