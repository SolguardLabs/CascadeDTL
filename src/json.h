#ifndef CASCADE_JSON_H
#define CASCADE_JSON_H

#include "common.h"

typedef enum JsonKind {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonKind;

typedef struct JsonValue JsonValue;

typedef struct JsonMember {
    char *key;
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonKind kind;
    union {
        bool boolean;
        CascadeAmount number;
        char *string;
        struct {
            JsonValue **items;
            size_t len;
            size_t cap;
        } array;
        struct {
            JsonMember *items;
            size_t len;
            size_t cap;
        } object;
    } as;
};

JsonValue *json_parse_text(const char *text, CascadeStatus *status);
JsonValue *json_parse_file(const char *path, CascadeStatus *status);
void json_free(JsonValue *value);

const JsonValue *json_object_get(const JsonValue *object, const char *key);
const JsonValue *json_array_get(const JsonValue *array, size_t index);
size_t json_array_len(const JsonValue *array);

const char *json_get_string(const JsonValue *object, const char *key, const char *fallback);
CascadeAmount json_get_amount(const JsonValue *object, const char *key, CascadeAmount fallback);
int json_get_int(const JsonValue *object, const char *key, int fallback);
bool json_get_bool(const JsonValue *object, const char *key, bool fallback);

bool json_expect_object(const JsonValue *value, const char *name, CascadeStatus *status);
bool json_expect_array(const JsonValue *value, const char *name, CascadeStatus *status);
bool json_expect_string(const JsonValue *value, const char *name, CascadeStatus *status);
bool json_expect_number(const JsonValue *value, const char *name, CascadeStatus *status);

#endif
