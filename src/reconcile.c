#include "reconcile.h"

#include <string.h>

void reconcile_report_init(ReconcileReport *report) {
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
}

int reconcile_find_lane(const ReconcileReport *report, const char *id) {
    if (report == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < report->lane_count; i++) {
        if (cascade_streq(report->lanes[i].id, id)) {
            return i;
        }
    }
    return -1;
}

static LaneSnapshot *reconcile_lane(ReconcileReport *report, const char *id) {
    int existing = reconcile_find_lane(report, id);
    if (existing >= 0) {
        return &report->lanes[existing];
    }
    if (report->lane_count >= CASCADE_MAX_LANE_SNAPSHOTS) {
        return NULL;
    }
    LaneSnapshot *lane = &report->lanes[report->lane_count++];
    memset(lane, 0, sizeof(*lane));
    cascade_copy_id(lane->id, id != NULL && id[0] != '\0' ? id : "default");
    return lane;
}

static const char *packet_lane(const CascadeState *state, const Packet *packet) {
    int reserve_index = model_find_reserve(state, packet->reserve_id);
    if (reserve_index < 0) {
        return "unmapped";
    }
    return state->reserves[reserve_index].lane;
}

bool reconcile_build(const CascadeState *state, ReconcileReport *report) {
    if (state == NULL || report == NULL) {
        return false;
    }
    reconcile_report_init(report);

    for (int i = 0; i < state->lane_policy_count; i++) {
        if (reconcile_lane(report, state->lane_policies[i].id) == NULL) {
            return false;
        }
    }
    for (int i = 0; i < state->reserve_count; i++) {
        const Reserve *reserve = &state->reserves[i];
        LaneSnapshot *lane = reconcile_lane(report, reserve->lane);
        if (lane == NULL) {
            return false;
        }
        lane->reserve_count++;
        lane->available += reserve->available;
        lane->locked += reserve->locked;
        lane->settled += reserve->settled;
        lane->exposure += reserve->exposure;
        report->total_available += reserve->available;
        report->total_locked += reserve->locked;
        report->total_settled += reserve->settled;
        report->total_exposure += reserve->exposure;
    }
    for (int i = 0; i < state->packet_count; i++) {
        const Packet *packet = &state->packets[i];
        LaneSnapshot *lane = reconcile_lane(report, packet_lane(state, packet));
        if (lane == NULL) {
            return false;
        }
        lane->packet_count++;
        lane->gross_packets += packet->amount;
        lane->gross_fees += packet->fee;
        if (packet->status == PACKET_SETTLED) {
            lane->settled_packets++;
        } else if (packet->status == PACKET_FAILED || packet->status == PACKET_REVIEW) {
            lane->failed_packets++;
        } else if (packet->status == PACKET_DEFERRED || packet->status == PACKET_QUEUED) {
            lane->deferred_packets++;
        }
    }
    return true;
}
