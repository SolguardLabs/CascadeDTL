#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void cascade_status_clear(CascadeStatus *status) {
    if (status == NULL) {
        return;
    }
    status->ok = true;
    status->message[0] = '\0';
}

void cascade_status_error(CascadeStatus *status, const char *fmt, ...) {
    if (status == NULL) {
        return;
    }
    status->ok = false;
    va_list args;
    va_start(args, fmt);
    vsnprintf(status->message, sizeof(status->message), fmt, args);
    va_end(args);
}

int cascade_streq(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    return strcmp(a, b) == 0;
}

int cascade_starts_with(const char *text, const char *prefix) {
    if (text == NULL || prefix == NULL) {
        return 0;
    }
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

void cascade_copy_id(char dst[CASCADE_ID_LEN], const char *src) {
    if (dst == NULL) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, CASCADE_ID_LEN, "%s", src);
}

char *cascade_strdup(const char *src) {
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src);
    char *out = (char *)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, src, len + 1);
    return out;
}

char *cascade_read_file(const char *path, size_t *len_out, CascadeStatus *status) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        cascade_status_error(status, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        cascade_status_error(status, "cannot seek %s", path);
        return NULL;
    }
    long raw_len = ftell(file);
    if (raw_len < 0) {
        fclose(file);
        cascade_status_error(status, "cannot determine size for %s", path);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        cascade_status_error(status, "cannot rewind %s", path);
        return NULL;
    }

    size_t len = (size_t)raw_len;
    char *data = (char *)malloc(len + 1);
    if (data == NULL) {
        fclose(file);
        cascade_status_error(status, "out of memory reading %s", path);
        return NULL;
    }

    size_t got = fread(data, 1, len, file);
    fclose(file);
    if (got != len) {
        free(data);
        cascade_status_error(status, "short read from %s", path);
        return NULL;
    }
    data[len] = '\0';
    if (len_out != NULL) {
        *len_out = len;
    }
    cascade_status_clear(status);
    return data;
}

bool cascade_amount_add(CascadeAmount a, CascadeAmount b, CascadeAmount *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    if (out != NULL) {
        *out = a + b;
    }
    return true;
}

bool cascade_amount_sub(CascadeAmount a, CascadeAmount b, CascadeAmount *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
        return false;
    }
    if (out != NULL) {
        *out = a - b;
    }
    return true;
}

bool cascade_amount_mul(CascadeAmount a, CascadeAmount b, CascadeAmount *out) {
    if (a == 0 || b == 0) {
        if (out != NULL) {
            *out = 0;
        }
        return true;
    }
    if (a > INT64_MAX / b || a < INT64_MIN / b) {
        return false;
    }
    if (out != NULL) {
        *out = a * b;
    }
    return true;
}

CascadeAmount cascade_amount_max(CascadeAmount a, CascadeAmount b) {
    return a > b ? a : b;
}

CascadeAmount cascade_amount_min(CascadeAmount a, CascadeAmount b) {
    return a < b ? a : b;
}

static bool text_buffer_reserve(TextBuffer *buf, size_t extra) {
    if (buf == NULL) {
        return false;
    }
    if (extra > SIZE_MAX - buf->len - 1) {
        return false;
    }
    size_t need = buf->len + extra + 1;
    if (need <= buf->cap) {
        return true;
    }
    size_t next = buf->cap == 0 ? 256 : buf->cap;
    while (next < need) {
        if (next > SIZE_MAX / 2) {
            next = need;
            break;
        }
        next *= 2;
    }
    char *data = (char *)realloc(buf->data, next);
    if (data == NULL) {
        return false;
    }
    buf->data = data;
    buf->cap = next;
    return true;
}

void text_buffer_init(TextBuffer *buf) {
    if (buf == NULL) {
        return;
    }
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void text_buffer_free(TextBuffer *buf) {
    if (buf == NULL) {
        return;
    }
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

bool text_buffer_append(TextBuffer *buf, const char *text) {
    if (text == NULL) {
        text = "";
    }
    size_t len = strlen(text);
    if (!text_buffer_reserve(buf, len)) {
        return false;
    }
    memcpy(buf->data + buf->len, text, len + 1);
    buf->len += len;
    return true;
}

bool text_buffer_append_char(TextBuffer *buf, char c) {
    if (!text_buffer_reserve(buf, 1)) {
        return false;
    }
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
    return true;
}

bool text_buffer_appendf(TextBuffer *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return false;
    }
    if (!text_buffer_reserve(buf, (size_t)needed)) {
        va_end(args);
        return false;
    }
    vsnprintf(buf->data + buf->len, buf->cap - buf->len, fmt, args);
    va_end(args);
    buf->len += (size_t)needed;
    return true;
}

bool text_buffer_append_json_string(TextBuffer *buf, const char *text) {
    if (!text_buffer_append_char(buf, '"')) {
        return false;
    }
    if (text == NULL) {
        text = "";
    }
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        char tmp[8];
        switch (*p) {
        case '"':
            if (!text_buffer_append(buf, "\\\"")) {
                return false;
            }
            break;
        case '\\':
            if (!text_buffer_append(buf, "\\\\")) {
                return false;
            }
            break;
        case '\b':
            if (!text_buffer_append(buf, "\\b")) {
                return false;
            }
            break;
        case '\f':
            if (!text_buffer_append(buf, "\\f")) {
                return false;
            }
            break;
        case '\n':
            if (!text_buffer_append(buf, "\\n")) {
                return false;
            }
            break;
        case '\r':
            if (!text_buffer_append(buf, "\\r")) {
                return false;
            }
            break;
        case '\t':
            if (!text_buffer_append(buf, "\\t")) {
                return false;
            }
            break;
        default:
            if (*p < 0x20) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", *p);
                if (!text_buffer_append(buf, tmp)) {
                    return false;
                }
            } else if (!text_buffer_append_char(buf, (char)*p)) {
                return false;
            }
            break;
        }
    }
    return text_buffer_append_char(buf, '"');
}

void event_log_init(EventLog *log) {
    if (log == NULL) {
        return;
    }
    log->items = NULL;
    log->len = 0;
    log->cap = 0;
}

void event_log_free(EventLog *log) {
    if (log == NULL) {
        return;
    }
    for (size_t i = 0; i < log->len; i++) {
        free(log->items[i]);
    }
    free(log->items);
    log->items = NULL;
    log->len = 0;
    log->cap = 0;
}

bool event_log_push(EventLog *log, const char *fmt, ...) {
    if (log == NULL) {
        return false;
    }
    if (log->len == log->cap) {
        size_t next = log->cap == 0 ? 32 : log->cap * 2;
        char **items = (char **)realloc(log->items, next * sizeof(char *));
        if (items == NULL) {
            return false;
        }
        log->items = items;
        log->cap = next;
    }

    char msg[CASCADE_EVENT_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    log->items[log->len] = cascade_strdup(msg);
    if (log->items[log->len] == NULL) {
        return false;
    }
    log->len++;
    return true;
}

uint64_t cascade_hash64(const char *text) {
    uint64_t hash = 1469598103934665603ULL;
    if (text == NULL) {
        return hash;
    }
    while (*text != '\0') {
        hash ^= (unsigned char)*text;
        hash *= 1099511628211ULL;
        text++;
    }
    return hash;
}

int cascade_compare_id(const void *a, const void *b) {
    const char *pa = *(const char *const *)a;
    const char *pb = *(const char *const *)b;
    if (pa == NULL && pb == NULL) {
        return 0;
    }
    if (pa == NULL) {
        return -1;
    }
    if (pb == NULL) {
        return 1;
    }
    return strcmp(pa, pb);
}

const char *cascade_basename(const char *path) {
    const char *last = path;
    if (path == NULL) {
        return "";
    }
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }
    return last;
}
