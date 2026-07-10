#include "json.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct JsonParser {
    const char *text;
    size_t pos;
    size_t len;
    CascadeStatus *status;
} JsonParser;

static JsonValue *json_new(JsonKind kind) {
    JsonValue *value = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (value == NULL) {
        return NULL;
    }
    value->kind = kind;
    return value;
}

static void parser_error(JsonParser *parser, const char *message) {
    if (parser->status != NULL && parser->status->ok) {
        cascade_status_error(parser->status, "json error at byte %zu: %s", parser->pos, message);
    }
}

static char parser_peek(JsonParser *parser) {
    if (parser->pos >= parser->len) {
        return '\0';
    }
    return parser->text[parser->pos];
}

static char parser_next(JsonParser *parser) {
    if (parser->pos >= parser->len) {
        return '\0';
    }
    return parser->text[parser->pos++];
}

static void parser_skip_ws(JsonParser *parser) {
    while (parser->pos < parser->len) {
        unsigned char c = (unsigned char)parser->text[parser->pos];
        if (!isspace(c)) {
            break;
        }
        parser->pos++;
    }
}

static bool parser_match(JsonParser *parser, const char *word) {
    size_t len = strlen(word);
    if (parser->pos + len > parser->len) {
        return false;
    }
    if (strncmp(parser->text + parser->pos, word, len) == 0) {
        parser->pos += len;
        return true;
    }
    return false;
}

static bool hex_value(char c, int *out) {
    if (c >= '0' && c <= '9') {
        *out = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        *out = c - 'a' + 10;
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        *out = c - 'A' + 10;
        return true;
    }
    return false;
}

static bool append_utf8(TextBuffer *buf, unsigned codepoint) {
    if (codepoint <= 0x7F) {
        return text_buffer_append_char(buf, (char)codepoint);
    }
    if (codepoint <= 0x7FF) {
        return text_buffer_append_char(buf, (char)(0xC0 | ((codepoint >> 6) & 0x1F))) &&
               text_buffer_append_char(buf, (char)(0x80 | (codepoint & 0x3F)));
    }
    return text_buffer_append_char(buf, (char)(0xE0 | ((codepoint >> 12) & 0x0F))) &&
           text_buffer_append_char(buf, (char)(0x80 | ((codepoint >> 6) & 0x3F))) &&
           text_buffer_append_char(buf, (char)(0x80 | (codepoint & 0x3F)));
}

static char *parse_string_raw(JsonParser *parser) {
    if (parser_next(parser) != '"') {
        parser_error(parser, "expected string");
        return NULL;
    }

    TextBuffer buf;
    text_buffer_init(&buf);
    while (parser->pos < parser->len) {
        char c = parser_next(parser);
        if (c == '"') {
            if (buf.data == NULL) {
                text_buffer_append(&buf, "");
            }
            return buf.data;
        }
        if ((unsigned char)c < 0x20) {
            parser_error(parser, "control character inside string");
            text_buffer_free(&buf);
            return NULL;
        }
        if (c != '\\') {
            if (!text_buffer_append_char(&buf, c)) {
                parser_error(parser, "out of memory parsing string");
                text_buffer_free(&buf);
                return NULL;
            }
            continue;
        }

        char esc = parser_next(parser);
        switch (esc) {
        case '"':
        case '\\':
        case '/':
            if (!text_buffer_append_char(&buf, esc)) {
                parser_error(parser, "out of memory parsing escape");
                text_buffer_free(&buf);
                return NULL;
            }
            break;
        case 'b':
            text_buffer_append_char(&buf, '\b');
            break;
        case 'f':
            text_buffer_append_char(&buf, '\f');
            break;
        case 'n':
            text_buffer_append_char(&buf, '\n');
            break;
        case 'r':
            text_buffer_append_char(&buf, '\r');
            break;
        case 't':
            text_buffer_append_char(&buf, '\t');
            break;
        case 'u': {
            unsigned codepoint = 0;
            for (int i = 0; i < 4; i++) {
                int v = 0;
                if (!hex_value(parser_next(parser), &v)) {
                    parser_error(parser, "invalid unicode escape");
                    text_buffer_free(&buf);
                    return NULL;
                }
                codepoint = (codepoint << 4) | (unsigned)v;
            }
            if (!append_utf8(&buf, codepoint)) {
                parser_error(parser, "out of memory parsing unicode");
                text_buffer_free(&buf);
                return NULL;
            }
            break;
        }
        default:
            parser_error(parser, "invalid escape");
            text_buffer_free(&buf);
            return NULL;
        }
    }

    parser_error(parser, "unterminated string");
    text_buffer_free(&buf);
    return NULL;
}

static JsonValue *parse_value(JsonParser *parser);

static bool array_push(JsonValue *array, JsonValue *item) {
    if (array->as.array.len == array->as.array.cap) {
        size_t next = array->as.array.cap == 0 ? 8 : array->as.array.cap * 2;
        JsonValue **items = (JsonValue **)realloc(array->as.array.items, next * sizeof(JsonValue *));
        if (items == NULL) {
            return false;
        }
        array->as.array.items = items;
        array->as.array.cap = next;
    }
    array->as.array.items[array->as.array.len++] = item;
    return true;
}

static bool object_push(JsonValue *object, char *key, JsonValue *value) {
    if (object->as.object.len == object->as.object.cap) {
        size_t next = object->as.object.cap == 0 ? 8 : object->as.object.cap * 2;
        JsonMember *items = (JsonMember *)realloc(object->as.object.items, next * sizeof(JsonMember));
        if (items == NULL) {
            return false;
        }
        object->as.object.items = items;
        object->as.object.cap = next;
    }
    object->as.object.items[object->as.object.len].key = key;
    object->as.object.items[object->as.object.len].value = value;
    object->as.object.len++;
    return true;
}

static JsonValue *parse_array(JsonParser *parser) {
    JsonValue *array = json_new(JSON_ARRAY);
    if (array == NULL) {
        parser_error(parser, "out of memory parsing array");
        return NULL;
    }
    parser_next(parser);
    parser_skip_ws(parser);
    if (parser_peek(parser) == ']') {
        parser_next(parser);
        return array;
    }

    while (parser->pos < parser->len) {
        JsonValue *item = parse_value(parser);
        if (item == NULL) {
            json_free(array);
            return NULL;
        }
        if (!array_push(array, item)) {
            json_free(item);
            json_free(array);
            parser_error(parser, "out of memory appending array item");
            return NULL;
        }
        parser_skip_ws(parser);
        char c = parser_next(parser);
        if (c == ']') {
            return array;
        }
        if (c != ',') {
            json_free(array);
            parser_error(parser, "expected comma or array end");
            return NULL;
        }
        parser_skip_ws(parser);
    }

    json_free(array);
    parser_error(parser, "unterminated array");
    return NULL;
}

static JsonValue *parse_object(JsonParser *parser) {
    JsonValue *object = json_new(JSON_OBJECT);
    if (object == NULL) {
        parser_error(parser, "out of memory parsing object");
        return NULL;
    }
    parser_next(parser);
    parser_skip_ws(parser);
    if (parser_peek(parser) == '}') {
        parser_next(parser);
        return object;
    }

    while (parser->pos < parser->len) {
        if (parser_peek(parser) != '"') {
            json_free(object);
            parser_error(parser, "expected object key");
            return NULL;
        }
        char *key = parse_string_raw(parser);
        if (key == NULL) {
            json_free(object);
            return NULL;
        }
        parser_skip_ws(parser);
        if (parser_next(parser) != ':') {
            free(key);
            json_free(object);
            parser_error(parser, "expected colon");
            return NULL;
        }
        parser_skip_ws(parser);
        JsonValue *value = parse_value(parser);
        if (value == NULL) {
            free(key);
            json_free(object);
            return NULL;
        }
        if (!object_push(object, key, value)) {
            free(key);
            json_free(value);
            json_free(object);
            parser_error(parser, "out of memory appending object member");
            return NULL;
        }
        parser_skip_ws(parser);
        char c = parser_next(parser);
        if (c == '}') {
            return object;
        }
        if (c != ',') {
            json_free(object);
            parser_error(parser, "expected comma or object end");
            return NULL;
        }
        parser_skip_ws(parser);
    }

    json_free(object);
    parser_error(parser, "unterminated object");
    return NULL;
}

static JsonValue *parse_number(JsonParser *parser) {
    size_t start = parser->pos;
    if (parser_peek(parser) == '-') {
        parser->pos++;
    }
    if (!isdigit((unsigned char)parser_peek(parser))) {
        parser_error(parser, "expected digit");
        return NULL;
    }
    while (isdigit((unsigned char)parser_peek(parser))) {
        parser->pos++;
    }
    if (parser_peek(parser) == '.') {
        parser_error(parser, "decimal amounts are not supported");
        return NULL;
    }
    if (parser_peek(parser) == 'e' || parser_peek(parser) == 'E') {
        parser_error(parser, "exponential amounts are not supported");
        return NULL;
    }

    size_t len = parser->pos - start;
    char tmp[64];
    if (len >= sizeof(tmp)) {
        parser_error(parser, "number is too long");
        return NULL;
    }
    memcpy(tmp, parser->text + start, len);
    tmp[len] = '\0';
    errno = 0;
    char *end = NULL;
    long long n = strtoll(tmp, &end, 10);
    if (errno != 0 || end == tmp || *end != '\0') {
        parser_error(parser, "invalid number");
        return NULL;
    }
    JsonValue *value = json_new(JSON_NUMBER);
    if (value == NULL) {
        parser_error(parser, "out of memory parsing number");
        return NULL;
    }
    value->as.number = (CascadeAmount)n;
    return value;
}

static JsonValue *parse_value(JsonParser *parser) {
    parser_skip_ws(parser);
    char c = parser_peek(parser);
    if (c == '{') {
        return parse_object(parser);
    }
    if (c == '[') {
        return parse_array(parser);
    }
    if (c == '"') {
        JsonValue *value = json_new(JSON_STRING);
        if (value == NULL) {
            parser_error(parser, "out of memory parsing string");
            return NULL;
        }
        value->as.string = parse_string_raw(parser);
        if (value->as.string == NULL) {
            json_free(value);
            return NULL;
        }
        return value;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        return parse_number(parser);
    }
    if (parser_match(parser, "true")) {
        JsonValue *value = json_new(JSON_BOOL);
        if (value == NULL) {
            parser_error(parser, "out of memory parsing boolean");
            return NULL;
        }
        value->as.boolean = true;
        return value;
    }
    if (parser_match(parser, "false")) {
        JsonValue *value = json_new(JSON_BOOL);
        if (value == NULL) {
            parser_error(parser, "out of memory parsing boolean");
            return NULL;
        }
        value->as.boolean = false;
        return value;
    }
    if (parser_match(parser, "null")) {
        return json_new(JSON_NULL);
    }
    parser_error(parser, "unexpected token");
    return NULL;
}

JsonValue *json_parse_text(const char *text, CascadeStatus *status) {
    cascade_status_clear(status);
    if (text == NULL) {
        cascade_status_error(status, "json input is null");
        return NULL;
    }
    JsonParser parser;
    parser.text = text;
    parser.pos = 0;
    parser.len = strlen(text);
    parser.status = status;

    JsonValue *root = parse_value(&parser);
    if (root == NULL) {
        return NULL;
    }
    parser_skip_ws(&parser);
    if (parser.pos != parser.len) {
        json_free(root);
        cascade_status_error(status, "json error at byte %zu: trailing characters", parser.pos);
        return NULL;
    }
    cascade_status_clear(status);
    return root;
}

JsonValue *json_parse_file(const char *path, CascadeStatus *status) {
    size_t len = 0;
    char *text = cascade_read_file(path, &len, status);
    (void)len;
    if (text == NULL) {
        return NULL;
    }
    JsonValue *root = json_parse_text(text, status);
    free(text);
    return root;
}

void json_free(JsonValue *value) {
    if (value == NULL) {
        return;
    }
    switch (value->kind) {
    case JSON_STRING:
        free(value->as.string);
        break;
    case JSON_ARRAY:
        for (size_t i = 0; i < value->as.array.len; i++) {
            json_free(value->as.array.items[i]);
        }
        free(value->as.array.items);
        break;
    case JSON_OBJECT:
        for (size_t i = 0; i < value->as.object.len; i++) {
            free(value->as.object.items[i].key);
            json_free(value->as.object.items[i].value);
        }
        free(value->as.object.items);
        break;
    default:
        break;
    }
    free(value);
}

const JsonValue *json_object_get(const JsonValue *object, const char *key) {
    if (object == NULL || object->kind != JSON_OBJECT || key == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < object->as.object.len; i++) {
        if (cascade_streq(object->as.object.items[i].key, key)) {
            return object->as.object.items[i].value;
        }
    }
    return NULL;
}

const JsonValue *json_array_get(const JsonValue *array, size_t index) {
    if (array == NULL || array->kind != JSON_ARRAY || index >= array->as.array.len) {
        return NULL;
    }
    return array->as.array.items[index];
}

size_t json_array_len(const JsonValue *array) {
    if (array == NULL || array->kind != JSON_ARRAY) {
        return 0;
    }
    return array->as.array.len;
}

const char *json_get_string(const JsonValue *object, const char *key, const char *fallback) {
    const JsonValue *value = json_object_get(object, key);
    if (value == NULL || value->kind != JSON_STRING) {
        return fallback;
    }
    return value->as.string;
}

CascadeAmount json_get_amount(const JsonValue *object, const char *key, CascadeAmount fallback) {
    const JsonValue *value = json_object_get(object, key);
    if (value == NULL || value->kind != JSON_NUMBER) {
        return fallback;
    }
    return value->as.number;
}

int json_get_int(const JsonValue *object, const char *key, int fallback) {
    CascadeAmount amount = json_get_amount(object, key, (CascadeAmount)fallback);
    if (amount > INT32_MAX || amount < INT32_MIN) {
        return fallback;
    }
    return (int)amount;
}

bool json_get_bool(const JsonValue *object, const char *key, bool fallback) {
    const JsonValue *value = json_object_get(object, key);
    if (value == NULL || value->kind != JSON_BOOL) {
        return fallback;
    }
    return value->as.boolean;
}

bool json_expect_object(const JsonValue *value, const char *name, CascadeStatus *status) {
    if (value == NULL || value->kind != JSON_OBJECT) {
        cascade_status_error(status, "%s must be an object", name);
        return false;
    }
    return true;
}

bool json_expect_array(const JsonValue *value, const char *name, CascadeStatus *status) {
    if (value == NULL || value->kind != JSON_ARRAY) {
        cascade_status_error(status, "%s must be an array", name);
        return false;
    }
    return true;
}

bool json_expect_string(const JsonValue *value, const char *name, CascadeStatus *status) {
    if (value == NULL || value->kind != JSON_STRING) {
        cascade_status_error(status, "%s must be a string", name);
        return false;
    }
    return true;
}

bool json_expect_number(const JsonValue *value, const char *name, CascadeStatus *status) {
    if (value == NULL || value->kind != JSON_NUMBER) {
        cascade_status_error(status, "%s must be a number", name);
        return false;
    }
    return true;
}
