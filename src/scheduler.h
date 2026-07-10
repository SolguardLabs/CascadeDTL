#ifndef CASCADE_SCHEDULER_H
#define CASCADE_SCHEDULER_H

#include "model.h"

void scheduler_enqueue_batch(CascadeState *state, Batch *batch);
int scheduler_select_next(CascadeState *state);
int scheduler_ready_count(const CascadeState *state);
int scheduler_score_packet(const CascadeState *state, const Packet *packet, int ready_count);
bool scheduler_has_pending(const CascadeState *state);

#endif
