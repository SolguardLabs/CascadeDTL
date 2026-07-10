#ifndef CASCADE_LEDGER_H
#define CASCADE_LEDGER_H

#include "model.h"

typedef enum LedgerOpenMode {
    LEDGER_OPEN_FRESH,
    LEDGER_OPEN_ATTACHED
} LedgerOpenMode;

typedef struct LedgerOpenResult {
    int receipt_index;
    LedgerOpenMode mode;
} LedgerOpenResult;

bool ledger_open_attempt(CascadeState *state, Packet *packet, LedgerOpenResult *out, CascadeStatus *status);
bool ledger_release_for_retry(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status);
bool ledger_commit_receipt(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status);
bool ledger_cancel_receipt(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status);
bool ledger_mark_review_hold(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status);

bool ledger_credit_participant(CascadeState *state, const char *participant_id, CascadeAmount amount, CascadeStatus *status);
bool ledger_credit_fee(CascadeState *state, const char *participant_id, CascadeAmount amount, CascadeStatus *status);
bool ledger_check_runtime_invariants(const CascadeState *state, CascadeStatus *status);

#endif
