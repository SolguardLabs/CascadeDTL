#include "ledger.h"

#include <stdio.h>
#include <string.h>

static void receipt_id_for(char out[CASCADE_ID_LEN], const Packet *packet, int attempt) {
    snprintf(out, CASCADE_ID_LEN, "rcpt-%.48s-%02d", packet->id, attempt);
}

static bool reserve_lock_amount(CascadeState *state, Reserve *reserve, Packet *packet, CascadeStatus *status) {
    if (reserve->frozen) {
        cascade_status_error(status, "reserve %s is frozen", reserve->id);
        return false;
    }
    if (reserve->available < packet->amount) {
        cascade_status_error(status,
                             "reserve %s has insufficient available coverage for %s",
                             reserve->id,
                             packet->id);
        return false;
    }
    reserve->available -= packet->amount;
    reserve->locked += packet->amount;
    reserve->generation++;
    model_add_event(state,
                    "reserve_locked reserve=%s packet=%s amount=%lld available=%lld locked=%lld",
                    reserve->id,
                    packet->id,
                    (long long)packet->amount,
                    (long long)reserve->available,
                    (long long)reserve->locked);
    return true;
}

static bool receipt_can_attach(const Receipt *receipt, const Packet *packet) {
    if (receipt == NULL || packet == NULL) {
        return false;
    }
    return receipt->valid &&
           !receipt->consumed &&
           receipt->retryable &&
           cascade_streq(receipt->packet_id, packet->id) &&
           cascade_streq(receipt->reserve_id, packet->reserve_id) &&
           receipt->amount == packet->amount;
}

bool ledger_open_attempt(CascadeState *state, Packet *packet, LedgerOpenResult *out, CascadeStatus *status) {
    if (state == NULL || packet == NULL || out == NULL) {
        cascade_status_error(status, "ledger open attempt received invalid arguments");
        return false;
    }
    out->receipt_index = -1;
    out->mode = LEDGER_OPEN_FRESH;

    if (packet->receipt_index >= 0 && packet->receipt_index < state->receipt_count) {
        Receipt *receipt = &state->receipts[packet->receipt_index];
        if (receipt_can_attach(receipt, packet)) {
            receipt->reused_count++;
            state->metrics.receipt_replays++;
            out->receipt_index = packet->receipt_index;
            out->mode = LEDGER_OPEN_ATTACHED;
            model_add_event(state,
                            "receipt_attached packet=%s receipt=%s attempt=%d reused=%d",
                            packet->id,
                            receipt->id,
                            packet->attempts,
                            receipt->reused_count);
            cascade_status_clear(status);
            return true;
        }
    }

    int reserve_index = model_find_reserve(state, packet->reserve_id);
    if (reserve_index < 0) {
        cascade_status_error(status, "packet %s references missing reserve %s", packet->id, packet->reserve_id);
        return false;
    }
    Reserve *reserve = &state->reserves[reserve_index];
    if (!reserve_lock_amount(state, reserve, packet, status)) {
        return false;
    }
    if (state->receipt_count >= CASCADE_MAX_RECEIPTS) {
        cascade_status_error(status, "receipt capacity exceeded");
        return false;
    }

    Receipt *receipt = &state->receipts[state->receipt_count];
    memset(receipt, 0, sizeof(*receipt));
    receipt_id_for(receipt->id, packet, packet->attempts);
    cascade_copy_id(receipt->packet_id, packet->id);
    cascade_copy_id(receipt->reserve_id, packet->reserve_id);
    receipt->amount = packet->amount;
    receipt->attempt = packet->attempts;
    receipt->opened_epoch = state->current_epoch;
    receipt->reserve_generation = reserve->generation;
    receipt->valid = 1;
    receipt->consumed = 0;
    receipt->retryable = 0;
    receipt->reused_count = 0;

    packet->receipt_index = state->receipt_count;
    out->receipt_index = state->receipt_count;
    out->mode = LEDGER_OPEN_FRESH;
    state->receipt_count++;
    state->metrics.receipt_count = state->receipt_count;

    model_add_event(state,
                    "receipt_opened packet=%s receipt=%s reserve=%s amount=%lld generation=%d",
                    packet->id,
                    receipt->id,
                    reserve->id,
                    (long long)receipt->amount,
                    receipt->reserve_generation);
    cascade_status_clear(status);
    return true;
}

bool ledger_release_for_retry(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status) {
    if (state == NULL || packet == NULL || receipt == NULL) {
        cascade_status_error(status, "ledger release received invalid arguments");
        return false;
    }
    int reserve_index = model_find_reserve(state, receipt->reserve_id);
    if (reserve_index < 0) {
        cascade_status_error(status, "receipt %s references missing reserve %s", receipt->id, receipt->reserve_id);
        return false;
    }
    Reserve *reserve = &state->reserves[reserve_index];
    if (packet->release_on_failure) {
        CascadeAmount released = receipt->amount;
        if (reserve->locked >= released) {
            reserve->locked -= released;
        } else {
            reserve->exposure += released - reserve->locked;
            reserve->locked = 0;
        }
        reserve->available += released;
        reserve->generation++;
        state->metrics.reserve_releases++;
        model_add_event(state,
                        "reserve_released reserve=%s packet=%s receipt=%s amount=%lld available=%lld locked=%lld",
                        reserve->id,
                        packet->id,
                        receipt->id,
                        (long long)released,
                        (long long)reserve->available,
                        (long long)reserve->locked);
    } else {
        model_add_event(state,
                        "reserve_retained reserve=%s packet=%s receipt=%s amount=%lld",
                        reserve->id,
                        packet->id,
                        receipt->id,
                        (long long)receipt->amount);
    }
    receipt->retryable = 1;
    receipt->valid = 1;
    receipt->consumed = 0;
    cascade_status_clear(status);
    return true;
}

bool ledger_credit_participant(CascadeState *state, const char *participant_id, CascadeAmount amount, CascadeStatus *status) {
    if (amount == 0 || participant_id == NULL || participant_id[0] == '\0') {
        cascade_status_clear(status);
        return true;
    }
    int idx = model_find_participant(state, participant_id);
    if (idx < 0) {
        cascade_status_error(status, "participant %s not found", participant_id);
        return false;
    }
    state->participants[idx].balance += amount;
    state->participants[idx].received += amount;
    return true;
}

bool ledger_credit_fee(CascadeState *state, const char *participant_id, CascadeAmount amount, CascadeStatus *status) {
    if (amount == 0 || participant_id == NULL || participant_id[0] == '\0') {
        cascade_status_clear(status);
        return true;
    }
    int idx = model_find_participant(state, participant_id);
    if (idx < 0) {
        cascade_status_error(status, "fee account %s not found", participant_id);
        return false;
    }
    state->participants[idx].balance += amount;
    state->participants[idx].fees += amount;
    return true;
}

bool ledger_commit_receipt(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status) {
    if (state == NULL || packet == NULL || receipt == NULL) {
        cascade_status_error(status, "ledger commit received invalid arguments");
        return false;
    }
    if (!receipt->valid || receipt->consumed) {
        cascade_status_error(status, "receipt %s is not open", receipt->id);
        return false;
    }
    int reserve_index = model_find_reserve(state, receipt->reserve_id);
    if (reserve_index < 0) {
        cascade_status_error(status, "receipt %s references missing reserve", receipt->id);
        return false;
    }
    Reserve *reserve = &state->reserves[reserve_index];

    CascadeAmount principal = packet->amount - packet->fee;
    if (principal < 0) {
        cascade_status_error(status, "packet %s has invalid principal", packet->id);
        return false;
    }

    CascadeAmount consumed_from_lock = cascade_amount_min(reserve->locked, receipt->amount);
    reserve->locked -= consumed_from_lock;
    if (consumed_from_lock < receipt->amount) {
        CascadeAmount gap = receipt->amount - consumed_from_lock;
        reserve->exposure += gap;
        state->metrics.gross_exposure += gap;
        model_add_event(state,
                        "reserve_exposure reserve=%s packet=%s receipt=%s amount=%lld",
                        reserve->id,
                        packet->id,
                        receipt->id,
                        (long long)gap);
    }
    reserve->settled += receipt->amount;
    reserve->generation++;

    if (!ledger_credit_participant(state, packet->beneficiary, principal, status)) {
        return false;
    }
    if (!ledger_credit_fee(state, packet->fee_account, packet->fee, status)) {
        return false;
    }

    receipt->consumed = 1;
    receipt->retryable = 0;
    packet->status = PACKET_SETTLED;
    state->metrics.packets_settled++;
    state->metrics.gross_settled += packet->amount;
    state->metrics.gross_fees += packet->fee;

    model_add_event(state,
                    "packet_settled packet=%s receipt=%s reserve=%s principal=%lld fee=%lld",
                    packet->id,
                    receipt->id,
                    reserve->id,
                    (long long)principal,
                    (long long)packet->fee);
    cascade_status_clear(status);
    return true;
}

bool ledger_cancel_receipt(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status) {
    if (state == NULL || packet == NULL || receipt == NULL) {
        cascade_status_error(status, "ledger cancel received invalid arguments");
        return false;
    }
    int reserve_index = model_find_reserve(state, receipt->reserve_id);
    if (reserve_index < 0) {
        cascade_status_error(status, "receipt %s references missing reserve", receipt->id);
        return false;
    }
    Reserve *reserve = &state->reserves[reserve_index];
    if (!receipt->consumed && receipt->valid) {
        CascadeAmount released = cascade_amount_min(reserve->locked, receipt->amount);
        reserve->locked -= released;
        reserve->available += released;
        reserve->generation++;
    }
    receipt->valid = 0;
    receipt->retryable = 0;
    packet->status = PACKET_FAILED;
    state->metrics.packets_failed++;
    model_add_event(state,
                    "packet_failed packet=%s receipt=%s reason=%s",
                    packet->id,
                    receipt->id,
                    packet->last_error);
    cascade_status_clear(status);
    return true;
}

bool ledger_mark_review_hold(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status) {
    if (state == NULL || packet == NULL || receipt == NULL) {
        cascade_status_error(status, "review hold received invalid arguments");
        return false;
    }
    int reserve_index = model_find_reserve(state, receipt->reserve_id);
    if (reserve_index < 0) {
        cascade_status_error(status, "receipt %s references missing reserve", receipt->id);
        return false;
    }
    Reserve *reserve = &state->reserves[reserve_index];
    reserve->frozen = 1;
    receipt->retryable = 0;
    packet->status = PACKET_REVIEW;
    state->metrics.packets_failed++;
    model_add_event(state,
                    "packet_review packet=%s reserve=%s receipt=%s",
                    packet->id,
                    reserve->id,
                    receipt->id);
    cascade_status_clear(status);
    return true;
}

bool ledger_check_runtime_invariants(const CascadeState *state, CascadeStatus *status) {
    if (state == NULL) {
        cascade_status_error(status, "state is null");
        return false;
    }
    for (int i = 0; i < state->reserve_count; i++) {
        const Reserve *reserve = &state->reserves[i];
        if (reserve->available < 0 || reserve->locked < 0 || reserve->settled < 0) {
            cascade_status_error(status, "reserve %s has negative accounting", reserve->id);
            return false;
        }
        if (state->strict_accounting && reserve->exposure > 0) {
            cascade_status_error(status, "reserve %s has exposure %lld", reserve->id, (long long)reserve->exposure);
            return false;
        }
    }
    cascade_status_clear(status);
    return true;
}
