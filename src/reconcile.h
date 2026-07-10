#ifndef CASCADE_RECONCILE_H
#define CASCADE_RECONCILE_H

#include "model.h"

#define CASCADE_MAX_LANE_SNAPSHOTS 64

typedef struct LaneSnapshot {
    char id[CASCADE_ID_LEN];
    int reserve_count;
    int packet_count;
    int settled_packets;
    int failed_packets;
    int deferred_packets;
    CascadeAmount available;
    CascadeAmount locked;
    CascadeAmount settled;
    CascadeAmount exposure;
    CascadeAmount gross_packets;
    CascadeAmount gross_fees;
} LaneSnapshot;

typedef struct ReconcileReport {
    LaneSnapshot lanes[CASCADE_MAX_LANE_SNAPSHOTS];
    int lane_count;
    CascadeAmount total_available;
    CascadeAmount total_locked;
    CascadeAmount total_settled;
    CascadeAmount total_exposure;
} ReconcileReport;

void reconcile_report_init(ReconcileReport *report);
bool reconcile_build(const CascadeState *state, ReconcileReport *report);
int reconcile_find_lane(const ReconcileReport *report, const char *id);

#endif
