#ifndef CASCADE_SETTLEMENT_H
#define CASCADE_SETTLEMENT_H

#include "ledger.h"
#include "scheduler.h"

bool settlement_run(CascadeState *state, CascadeStatus *status);
bool settlement_process_batch(CascadeState *state, Batch *batch, CascadeStatus *status);
bool settlement_process_packet(CascadeState *state, Batch *batch, Packet *packet, CascadeStatus *status);

#endif
