#ifndef CASCADE_RISK_H
#define CASCADE_RISK_H

#include "model.h"

const LanePolicy *risk_policy_for_packet(const CascadeState *state, const Packet *packet);
bool risk_validate_packet(CascadeState *state, const Batch *batch, Packet *packet, CascadeStatus *status);
CascadeAmount risk_fee_cap_for_packet(const LanePolicy *policy, const Packet *packet);

#endif
