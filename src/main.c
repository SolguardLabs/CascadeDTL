#include "capital.h"
#include "governance.h"
#include "report.h"
#include "settlement.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void) {
    puts("CascadeDTL deterministic settlement engine");
    puts("");
    puts("Usage:");
    puts("  cascadedtl run <fixture.json> [--json] [--events] [--strict]");
    puts("  cascadedtl validate <fixture.json>");
    puts("  cascadedtl capital <available> <locked> <pending> <fees> <largest> <haircut-bps>");
    puts("                     <volatility-bps> <liquidity-bps> <concentration-bps>");
    puts("                     <horizon> <target-bps> <operational-floor>");
    puts("  cascadedtl governance <protocol> <network> <chain-id> <target> <selector>");
    puts("                        <payload-digest> <predecessor> <salt> <eta> <expires-at>");
    puts("                        <quorum> <approvals> <now> <predecessor-satisfied>");
    puts("  cascadedtl --help");
    puts("  cascadedtl --version");
}

static int emit_error(const char *prefix, const CascadeStatus *status) {
    fprintf(stderr, "%s: %s\n", prefix, status != NULL ? status->message : "unknown error");
    return 1;
}

static bool parse_amount_arg(const char *text, CascadeAmount *out) {
    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    errno = 0;
    char *end = NULL;
    const long long parsed = strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') {
        return false;
    }
    *out = (CascadeAmount)parsed;
    return true;
}

static bool parse_int_arg(const char *text, int *out) {
    CascadeAmount parsed = 0;
    if (!parse_amount_arg(text, &parsed) || parsed < INT_MIN || parsed > INT_MAX || out == NULL) {
        return false;
    }
    *out = (int)parsed;
    return true;
}

static bool copy_cli_id(char out[CASCADE_ID_LEN], const char *value) {
    if (out == NULL || value == NULL || value[0] == '\0' || strlen(value) >= CASCADE_ID_LEN) {
        return false;
    }
    cascade_copy_id(out, value);
    return true;
}

static int command_capital(int argc, char **argv) {
    if (argc != 14) {
        print_help();
        return 1;
    }
    CapitalInput input;
    CapitalPolicy policy;
    memset(&input, 0, sizeof(input));
    memset(&policy, 0, sizeof(policy));
    if (!parse_amount_arg(argv[2], &input.reserve_available) ||
        !parse_amount_arg(argv[3], &input.reserve_locked) ||
        !parse_amount_arg(argv[4], &input.pending_gross) ||
        !parse_amount_arg(argv[5], &input.expected_fees) ||
        !parse_amount_arg(argv[6], &input.largest_counterparty) ||
        !parse_int_arg(argv[7], &policy.reserve_haircut_bps) ||
        !parse_int_arg(argv[8], &policy.volatility_bps) ||
        !parse_int_arg(argv[9], &policy.liquidity_bps) ||
        !parse_int_arg(argv[10], &policy.concentration_bps) ||
        !parse_int_arg(argv[11], &policy.settlement_horizon_epochs) ||
        !parse_int_arg(argv[12], &policy.target_coverage_bps) ||
        !parse_amount_arg(argv[13], &policy.operational_floor)) {
        fputs("capital: all numeric arguments must be base-10 integers\n", stderr);
        return 1;
    }

    CascadeStatus status;
    cascade_status_clear(&status);
    CapitalReport report;
    if (!capital_compute(&policy, &input, &report, &status)) {
        return emit_error("capital", &status);
    }
    TextBuffer output;
    text_buffer_init(&output);
    if (!capital_write_json(&policy, &input, &report, &output)) {
        text_buffer_free(&output);
        fputs("capital: out of memory\n", stderr);
        return 1;
    }
    fputs(output.data != NULL ? output.data : "", stdout);
    text_buffer_free(&output);
    return 0;
}

static int command_governance(int argc, char **argv) {
    if (argc != 16) {
        print_help();
        return 1;
    }
    GovernanceOperation operation;
    memset(&operation, 0, sizeof(operation));
    int predecessor_satisfied = 0;
    CascadeAmount evaluated_at = 0;
    if (!copy_cli_id(operation.protocol, argv[2]) ||
        !copy_cli_id(operation.network, argv[3]) ||
        !parse_int_arg(argv[4], &operation.chain_id) ||
        !copy_cli_id(operation.target, argv[5]) ||
        !copy_cli_id(operation.selector, argv[6]) ||
        !copy_cli_id(operation.payload_digest, argv[7]) ||
        !copy_cli_id(operation.predecessor, argv[8]) ||
        !copy_cli_id(operation.salt, argv[9]) ||
        !parse_amount_arg(argv[10], &operation.eta) ||
        !parse_amount_arg(argv[11], &operation.expires_at) ||
        !parse_int_arg(argv[12], &operation.quorum) ||
        !parse_int_arg(argv[13], &operation.approvals) ||
        !parse_amount_arg(argv[14], &evaluated_at) ||
        !parse_int_arg(argv[15], &predecessor_satisfied)) {
        fputs("governance: invalid identifier or numeric argument\n", stderr);
        return 1;
    }
    int64_t now = evaluated_at;
    if (predecessor_satisfied != 0 && predecessor_satisfied != 1) {
        fputs("governance: predecessor-satisfied must be 0 or 1\n", stderr);
        return 1;
    }

    CascadeStatus status;
    cascade_status_clear(&status);
    GovernanceReport report;
    if (!governance_evaluate(&operation, now, predecessor_satisfied == 1, &report, &status)) {
        return emit_error("governance", &status);
    }
    TextBuffer output;
    text_buffer_init(&output);
    if (!governance_write_json(&operation, now, &report, &output)) {
        text_buffer_free(&output);
        fputs("governance: out of memory\n", stderr);
        return 1;
    }
    fputs(output.data != NULL ? output.data : "", stdout);
    text_buffer_free(&output);
    return 0;
}

static int command_validate(const char *path) {
    CascadeStatus status;
    cascade_status_clear(&status);
    CascadeState state;
    model_state_init(&state);
    bool ok = model_load_file(&state, path, &status);
    model_state_free(&state);
    if (!ok) {
        return emit_error("validate", &status);
    }
    puts("ok");
    return 0;
}

static int command_run(int argc, char **argv) {
    if (argc < 3) {
        print_help();
        return 1;
    }
    const char *path = argv[2];
    bool json = false;
    bool events = false;
    bool strict = false;
    for (int i = 3; i < argc; i++) {
        if (cascade_streq(argv[i], "--json")) {
            json = true;
        } else if (cascade_streq(argv[i], "--events")) {
            events = true;
        } else if (cascade_streq(argv[i], "--strict")) {
            strict = true;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    CascadeStatus status;
    cascade_status_clear(&status);
    CascadeState state;
    model_state_init(&state);
    state.strict_accounting = strict ? 1 : 0;

    if (!model_load_file(&state, path, &status)) {
        model_state_free(&state);
        return emit_error("load", &status);
    }
    if (!settlement_run(&state, &status)) {
        model_state_free(&state);
        return emit_error("settlement", &status);
    }

    TextBuffer output;
    text_buffer_init(&output);
    bool wrote = json || events
                    ? report_write_json(&state, &output, events)
                    : report_write_summary(&state, &output);
    if (!wrote) {
        text_buffer_free(&output);
        model_state_free(&state);
        fprintf(stderr, "report: out of memory\n");
        return 1;
    }
    fputs(output.data != NULL ? output.data : "", stdout);
    text_buffer_free(&output);
    model_state_free(&state);
    return 0;
}

int main(int argc, char **argv) {
    if (argc <= 1 || cascade_streq(argv[1], "--help") || cascade_streq(argv[1], "-h")) {
        print_help();
        return argc <= 1 ? 1 : 0;
    }
    if (cascade_streq(argv[1], "--version")) {
        puts("CascadeDTL 1.0.0");
        return 0;
    }
    if (cascade_streq(argv[1], "validate")) {
        if (argc != 3) {
            print_help();
            return 1;
        }
        return command_validate(argv[2]);
    }
    if (cascade_streq(argv[1], "run")) {
        return command_run(argc, argv);
    }
    if (cascade_streq(argv[1], "capital")) {
        return command_capital(argc, argv);
    }
    if (cascade_streq(argv[1], "governance")) {
        return command_governance(argc, argv);
    }
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    print_help();
    return 1;
}
