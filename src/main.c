#include "report.h"
#include "settlement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_help(void) {
    puts("CascadeDTL batch settlement simulator");
    puts("");
    puts("Usage:");
    puts("  cascadedtl run <fixture.json> [--json] [--events] [--strict]");
    puts("  cascadedtl validate <fixture.json>");
    puts("  cascadedtl --help");
    puts("  cascadedtl --version");
}

static int emit_error(const char *prefix, const CascadeStatus *status) {
    fprintf(stderr, "%s: %s\n", prefix, status != NULL ? status->message : "unknown error");
    return 1;
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
        puts("CascadeDTL 0.1.0");
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
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    print_help();
    return 1;
}
