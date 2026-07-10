#ifndef CASCADE_MODEL_H
#define CASCADE_MODEL_H

#include "common.h"
#include "json.h"

#define CASCADE_MAX_PARTICIPANTS 96
#define CASCADE_MAX_RESERVES 128
#define CASCADE_MAX_PACKETS 384
#define CASCADE_MAX_BATCHES 96
#define CASCADE_MAX_RECEIPTS 768
#define CASCADE_MAX_LANE_POLICIES 32

typedef enum PacketStatus {
    PACKET_NEW,
    PACKET_QUEUED,
    PACKET_DEFERRED,
    PACKET_SETTLED,
    PACKET_FAILED,
    PACKET_REVIEW
} PacketStatus;

typedef struct Participant {
    char id[CASCADE_ID_LEN];
    CascadeAmount balance;
    CascadeAmount received;
    CascadeAmount fees;
    CascadeAmount unsettled;
} Participant;

typedef struct Reserve {
    char id[CASCADE_ID_LEN];
    char owner[CASCADE_ID_LEN];
    char lane[CASCADE_ID_LEN];
    CascadeAmount available;
    CascadeAmount locked;
    CascadeAmount settled;
    CascadeAmount exposure;
    int frozen;
    int generation;
} Reserve;

typedef struct Receipt {
    char id[CASCADE_ID_LEN];
    char packet_id[CASCADE_ID_LEN];
    char reserve_id[CASCADE_ID_LEN];
    CascadeAmount amount;
    int attempt;
    int opened_epoch;
    int reserve_generation;
    int valid;
    int consumed;
    int retryable;
    int reused_count;
} Receipt;

typedef struct Packet {
    char id[CASCADE_ID_LEN];
    char reserve_id[CASCADE_ID_LEN];
    char beneficiary[CASCADE_ID_LEN];
    char fee_account[CASCADE_ID_LEN];
    char memo[CASCADE_ID_LEN];
    CascadeAmount amount;
    CascadeAmount fee;
    int base_priority;
    int congestion_weight;
    int max_attempts;
    int retry_delay;
    int legs;
    int fail_attempt;
    int fail_leg;
    int fail_terminal;
    int release_on_failure;
    int hold_on_failure;
    int attempts;
    int not_before_epoch;
    int sequence;
    int receipt_index;
    int last_score;
    PacketStatus status;
    char last_error[CASCADE_MSG_LEN];
} Packet;

typedef struct Batch {
    char id[CASCADE_ID_LEN];
    int epoch;
    int packet_start;
    int packet_count;
} Batch;

typedef struct LanePolicy {
    char id[CASCADE_ID_LEN];
    CascadeAmount max_packet_amount;
    int max_fee_bps;
    int min_priority;
    int allow_deferred;
    int require_fee_account;
} LanePolicy;

typedef struct CascadeMetrics {
    int batches_processed;
    int packets_settled;
    int packets_failed;
    int packets_deferred;
    int policy_rejections;
    int receipt_count;
    int receipt_replays;
    int reserve_releases;
    CascadeAmount gross_settled;
    CascadeAmount gross_fees;
    CascadeAmount gross_exposure;
} CascadeMetrics;

typedef struct CascadeState {
    char scenario_id[CASCADE_ID_LEN];
    int congestion_base;
    int congestion_per_packet;
    int retry_boost;
    int current_epoch;
    int strict_accounting;

    Participant participants[CASCADE_MAX_PARTICIPANTS];
    int participant_count;

    Reserve reserves[CASCADE_MAX_RESERVES];
    int reserve_count;

    Packet packets[CASCADE_MAX_PACKETS];
    int packet_count;

    Batch batches[CASCADE_MAX_BATCHES];
    int batch_count;

    Receipt receipts[CASCADE_MAX_RECEIPTS];
    int receipt_count;

    LanePolicy lane_policies[CASCADE_MAX_LANE_POLICIES];
    int lane_policy_count;

    CascadeMetrics metrics;
    EventLog events;
} CascadeState;

void model_state_init(CascadeState *state);
void model_state_free(CascadeState *state);

bool model_load_file(CascadeState *state, const char *path, CascadeStatus *status);
bool model_load_json(CascadeState *state, const JsonValue *root, CascadeStatus *status);
bool model_validate(const CascadeState *state, CascadeStatus *status);

const char *packet_status_name(PacketStatus status);
PacketStatus packet_status_from_name(const char *name);

int model_find_participant(const CascadeState *state, const char *id);
int model_find_reserve(const CascadeState *state, const char *id);
int model_find_packet(const CascadeState *state, const char *id);
int model_find_receipt(const CascadeState *state, const char *id);
int model_find_lane_policy(const CascadeState *state, const char *id);

bool model_add_event(CascadeState *state, const char *fmt, ...);
CascadeAmount model_total_reserve_capacity(const CascadeState *state);
CascadeAmount model_total_participant_balance(const CascadeState *state);

#endif
