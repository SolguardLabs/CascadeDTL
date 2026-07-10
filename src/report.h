#ifndef CASCADE_REPORT_H
#define CASCADE_REPORT_H

#include "model.h"

bool report_write_json(const CascadeState *state, TextBuffer *out, bool include_events);
bool report_write_summary(const CascadeState *state, TextBuffer *out);

#endif
