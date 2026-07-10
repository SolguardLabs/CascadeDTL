#include "settlement.h"
#include "risk.h"

#include <stdio.h>

static bool packet_should_fail(const Packet *packet, int leg) {
    return packet->fail_attempt > 0 &&
           packet->fail_leg > 0 &&
           packet->attempts == packet->fail_attempt &&
           leg == packet->fail_leg;
}

static bool packet_schedule_retry(CascadeState *state, Packet *packet, Receipt *receipt, CascadeStatus *status) {
    snprintf(packet->last_error,
             sizeof(packet->last_error),
             "branch failed at attempt %d leg %d",
             packet->attempts,
             packet->fail_leg);

    if (packet->hold_on_failure) {
        return ledger_mark_review_hold(state, packet, receipt, status);
    }
    if (packet->fail_terminal || packet->attempts >= packet->max_attempts) {
        return ledger_cancel_receipt(state, packet, receipt, status);
    }
    if (!ledger_release_for_retry(state, packet, receipt, status)) {
        return false;
    }
    packet->status = PACKET_DEFERRED;
    packet->not_before_epoch = state->current_epoch + packet->retry_delay;
    state->metrics.packets_deferred++;
    model_add_event(state,
                    "packet_deferred packet=%s receipt=%s next_epoch=%d attempts=%d",
                    packet->id,
                    receipt->id,
                    packet->not_before_epoch,
                    packet->attempts);
    cascade_status_clear(status);
    return true;
}

bool settlement_process_packet(CascadeState *state, Batch *batch, Packet *packet, CascadeStatus *status) {
    if (state == NULL || packet == NULL) {
        cascade_status_error(status, "settlement process packet received invalid arguments");
        return false;
    }
    if (!risk_validate_packet(state, batch, packet, status)) {
        return false;
    }
    if (packet->status == PACKET_FAILED) {
        return true;
    }
    packet->attempts++;
    packet->status = PACKET_QUEUED;

    LedgerOpenResult opened;
    if (!ledger_open_attempt(state, packet, &opened, status)) {
        packet->status = PACKET_FAILED;
        snprintf(packet->last_error, sizeof(packet->last_error), "%s", status->message);
        state->metrics.packets_failed++;
        model_add_event(state, "packet_rejected packet=%s reason=%s", packet->id, status->message);
        cascade_status_clear(status);
        return true;
    }

    Receipt *receipt = &state->receipts[opened.receipt_index];
    model_add_event(state,
                    "packet_attempt packet=%s attempt=%d score=%d mode=%s",
                    packet->id,
                    packet->attempts,
                    packet->last_score,
                    opened.mode == LEDGER_OPEN_ATTACHED ? "attached" : "fresh");

    for (int leg = 1; leg <= packet->legs; leg++) {
        if (packet_should_fail(packet, leg)) {
            model_add_event(state,
                            "branch_result packet=%s leg=%d status=retryable",
                            packet->id,
                            leg);
            return packet_schedule_retry(state, packet, receipt, status);
        }
        model_add_event(state,
                        "branch_result packet=%s leg=%d status=accepted",
                        packet->id,
                        leg);
    }

    return ledger_commit_receipt(state, packet, receipt, status);
}

bool settlement_process_batch(CascadeState *state, Batch *batch, CascadeStatus *status) {
    if (state == NULL || batch == NULL) {
        cascade_status_error(status, "settlement process batch received invalid arguments");
        return false;
    }
    state->current_epoch = batch->epoch;
    state->metrics.batches_processed++;
    model_add_event(state,
                    "batch_started id=%s packets=%d",
                    batch->id,
                    batch->packet_count);
    scheduler_enqueue_batch(state, batch);

    int guard = 0;
    while (guard++ < CASCADE_MAX_PACKETS * 4) {
        int next = scheduler_select_next(state);
        if (next < 0) {
            break;
        }
        if (!settlement_process_packet(state, batch, &state->packets[next], status)) {
            return false;
        }
    }
    if (guard >= CASCADE_MAX_PACKETS * 4) {
        cascade_status_error(status, "scheduler guard exhausted");
        return false;
    }
    model_add_event(state, "batch_completed id=%s", batch->id);
    return ledger_check_runtime_invariants(state, status);
}

bool settlement_run(CascadeState *state, CascadeStatus *status) {
    if (state == NULL) {
        cascade_status_error(status, "settlement state is null");
        return false;
    }
    for (int i = 0; i < state->batch_count; i++) {
        if (!settlement_process_batch(state, &state->batches[i], status)) {
            return false;
        }
    }
    state->metrics.receipt_count = state->receipt_count;
    return ledger_check_runtime_invariants(state, status);
}
