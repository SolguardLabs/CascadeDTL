#include "model.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void packet_defaults(Packet *packet) {
    memset(packet, 0, sizeof(*packet));
    packet->base_priority = 10;
    packet->congestion_weight = 1;
    packet->max_attempts = 1;
    packet->retry_delay = 1;
    packet->legs = 1;
    packet->fail_attempt = 0;
    packet->fail_leg = 0;
    packet->fail_terminal = 0;
    packet->release_on_failure = 1;
    packet->hold_on_failure = 0;
    packet->receipt_index = -1;
    packet->status = PACKET_NEW;
}

void model_state_init(CascadeState *state) {
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    cascade_copy_id(state->scenario_id, "unnamed");
    state->congestion_base = 4;
    state->congestion_per_packet = 2;
    state->retry_boost = 12;
    event_log_init(&state->events);
}

void model_state_free(CascadeState *state) {
    if (state == NULL) {
        return;
    }
    event_log_free(&state->events);
}

const char *packet_status_name(PacketStatus status) {
    switch (status) {
    case PACKET_NEW:
        return "new";
    case PACKET_QUEUED:
        return "queued";
    case PACKET_DEFERRED:
        return "deferred";
    case PACKET_SETTLED:
        return "settled";
    case PACKET_FAILED:
        return "failed";
    case PACKET_REVIEW:
        return "review";
    default:
        return "unknown";
    }
}

PacketStatus packet_status_from_name(const char *name) {
    if (cascade_streq(name, "queued")) {
        return PACKET_QUEUED;
    }
    if (cascade_streq(name, "deferred")) {
        return PACKET_DEFERRED;
    }
    if (cascade_streq(name, "settled")) {
        return PACKET_SETTLED;
    }
    if (cascade_streq(name, "failed")) {
        return PACKET_FAILED;
    }
    if (cascade_streq(name, "review")) {
        return PACKET_REVIEW;
    }
    return PACKET_NEW;
}

int model_find_participant(const CascadeState *state, const char *id) {
    if (state == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < state->participant_count; i++) {
        if (cascade_streq(state->participants[i].id, id)) {
            return i;
        }
    }
    return -1;
}

int model_find_reserve(const CascadeState *state, const char *id) {
    if (state == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < state->reserve_count; i++) {
        if (cascade_streq(state->reserves[i].id, id)) {
            return i;
        }
    }
    return -1;
}

int model_find_packet(const CascadeState *state, const char *id) {
    if (state == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < state->packet_count; i++) {
        if (cascade_streq(state->packets[i].id, id)) {
            return i;
        }
    }
    return -1;
}

int model_find_receipt(const CascadeState *state, const char *id) {
    if (state == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < state->receipt_count; i++) {
        if (cascade_streq(state->receipts[i].id, id)) {
            return i;
        }
    }
    return -1;
}

int model_find_lane_policy(const CascadeState *state, const char *id) {
    if (state == NULL || id == NULL) {
        return -1;
    }
    for (int i = 0; i < state->lane_policy_count; i++) {
        if (cascade_streq(state->lane_policies[i].id, id)) {
            return i;
        }
    }
    return -1;
}

bool model_add_event(CascadeState *state, const char *fmt, ...) {
    if (state == NULL) {
        return false;
    }
    char msg[CASCADE_EVENT_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    return event_log_push(&state->events, "epoch=%d %s", state->current_epoch, msg);
}

CascadeAmount model_total_reserve_capacity(const CascadeState *state) {
    CascadeAmount total = 0;
    if (state == NULL) {
        return total;
    }
    for (int i = 0; i < state->reserve_count; i++) {
        total += state->reserves[i].available;
        total += state->reserves[i].locked;
        total += state->reserves[i].settled;
    }
    return total;
}

CascadeAmount model_total_participant_balance(const CascadeState *state) {
    CascadeAmount total = 0;
    if (state == NULL) {
        return total;
    }
    for (int i = 0; i < state->participant_count; i++) {
        total += state->participants[i].balance;
    }
    return total;
}

static bool parse_participant(CascadeState *state, const JsonValue *value, CascadeStatus *status) {
    if (!json_expect_object(value, "participant", status)) {
        return false;
    }
    if (state->participant_count >= CASCADE_MAX_PARTICIPANTS) {
        cascade_status_error(status, "too many participants");
        return false;
    }

    const char *id = json_get_string(value, "id", NULL);
    if (id == NULL || id[0] == '\0') {
        cascade_status_error(status, "participant id is required");
        return false;
    }
    if (model_find_participant(state, id) >= 0) {
        cascade_status_error(status, "duplicate participant %s", id);
        return false;
    }

    Participant *participant = &state->participants[state->participant_count++];
    memset(participant, 0, sizeof(*participant));
    cascade_copy_id(participant->id, id);
    participant->balance = json_get_amount(value, "balance", 0);
    participant->received = json_get_amount(value, "received", 0);
    participant->fees = json_get_amount(value, "fees", 0);
    participant->unsettled = json_get_amount(value, "unsettled", 0);
    return true;
}

static bool parse_reserve(CascadeState *state, const JsonValue *value, CascadeStatus *status) {
    if (!json_expect_object(value, "reserve", status)) {
        return false;
    }
    if (state->reserve_count >= CASCADE_MAX_RESERVES) {
        cascade_status_error(status, "too many reserves");
        return false;
    }

    const char *id = json_get_string(value, "id", NULL);
    const char *owner = json_get_string(value, "owner", NULL);
    if (id == NULL || id[0] == '\0') {
        cascade_status_error(status, "reserve id is required");
        return false;
    }
    if (owner == NULL || owner[0] == '\0') {
        cascade_status_error(status, "reserve owner is required");
        return false;
    }
    if (model_find_reserve(state, id) >= 0) {
        cascade_status_error(status, "duplicate reserve %s", id);
        return false;
    }
    if (model_find_participant(state, owner) < 0) {
        cascade_status_error(status, "reserve %s owner %s is not a participant", id, owner);
        return false;
    }

    Reserve *reserve = &state->reserves[state->reserve_count++];
    memset(reserve, 0, sizeof(*reserve));
    cascade_copy_id(reserve->id, id);
    cascade_copy_id(reserve->owner, owner);
    cascade_copy_id(reserve->lane, json_get_string(value, "lane", "default"));
    reserve->available = json_get_amount(value, "available", 0);
    reserve->locked = json_get_amount(value, "locked", 0);
    reserve->settled = json_get_amount(value, "settled", 0);
    reserve->exposure = json_get_amount(value, "exposure", 0);
    reserve->frozen = json_get_bool(value, "frozen", false) ? 1 : 0;
    reserve->generation = json_get_int(value, "generation", 0);
    if (reserve->available < 0 || reserve->locked < 0 || reserve->settled < 0) {
        cascade_status_error(status, "reserve %s has negative accounting fields", id);
        return false;
    }
    return true;
}

static bool parse_lane_policy(CascadeState *state, const JsonValue *value, CascadeStatus *status) {
    if (!json_expect_object(value, "lane policy", status)) {
        return false;
    }
    if (state->lane_policy_count >= CASCADE_MAX_LANE_POLICIES) {
        cascade_status_error(status, "too many lane policies");
        return false;
    }

    const char *id = json_get_string(value, "id", NULL);
    if (id == NULL || id[0] == '\0') {
        cascade_status_error(status, "lane policy id is required");
        return false;
    }
    if (model_find_lane_policy(state, id) >= 0) {
        cascade_status_error(status, "duplicate lane policy %s", id);
        return false;
    }

    LanePolicy *policy = &state->lane_policies[state->lane_policy_count++];
    memset(policy, 0, sizeof(*policy));
    cascade_copy_id(policy->id, id);
    policy->max_packet_amount = json_get_amount(value, "maxPacketAmount", 0);
    policy->max_fee_bps = json_get_int(value, "maxFeeBps", -1);
    policy->min_priority = json_get_int(value, "minPriority", 0);
    policy->allow_deferred = json_get_bool(value, "allowDeferred", true) ? 1 : 0;
    policy->require_fee_account = json_get_bool(value, "requireFeeAccount", false) ? 1 : 0;

    if (policy->max_packet_amount < 0) {
        cascade_status_error(status, "lane policy %s maxPacketAmount is invalid", id);
        return false;
    }
    if (policy->max_fee_bps < -1 || policy->max_fee_bps > 10000) {
        cascade_status_error(status, "lane policy %s maxFeeBps is invalid", id);
        return false;
    }
    if (policy->min_priority < 0) {
        cascade_status_error(status, "lane policy %s minPriority is invalid", id);
        return false;
    }
    return true;
}

static void parse_failure_plan(Packet *packet, const JsonValue *value) {
    if (value == NULL || value->kind != JSON_OBJECT) {
        return;
    }
    packet->fail_attempt = json_get_int(value, "attempt", 0);
    packet->fail_leg = json_get_int(value, "leg", 0);
    packet->fail_terminal = json_get_bool(value, "terminal", false) ? 1 : 0;
    packet->release_on_failure = json_get_bool(value, "releaseReserve", true) ? 1 : 0;
    packet->hold_on_failure = json_get_bool(value, "holdForReview", false) ? 1 : 0;
}

static bool parse_packet(CascadeState *state, const JsonValue *value, int sequence, CascadeStatus *status) {
    if (!json_expect_object(value, "packet", status)) {
        return false;
    }
    if (state->packet_count >= CASCADE_MAX_PACKETS) {
        cascade_status_error(status, "too many packets");
        return false;
    }

    Packet packet;
    packet_defaults(&packet);
    const char *id = json_get_string(value, "id", NULL);
    const char *reserve = json_get_string(value, "reserve", NULL);
    const char *beneficiary = json_get_string(value, "beneficiary", NULL);
    if (id == NULL || id[0] == '\0') {
        cascade_status_error(status, "packet id is required");
        return false;
    }
    if (reserve == NULL || reserve[0] == '\0') {
        cascade_status_error(status, "packet %s reserve is required", id);
        return false;
    }
    if (beneficiary == NULL || beneficiary[0] == '\0') {
        cascade_status_error(status, "packet %s beneficiary is required", id);
        return false;
    }
    if (model_find_packet(state, id) >= 0) {
        cascade_status_error(status, "duplicate packet %s", id);
        return false;
    }

    cascade_copy_id(packet.id, id);
    cascade_copy_id(packet.reserve_id, reserve);
    cascade_copy_id(packet.beneficiary, beneficiary);
    cascade_copy_id(packet.fee_account, json_get_string(value, "feeAccount", ""));
    cascade_copy_id(packet.memo, json_get_string(value, "memo", ""));
    packet.amount = json_get_amount(value, "amount", 0);
    packet.fee = json_get_amount(value, "fee", 0);
    packet.base_priority = json_get_int(value, "priority", 10);
    packet.congestion_weight = json_get_int(value, "congestionWeight", 1);
    packet.max_attempts = json_get_int(value, "maxAttempts", 1);
    packet.retry_delay = json_get_int(value, "retryDelay", 1);
    packet.legs = json_get_int(value, "legs", 1);
    packet.not_before_epoch = json_get_int(value, "notBefore", 0);
    packet.sequence = sequence;
    parse_failure_plan(&packet, json_object_get(value, "failure"));

    if (packet.amount <= 0) {
        cascade_status_error(status, "packet %s amount must be positive", id);
        return false;
    }
    if (packet.fee < 0 || packet.fee > packet.amount) {
        cascade_status_error(status, "packet %s fee must be between 0 and amount", id);
        return false;
    }
    if (packet.max_attempts < 1 || packet.max_attempts > 16) {
        cascade_status_error(status, "packet %s maxAttempts out of range", id);
        return false;
    }
    if (packet.retry_delay < 1 || packet.retry_delay > 64) {
        cascade_status_error(status, "packet %s retryDelay out of range", id);
        return false;
    }
    if (packet.legs < 1 || packet.legs > 32) {
        cascade_status_error(status, "packet %s legs out of range", id);
        return false;
    }
    if (packet.fail_attempt < 0 || packet.fail_leg < 0) {
        cascade_status_error(status, "packet %s failure plan is invalid", id);
        return false;
    }

    state->packets[state->packet_count++] = packet;
    return true;
}

static bool parse_batch(CascadeState *state, const JsonValue *value, CascadeStatus *status) {
    if (!json_expect_object(value, "batch", status)) {
        return false;
    }
    if (state->batch_count >= CASCADE_MAX_BATCHES) {
        cascade_status_error(status, "too many batches");
        return false;
    }
    const char *id = json_get_string(value, "id", NULL);
    if (id == NULL || id[0] == '\0') {
        cascade_status_error(status, "batch id is required");
        return false;
    }

    const JsonValue *packets = json_object_get(value, "packets");
    if (packets != NULL && !json_expect_array(packets, "batch packets", status)) {
        return false;
    }

    Batch *batch = &state->batches[state->batch_count++];
    memset(batch, 0, sizeof(*batch));
    cascade_copy_id(batch->id, id);
    batch->epoch = json_get_int(value, "epoch", state->batch_count);
    batch->packet_start = state->packet_count;
    batch->packet_count = 0;

    size_t packet_len = json_array_len(packets);
    for (size_t i = 0; i < packet_len; i++) {
        if (!parse_packet(state, json_array_get(packets, i), state->packet_count, status)) {
            return false;
        }
        batch->packet_count++;
    }
    return true;
}

bool model_load_json(CascadeState *state, const JsonValue *root, CascadeStatus *status) {
    if (!json_expect_object(root, "root", status)) {
        return false;
    }
    const char *scenario = json_get_string(root, "scenario", "unnamed");
    cascade_copy_id(state->scenario_id, scenario);

    const JsonValue *congestion = json_object_get(root, "congestion");
    if (congestion != NULL) {
        if (!json_expect_object(congestion, "congestion", status)) {
            return false;
        }
        state->congestion_base = json_get_int(congestion, "base", state->congestion_base);
        state->congestion_per_packet = json_get_int(congestion, "perQueuedPacket", state->congestion_per_packet);
        state->retry_boost = json_get_int(congestion, "retryBoost", state->retry_boost);
    }

    const JsonValue *participants = json_object_get(root, "participants");
    if (!json_expect_array(participants, "participants", status)) {
        return false;
    }
    for (size_t i = 0; i < json_array_len(participants); i++) {
        if (!parse_participant(state, json_array_get(participants, i), status)) {
            return false;
        }
    }

    const JsonValue *reserves = json_object_get(root, "reserves");
    if (!json_expect_array(reserves, "reserves", status)) {
        return false;
    }
    for (size_t i = 0; i < json_array_len(reserves); i++) {
        if (!parse_reserve(state, json_array_get(reserves, i), status)) {
            return false;
        }
    }

    const JsonValue *lane_policies = json_object_get(root, "lanePolicies");
    if (lane_policies != NULL) {
        if (!json_expect_array(lane_policies, "lanePolicies", status)) {
            return false;
        }
        for (size_t i = 0; i < json_array_len(lane_policies); i++) {
            if (!parse_lane_policy(state, json_array_get(lane_policies, i), status)) {
                return false;
            }
        }
    }

    const JsonValue *batches = json_object_get(root, "batches");
    if (!json_expect_array(batches, "batches", status)) {
        return false;
    }
    for (size_t i = 0; i < json_array_len(batches); i++) {
        if (!parse_batch(state, json_array_get(batches, i), status)) {
            return false;
        }
    }

    return model_validate(state, status);
}

bool model_load_file(CascadeState *state, const char *path, CascadeStatus *status) {
    JsonValue *root = json_parse_file(path, status);
    if (root == NULL) {
        return false;
    }
    bool ok = model_load_json(state, root, status);
    json_free(root);
    if (ok) {
        model_add_event(state, "loaded fixture=%s participants=%d reserves=%d batches=%d",
                        cascade_basename(path),
                        state->participant_count,
                        state->reserve_count,
                        state->batch_count);
    }
    return ok;
}

static bool validate_unique_ids(const CascadeState *state, CascadeStatus *status) {
    for (int i = 0; i < state->participant_count; i++) {
        for (int j = i + 1; j < state->participant_count; j++) {
            if (cascade_streq(state->participants[i].id, state->participants[j].id)) {
                cascade_status_error(status, "duplicate participant %s", state->participants[i].id);
                return false;
            }
        }
    }
    for (int i = 0; i < state->reserve_count; i++) {
        for (int j = i + 1; j < state->reserve_count; j++) {
            if (cascade_streq(state->reserves[i].id, state->reserves[j].id)) {
                cascade_status_error(status, "duplicate reserve %s", state->reserves[i].id);
                return false;
            }
        }
    }
    for (int i = 0; i < state->packet_count; i++) {
        for (int j = i + 1; j < state->packet_count; j++) {
            if (cascade_streq(state->packets[i].id, state->packets[j].id)) {
                cascade_status_error(status, "duplicate packet %s", state->packets[i].id);
                return false;
            }
        }
    }
    return true;
}

bool model_validate(const CascadeState *state, CascadeStatus *status) {
    if (state == NULL) {
        cascade_status_error(status, "state is null");
        return false;
    }
    if (state->participant_count <= 0) {
        cascade_status_error(status, "at least one participant is required");
        return false;
    }
    if (state->reserve_count <= 0) {
        cascade_status_error(status, "at least one reserve is required");
        return false;
    }
    if (!validate_unique_ids(state, status)) {
        return false;
    }
    for (int i = 0; i < state->reserve_count; i++) {
        const Reserve *reserve = &state->reserves[i];
        if (model_find_participant(state, reserve->owner) < 0) {
            cascade_status_error(status, "reserve %s owner does not exist", reserve->id);
            return false;
        }
        if (reserve->available < 0 || reserve->locked < 0 || reserve->settled < 0) {
            cascade_status_error(status, "reserve %s has negative accounting", reserve->id);
            return false;
        }
    }
    for (int i = 0; i < state->packet_count; i++) {
        const Packet *packet = &state->packets[i];
        if (model_find_reserve(state, packet->reserve_id) < 0) {
            cascade_status_error(status, "packet %s references missing reserve %s", packet->id, packet->reserve_id);
            return false;
        }
        if (model_find_participant(state, packet->beneficiary) < 0) {
            cascade_status_error(status, "packet %s references missing beneficiary %s",
                                 packet->id,
                                 packet->beneficiary);
            return false;
        }
        if (packet->fee_account[0] != '\0' && model_find_participant(state, packet->fee_account) < 0) {
            cascade_status_error(status, "packet %s references missing fee account %s",
                                 packet->id,
                                 packet->fee_account);
            return false;
        }
    }
    for (int i = 0; i < state->lane_policy_count; i++) {
        const LanePolicy *policy = &state->lane_policies[i];
        if (policy->id[0] == '\0') {
            cascade_status_error(status, "lane policy id is empty");
            return false;
        }
        if (policy->max_packet_amount < 0 || policy->max_fee_bps < -1 ||
            policy->max_fee_bps > 10000 || policy->min_priority < 0) {
            cascade_status_error(status, "lane policy %s has invalid limits", policy->id);
            return false;
        }
    }
    cascade_status_clear(status);
    return true;
}
