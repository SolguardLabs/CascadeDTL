#ifndef CASCADE_GOVERNANCE_H
#define CASCADE_GOVERNANCE_H

#include "common.h"

typedef enum GovernanceLifecycle {
    GOVERNANCE_PENDING,
    GOVERNANCE_TIMELOCKED,
    GOVERNANCE_READY,
    GOVERNANCE_EXPIRED,
    GOVERNANCE_BLOCKED_PREDECESSOR
} GovernanceLifecycle;

typedef struct GovernanceOperation {
    char protocol[CASCADE_ID_LEN];
    char network[CASCADE_ID_LEN];
    int chain_id;
    char target[CASCADE_ID_LEN];
    char selector[CASCADE_ID_LEN];
    char payload_digest[CASCADE_ID_LEN];
    char predecessor[CASCADE_ID_LEN];
    char salt[CASCADE_ID_LEN];
    int64_t eta;
    int64_t expires_at;
    int quorum;
    int approvals;
} GovernanceOperation;

typedef struct GovernanceReport {
    char operation_id[17];
    GovernanceLifecycle lifecycle;
    int approvals_remaining;
    int predecessor_satisfied;
    int executable;
} GovernanceReport;

const char *governance_lifecycle_name(GovernanceLifecycle lifecycle);
bool governance_validate(const GovernanceOperation *operation, CascadeStatus *status);
bool governance_evaluate(const GovernanceOperation *operation,
                         int64_t now,
                         bool predecessor_satisfied,
                         GovernanceReport *report,
                         CascadeStatus *status);
bool governance_write_json(const GovernanceOperation *operation,
                           int64_t now,
                           const GovernanceReport *report,
                           TextBuffer *out);

#endif
