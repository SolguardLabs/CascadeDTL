#include "scheduler.h"

#include <limits.h>

void scheduler_enqueue_batch(CascadeState *state, Batch *batch) {
    if (state == NULL || batch == NULL) {
        return;
    }
    for (int i = 0; i < batch->packet_count; i++) {
        int packet_index = batch->packet_start + i;
        if (packet_index < 0 || packet_index >= state->packet_count) {
            continue;
        }
        Packet *packet = &state->packets[packet_index];
        if (packet->not_before_epoch <= 0) {
            packet->not_before_epoch = batch->epoch;
        }
        packet->status = PACKET_QUEUED;
        model_add_event(state,
                        "packet_queued batch=%s packet=%s not_before=%d priority=%d",
                        batch->id,
                        packet->id,
                        packet->not_before_epoch,
                        packet->base_priority);
    }
}

int scheduler_ready_count(const CascadeState *state) {
    if (state == NULL) {
        return 0;
    }
    int count = 0;
    for (int i = 0; i < state->packet_count; i++) {
        const Packet *packet = &state->packets[i];
        if ((packet->status == PACKET_QUEUED || packet->status == PACKET_DEFERRED) &&
            packet->not_before_epoch <= state->current_epoch) {
            count++;
        }
    }
    return count;
}

int scheduler_score_packet(const CascadeState *state, const Packet *packet, int ready_count) {
    if (state == NULL || packet == NULL) {
        return INT_MIN;
    }
    int congestion = state->congestion_base + ready_count * state->congestion_per_packet;
    int retry = packet->attempts > 0 ? state->retry_boost : 0;
    int delay_penalty = packet->not_before_epoch > state->current_epoch
                            ? (packet->not_before_epoch - state->current_epoch) * 3
                            : 0;
    return packet->base_priority + packet->congestion_weight * congestion + retry - delay_penalty;
}

int scheduler_select_next(CascadeState *state) {
    if (state == NULL) {
        return -1;
    }
    int ready = scheduler_ready_count(state);
    int best_index = -1;
    int best_score = INT_MIN;
    int best_sequence = INT_MAX;
    for (int i = 0; i < state->packet_count; i++) {
        Packet *packet = &state->packets[i];
        if (packet->status != PACKET_QUEUED && packet->status != PACKET_DEFERRED) {
            continue;
        }
        if (packet->not_before_epoch > state->current_epoch) {
            continue;
        }
        int score = scheduler_score_packet(state, packet, ready);
        packet->last_score = score;
        if (score > best_score || (score == best_score && packet->sequence < best_sequence)) {
            best_score = score;
            best_sequence = packet->sequence;
            best_index = i;
        }
    }
    return best_index;
}

bool scheduler_has_pending(const CascadeState *state) {
    if (state == NULL) {
        return false;
    }
    for (int i = 0; i < state->packet_count; i++) {
        const Packet *packet = &state->packets[i];
        if (packet->status == PACKET_QUEUED || packet->status == PACKET_DEFERRED) {
            return true;
        }
    }
    return false;
}
