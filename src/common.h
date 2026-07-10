#ifndef CASCADE_COMMON_H
#define CASCADE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define CASCADE_ID_LEN 64
#define CASCADE_MSG_LEN 256
#define CASCADE_EVENT_LEN 320

typedef int64_t CascadeAmount;

typedef struct CascadeStatus {
    bool ok;
    char message[CASCADE_MSG_LEN];
} CascadeStatus;

typedef struct TextBuffer {
    char *data;
    size_t len;
    size_t cap;
} TextBuffer;

typedef struct EventLog {
    char **items;
    size_t len;
    size_t cap;
} EventLog;

void cascade_status_clear(CascadeStatus *status);
void cascade_status_error(CascadeStatus *status, const char *fmt, ...);

int cascade_streq(const char *a, const char *b);
int cascade_starts_with(const char *text, const char *prefix);
void cascade_copy_id(char dst[CASCADE_ID_LEN], const char *src);
char *cascade_strdup(const char *src);
char *cascade_read_file(const char *path, size_t *len_out, CascadeStatus *status);

bool cascade_amount_add(CascadeAmount a, CascadeAmount b, CascadeAmount *out);
bool cascade_amount_sub(CascadeAmount a, CascadeAmount b, CascadeAmount *out);
bool cascade_amount_mul(CascadeAmount a, CascadeAmount b, CascadeAmount *out);
CascadeAmount cascade_amount_max(CascadeAmount a, CascadeAmount b);
CascadeAmount cascade_amount_min(CascadeAmount a, CascadeAmount b);

void text_buffer_init(TextBuffer *buf);
void text_buffer_free(TextBuffer *buf);
bool text_buffer_append(TextBuffer *buf, const char *text);
bool text_buffer_append_char(TextBuffer *buf, char c);
bool text_buffer_appendf(TextBuffer *buf, const char *fmt, ...);
bool text_buffer_append_json_string(TextBuffer *buf, const char *text);

void event_log_init(EventLog *log);
void event_log_free(EventLog *log);
bool event_log_push(EventLog *log, const char *fmt, ...);

uint64_t cascade_hash64(const char *text);
int cascade_compare_id(const void *a, const void *b);
const char *cascade_basename(const char *path);

#endif
