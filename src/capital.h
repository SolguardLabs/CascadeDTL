#ifndef CASCADE_CAPITAL_H
#define CASCADE_CAPITAL_H

#include "common.h"

#define CASCADE_BPS 10000
#define CASCADE_MAX_HORIZON_EPOCHS 365

typedef struct CapitalPolicy {
    int reserve_haircut_bps;
    int volatility_bps;
    int liquidity_bps;
    int concentration_bps;
    int settlement_horizon_epochs;
    int target_coverage_bps;
    CascadeAmount operational_floor;
} CapitalPolicy;

typedef struct CapitalInput {
    CascadeAmount reserve_available;
    CascadeAmount reserve_locked;
    CascadeAmount pending_gross;
    CascadeAmount expected_fees;
    CascadeAmount largest_counterparty;
} CapitalInput;

typedef struct CapitalReport {
    CascadeAmount eligible_available;
    CascadeAmount eligible_locked;
    CascadeAmount eligible_resources;
    CascadeAmount volatility_addon;
    CascadeAmount liquidity_addon;
    CascadeAmount concentration_addon;
    CascadeAmount stressed_obligation;
    CascadeAmount required_coverage;
    CascadeAmount surplus;
    CascadeAmount shortfall;
    int coverage_bps;
    int largest_share_bps;
    int policy_satisfied;
} CapitalReport;

bool capital_validate_policy(const CapitalPolicy *policy, CascadeStatus *status);
bool capital_validate_input(const CapitalInput *input, CascadeStatus *status);
bool capital_compute(const CapitalPolicy *policy,
                     const CapitalInput *input,
                     CapitalReport *report,
                     CascadeStatus *status);
bool capital_write_json(const CapitalPolicy *policy,
                        const CapitalInput *input,
                        const CapitalReport *report,
                        TextBuffer *out);

#endif
