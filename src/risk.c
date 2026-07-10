#include "risk.h"

#include <stdio.h>

const LanePolicy *risk_policy_for_packet(const CascadeState *state, const Packet *packet) {
    if (state == NULL || packet == NULL) {
        return NULL;
    }
    int reserve_index = model_find_reserve(state, packet->reserve_id);
    if (reserve_index < 0) {
        return NULL;
    }
    const Reserve *reserve = &state->reserves[reserve_index];
    int policy_index = model_find_lane_policy(state, reserve->lane);
    if (policy_index < 0) {
        return NULL;
    }
    return &state->lane_policies[policy_index];
}

CascadeAmount risk_fee_cap_for_packet(const LanePolicy *policy, const Packet *packet) {
    if (policy == NULL || packet == NULL || policy->max_fee_bps < 0) {
        return packet != NULL ? packet->amount : 0;
    }
    CascadeAmount scaled = 0;
    if (!cascade_amount_mul(packet->amount, (CascadeAmount)policy->max_fee_bps, &scaled)) {
        return -1;
    }
    return scaled / 10000;
}

static bool reject_packet(CascadeState *state, Packet *packet, CascadeStatus *status, const char *reason) {
    snprintf(packet->last_error, sizeof(packet->last_error), "%s", reason);
    packet->status = PACKET_FAILED;
    state->metrics.packets_failed++;
    state->metrics.policy_rejections++;
    model_add_event(state, "packet_policy_rejected packet=%s reason=%s", packet->id, reason);
    cascade_status_clear(status);
    return true;
}

bool risk_validate_packet(CascadeState *state, const Batch *batch, Packet *packet, CascadeStatus *status) {
    (void)batch;
    if (state == NULL || packet == NULL) {
        cascade_status_error(status, "risk validation received invalid arguments");
        return false;
    }

    const LanePolicy *policy = risk_policy_for_packet(state, packet);
    if (policy == NULL) {
        cascade_status_clear(status);
        return true;
    }

    if (policy->max_packet_amount > 0 && packet->amount > policy->max_packet_amount) {
        return reject_packet(state, packet, status, "lane amount limit exceeded");
    }
    if (policy->min_priority > 0 && packet->base_priority < policy->min_priority) {
        return reject_packet(state, packet, status, "lane priority floor not met");
    }
    if (!policy->allow_deferred && packet->attempts > 0) {
        return reject_packet(state, packet, status, "lane does not accept deferred attempts");
    }
    if (policy->require_fee_account && packet->fee > 0 && packet->fee_account[0] == '\0') {
        return reject_packet(state, packet, status, "lane requires fee account");
    }
    if (policy->max_fee_bps >= 0) {
        CascadeAmount fee_cap = risk_fee_cap_for_packet(policy, packet);
        if (fee_cap < 0) {
            cascade_status_error(status, "fee cap overflow for packet %s", packet->id);
            return false;
        }
        if (packet->fee > fee_cap) {
            return reject_packet(state, packet, status, "lane fee cap exceeded");
        }
    }

    model_add_event(state, "packet_policy_accepted packet=%s lane=%s", packet->id, policy->id);
    cascade_status_clear(status);
    return true;
}
