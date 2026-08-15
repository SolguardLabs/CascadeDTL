#include "governance.h"

#include <stdio.h>
#include <string.h>

static bool field_is_canonical(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return false;
    }
    return strchr(value, '|') == NULL && strchr(value, '\n') == NULL && strchr(value, '\r') == NULL;
}

const char *governance_lifecycle_name(GovernanceLifecycle lifecycle) {
    switch (lifecycle) {
    case GOVERNANCE_PENDING:
        return "pending-approvals";
    case GOVERNANCE_TIMELOCKED:
        return "timelocked";
    case GOVERNANCE_READY:
        return "ready";
    case GOVERNANCE_EXPIRED:
        return "expired";
    case GOVERNANCE_BLOCKED_PREDECESSOR:
        return "blocked-predecessor";
    default:
        return "unknown";
    }
}

bool governance_validate(const GovernanceOperation *operation, CascadeStatus *status) {
    if (operation == NULL) {
        cascade_status_error(status, "governance operation is null");
        return false;
    }
    if (!field_is_canonical(operation->protocol) || !field_is_canonical(operation->network) ||
        !field_is_canonical(operation->target) || !field_is_canonical(operation->selector) ||
        !field_is_canonical(operation->payload_digest) || !field_is_canonical(operation->predecessor) ||
        !field_is_canonical(operation->salt)) {
        cascade_status_error(status, "governance fields must be non-empty canonical values");
        return false;
    }
    if (operation->chain_id <= 0) {
        cascade_status_error(status, "governance chain id must be positive");
        return false;
    }
    if (operation->eta < 0 || operation->expires_at <= operation->eta) {
        cascade_status_error(status, "governance execution window is invalid");
        return false;
    }
    if (operation->quorum < 1 || operation->quorum > 32) {
        cascade_status_error(status, "governance quorum must be between 1 and 32");
        return false;
    }
    if (operation->approvals < 0 || operation->approvals > 32) {
        cascade_status_error(status, "governance approvals must be between 0 and 32");
        return false;
    }
    cascade_status_clear(status);
    return true;
}

static bool operation_id(const GovernanceOperation *operation, char out[17], CascadeStatus *status) {
    TextBuffer canonical;
    text_buffer_init(&canonical);
    bool ok = text_buffer_appendf(&canonical,
                                  "CASCADE_GOVERNANCE_V1|%s|%s|%d|%s|%s|%s|%s|%s|%lld|%lld|%d",
                                  operation->protocol,
                                  operation->network,
                                  operation->chain_id,
                                  operation->target,
                                  operation->selector,
                                  operation->payload_digest,
                                  operation->predecessor,
                                  operation->salt,
                                  (long long)operation->eta,
                                  (long long)operation->expires_at,
                                  operation->quorum);
    if (!ok) {
        text_buffer_free(&canonical);
        cascade_status_error(status, "governance operation allocation failed");
        return false;
    }
    const uint64_t digest = cascade_hash64(canonical.data);
    text_buffer_free(&canonical);
    snprintf(out, 17, "%016llx", (unsigned long long)digest);
    return true;
}

bool governance_evaluate(const GovernanceOperation *operation,
                         int64_t now,
                         bool predecessor_satisfied,
                         GovernanceReport *report,
                         CascadeStatus *status) {
    if (report == NULL) {
        cascade_status_error(status, "governance report is null");
        return false;
    }
    if (!governance_validate(operation, status)) {
        return false;
    }
    if (now < 0) {
        cascade_status_error(status, "governance evaluation time cannot be negative");
        return false;
    }
    memset(report, 0, sizeof(*report));
    if (!operation_id(operation, report->operation_id, status)) {
        return false;
    }
    report->approvals_remaining = operation->approvals >= operation->quorum
                                      ? 0
                                      : operation->quorum - operation->approvals;
    report->predecessor_satisfied = predecessor_satisfied ? 1 : 0;

    if (now >= operation->expires_at) {
        report->lifecycle = GOVERNANCE_EXPIRED;
    } else if (report->approvals_remaining > 0) {
        report->lifecycle = GOVERNANCE_PENDING;
    } else if (now < operation->eta) {
        report->lifecycle = GOVERNANCE_TIMELOCKED;
    } else if (!predecessor_satisfied) {
        report->lifecycle = GOVERNANCE_BLOCKED_PREDECESSOR;
    } else {
        report->lifecycle = GOVERNANCE_READY;
        report->executable = 1;
    }
    cascade_status_clear(status);
    return true;
}

bool governance_write_json(const GovernanceOperation *operation,
                           int64_t now,
                           const GovernanceReport *report,
                           TextBuffer *out) {
    if (operation == NULL || report == NULL || out == NULL) {
        return false;
    }
    if (!text_buffer_append(out, "{\"operationId\":")) {
        return false;
    }
    if (!text_buffer_append_json_string(out, report->operation_id) ||
        !text_buffer_append(out, ",\"protocol\":") ||
        !text_buffer_append_json_string(out, operation->protocol) ||
        !text_buffer_append(out, ",\"network\":") ||
        !text_buffer_append_json_string(out, operation->network) ||
        !text_buffer_append(out, ",\"target\":") ||
        !text_buffer_append_json_string(out, operation->target) ||
        !text_buffer_append(out, ",\"selector\":") ||
        !text_buffer_append_json_string(out, operation->selector) ||
        !text_buffer_append(out, ",\"payloadDigest\":") ||
        !text_buffer_append_json_string(out, operation->payload_digest) ||
        !text_buffer_append(out, ",\"predecessor\":") ||
        !text_buffer_append_json_string(out, operation->predecessor) ||
        !text_buffer_append(out, ",\"salt\":") ||
        !text_buffer_append_json_string(out, operation->salt)) {
        return false;
    }
    return text_buffer_appendf(out,
                               ",\"chainId\":%d,\"eta\":%lld,\"expiresAt\":%lld,"
                               "\"quorum\":%d,\"approvals\":%d,\"evaluatedAt\":%lld,"
                               "\"approvalsRemaining\":%d,\"predecessorSatisfied\":%s,"
                               "\"lifecycle\":\"%s\",\"executable\":%s}\n",
                               operation->chain_id,
                               (long long)operation->eta,
                               (long long)operation->expires_at,
                               operation->quorum,
                               operation->approvals,
                               (long long)now,
                               report->approvals_remaining,
                               report->predecessor_satisfied ? "true" : "false",
                               governance_lifecycle_name(report->lifecycle),
                               report->executable ? "true" : "false");
}
