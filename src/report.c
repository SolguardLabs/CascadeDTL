#include "report.h"
#include "reconcile.h"

#include <stdio.h>

static bool append_comma(TextBuffer *out, int index) {
    if (index > 0) {
        return text_buffer_append(out, ",");
    }
    return true;
}

static bool append_participants(const CascadeState *state, TextBuffer *out) {
    if (!text_buffer_append(out, "\"participants\":[")) {
        return false;
    }
    for (int i = 0; i < state->participant_count; i++) {
        const Participant *p = &state->participants[i];
        if (!append_comma(out, i)) {
            return false;
        }
        if (!text_buffer_append(out, "{")) {
            return false;
        }
        if (!text_buffer_append(out, "\"id\":") || !text_buffer_append_json_string(out, p->id)) {
            return false;
        }
        if (!text_buffer_appendf(out,
                                 ",\"balance\":%lld,\"received\":%lld,\"fees\":%lld,\"unsettled\":%lld",
                                 (long long)p->balance,
                                 (long long)p->received,
                                 (long long)p->fees,
                                 (long long)p->unsettled)) {
            return false;
        }
        if (!text_buffer_append(out, "}")) {
            return false;
        }
    }
    return text_buffer_append(out, "]");
}

static bool append_reserves(const CascadeState *state, TextBuffer *out) {
    if (!text_buffer_append(out, "\"reserves\":[")) {
        return false;
    }
    for (int i = 0; i < state->reserve_count; i++) {
        const Reserve *r = &state->reserves[i];
        if (!append_comma(out, i)) {
            return false;
        }
        if (!text_buffer_append(out, "{")) {
            return false;
        }
        if (!text_buffer_append(out, "\"id\":") || !text_buffer_append_json_string(out, r->id)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"owner\":") || !text_buffer_append_json_string(out, r->owner)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"lane\":") || !text_buffer_append_json_string(out, r->lane)) {
            return false;
        }
        if (!text_buffer_appendf(out,
                                 ",\"available\":%lld,\"locked\":%lld,\"settled\":%lld,"
                                 "\"exposure\":%lld,\"frozen\":%s,\"generation\":%d",
                                 (long long)r->available,
                                 (long long)r->locked,
                                 (long long)r->settled,
                                 (long long)r->exposure,
                                 r->frozen ? "true" : "false",
                                 r->generation)) {
            return false;
        }
        if (!text_buffer_append(out, "}")) {
            return false;
        }
    }
    return text_buffer_append(out, "]");
}

static bool append_packets(const CascadeState *state, TextBuffer *out) {
    if (!text_buffer_append(out, "\"packets\":[")) {
        return false;
    }
    for (int i = 0; i < state->packet_count; i++) {
        const Packet *p = &state->packets[i];
        if (!append_comma(out, i)) {
            return false;
        }
        if (!text_buffer_append(out, "{")) {
            return false;
        }
        if (!text_buffer_append(out, "\"id\":") || !text_buffer_append_json_string(out, p->id)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"reserve\":") || !text_buffer_append_json_string(out, p->reserve_id)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"beneficiary\":") || !text_buffer_append_json_string(out, p->beneficiary)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"status\":") || !text_buffer_append_json_string(out, packet_status_name(p->status))) {
            return false;
        }
        if (!text_buffer_appendf(out,
                                 ",\"amount\":%lld,\"fee\":%lld,\"attempts\":%d,\"notBefore\":%d,"
                                 "\"lastScore\":%d,\"receiptIndex\":%d",
                                 (long long)p->amount,
                                 (long long)p->fee,
                                 p->attempts,
                                 p->not_before_epoch,
                                 p->last_score,
                                 p->receipt_index)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"lastError\":") || !text_buffer_append_json_string(out, p->last_error)) {
            return false;
        }
        if (!text_buffer_append(out, "}")) {
            return false;
        }
    }
    return text_buffer_append(out, "]");
}

static bool append_receipts(const CascadeState *state, TextBuffer *out) {
    if (!text_buffer_append(out, "\"receipts\":[")) {
        return false;
    }
    for (int i = 0; i < state->receipt_count; i++) {
        const Receipt *r = &state->receipts[i];
        if (!append_comma(out, i)) {
            return false;
        }
        if (!text_buffer_append(out, "{")) {
            return false;
        }
        if (!text_buffer_append(out, "\"id\":") || !text_buffer_append_json_string(out, r->id)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"packet\":") || !text_buffer_append_json_string(out, r->packet_id)) {
            return false;
        }
        if (!text_buffer_append(out, ",\"reserve\":") || !text_buffer_append_json_string(out, r->reserve_id)) {
            return false;
        }
        if (!text_buffer_appendf(out,
                                 ",\"amount\":%lld,\"attempt\":%d,\"openedEpoch\":%d,"
                                 "\"generation\":%d,\"valid\":%s,\"consumed\":%s,"
                                 "\"retryable\":%s,\"reused\":%d",
                                 (long long)r->amount,
                                 r->attempt,
                                 r->opened_epoch,
                                 r->reserve_generation,
                                 r->valid ? "true" : "false",
                                 r->consumed ? "true" : "false",
                                 r->retryable ? "true" : "false",
                                 r->reused_count)) {
            return false;
        }
        if (!text_buffer_append(out, "}")) {
            return false;
        }
    }
    return text_buffer_append(out, "]");
}

static bool append_batches(const CascadeState *state, TextBuffer *out) {
    if (!text_buffer_append(out, "\"batches\":[")) {
        return false;
    }
    for (int i = 0; i < state->batch_count; i++) {
        const Batch *b = &state->batches[i];
        if (!append_comma(out, i)) {
            return false;
        }
        if (!text_buffer_append(out, "{\"id\":") || !text_buffer_append_json_string(out, b->id)) {
            return false;
        }
        if (!text_buffer_appendf(out,
                                 ",\"epoch\":%d,\"packetStart\":%d,\"packetCount\":%d}",
                                 b->epoch,
                                 b->packet_start,
                                 b->packet_count)) {
            return false;
        }
    }
    return text_buffer_append(out, "]");
}

static bool append_lane_stats(const CascadeState *state, TextBuffer *out) {
    ReconcileReport reconcile;
    if (!reconcile_build(state, &reconcile)) {
        return false;
    }
    if (!text_buffer_append(out, "\"laneStats\":[")) {
        return false;
    }
    for (int i = 0; i < reconcile.lane_count; i++) {
        const LaneSnapshot *lane = &reconcile.lanes[i];
        if (!append_comma(out, i)) {
            return false;
        }
        if (!text_buffer_append(out, "{\"id\":") || !text_buffer_append_json_string(out, lane->id)) {
            return false;
        }
        if (!text_buffer_appendf(out,
                                 ",\"reserves\":%d,\"packets\":%d,\"settledPackets\":%d,"
                                 "\"failedPackets\":%d,\"deferredPackets\":%d,\"available\":%lld,"
                                 "\"locked\":%lld,\"settled\":%lld,\"exposure\":%lld,"
                                 "\"grossPackets\":%lld,\"grossFees\":%lld}",
                                 lane->reserve_count,
                                 lane->packet_count,
                                 lane->settled_packets,
                                 lane->failed_packets,
                                 lane->deferred_packets,
                                 (long long)lane->available,
                                 (long long)lane->locked,
                                 (long long)lane->settled,
                                 (long long)lane->exposure,
                                 (long long)lane->gross_packets,
                                 (long long)lane->gross_fees)) {
            return false;
        }
    }
    return text_buffer_append(out, "]");
}

static bool append_metrics(const CascadeState *state, TextBuffer *out) {
    const CascadeMetrics *m = &state->metrics;
    return text_buffer_appendf(out,
                               "\"metrics\":{\"batchesProcessed\":%d,\"packetsSettled\":%d,"
                               "\"packetsFailed\":%d,\"packetsDeferred\":%d,\"policyRejections\":%d,"
                               "\"receiptCount\":%d,"
                               "\"receiptReplays\":%d,\"reserveReleases\":%d,\"grossSettled\":%lld,"
                               "\"grossFees\":%lld,\"grossExposure\":%lld}",
                               m->batches_processed,
                               m->packets_settled,
                               m->packets_failed,
                               m->packets_deferred,
                               m->policy_rejections,
                               m->receipt_count,
                               m->receipt_replays,
                               m->reserve_releases,
                               (long long)m->gross_settled,
                               (long long)m->gross_fees,
                               (long long)m->gross_exposure);
}

static bool append_events(const CascadeState *state, TextBuffer *out) {
    if (!text_buffer_append(out, "\"events\":[")) {
        return false;
    }
    for (size_t i = 0; i < state->events.len; i++) {
        if (!append_comma(out, (int)i)) {
            return false;
        }
        if (!text_buffer_append_json_string(out, state->events.items[i])) {
            return false;
        }
    }
    return text_buffer_append(out, "]");
}

bool report_write_json(const CascadeState *state, TextBuffer *out, bool include_events) {
    if (state == NULL || out == NULL) {
        return false;
    }
    if (!text_buffer_append(out, "{")) {
        return false;
    }
    if (!text_buffer_append(out, "\"scenario\":") || !text_buffer_append_json_string(out, state->scenario_id)) {
        return false;
    }
    if (!text_buffer_appendf(out,
                             ",\"currentEpoch\":%d,\"ok\":true,\"strictAccounting\":%s,",
                             state->current_epoch,
                             state->strict_accounting ? "true" : "false")) {
        return false;
    }
    if (!append_metrics(state, out)) {
        return false;
    }
    if (!text_buffer_append(out, ",")) {
        return false;
    }
    if (!append_participants(state, out)) {
        return false;
    }
    if (!text_buffer_append(out, ",")) {
        return false;
    }
    if (!append_reserves(state, out)) {
        return false;
    }
    if (!text_buffer_append(out, ",")) {
        return false;
    }
    if (!append_packets(state, out)) {
        return false;
    }
    if (!text_buffer_append(out, ",")) {
        return false;
    }
    if (!append_receipts(state, out)) {
        return false;
    }
    if (!text_buffer_append(out, ",")) {
        return false;
    }
    if (!append_lane_stats(state, out)) {
        return false;
    }
    if (!text_buffer_append(out, ",")) {
        return false;
    }
    if (!append_batches(state, out)) {
        return false;
    }
    if (include_events) {
        if (!text_buffer_append(out, ",")) {
            return false;
        }
        if (!append_events(state, out)) {
            return false;
        }
    }
    return text_buffer_append(out, "}\n");
}

bool report_write_summary(const CascadeState *state, TextBuffer *out) {
    if (state == NULL || out == NULL) {
        return false;
    }
    return text_buffer_appendf(out,
                               "CascadeDTL scenario=%s epoch=%d settled=%d failed=%d deferred=%d receipts=%d\n",
                               state->scenario_id,
                               state->current_epoch,
                               state->metrics.packets_settled,
                               state->metrics.packets_failed,
                               state->metrics.packets_deferred,
                               state->receipt_count);
}
