#ifndef _JSON_SCHEMA_H
#define _JSON_SCHEMA_H

#include <json-c/json.h>

#define MIDI_CMD (0)
#define MIDI_SYSEX (0xF0)
#define MIDI_SYSEX_DUMMY_LEN (0x55)
#define MIDI_SYSEX_END (0xF7)
#define MIDI_SYSEX_VENDOR (1)
#define MIDI_SYSEX_VENDOR_LEN (3)
#define MIDI_SYSEX_BODY (MIDI_SYSEX_VENDOR + MIDI_SYSEX_VENDOR_LEN)
#define MIDI_SYSEX_HEAD (4)
#define MIDI_SYSEX_TAIL (2)
#define JS_VENDOR_0 (0x00)
#define JS_VENDOR_1 (0x02)
#define JS_VENDOR_2 (0x02)
#define JS_CMD MIDI_SYSEX_BODY

#define JS_CONFIG_NAME (JS_CMD + 1)
#define JS_CONFIG_NAME_LEN (8)
#define JS_CONFIG_TYPE (JS_CONFIG_NAME + JS_CONFIG_NAME_LEN)
#define JS_CONFIG_VALUE (JS_CONFIG_TYPE + 1)
#define JS_CONFIG_QUERY_LEN (JS_CONFIG_NAME + JS_CONFIG_NAME_LEN + MIDI_SYSEX_TAIL)
#define JS_CONFIG_QUERY (0x66)
#define JS_CONFIG_RETURN (0x61)
#define JS_CONFIG_DONE (0x67)

#define JS_SCHEMA_QUERY (0x44)
#define JS_SCHEMA_RETURN (0x45)
#define JS_SCHEMA_NAME JS_CONFIG_NAME
#define JS_SCHEMA_NAME_LEN JS_CONFIG_NAME_LEN
#define JS_SCHEMA_QUERY_LEN (JS_SCHEMA_NAME + JS_SCHEMA_NAME_LEN + MIDI_SYSEX_TAIL)
#define JS_SCHEMA_START (JS_SCHEMA_NAME + JS_SCHEMA_NAME_LEN)
#define JS_SCHEMA_EXCESS (JS_SCHEMA_START + MIDI_SYSEX_TAIL)

#define JS_NO (0)
#define JS_YES (1)

typedef enum {
    JsTypeInvalid = -1,
    JsTypeUInt7 = 0,
    JsTypeUInt8,
    JsTypeUInt32,
    JsTypeInt32,
    JsTypeASCII7,
    JsTypeASCII8,
    JsTypeInt16,
    JsTypeUInt16,
    JsTypeInt64,
    JsTypeUInt64,
    JsTypeMax
} JsType;

typedef struct {
    char *CC;
    char *Desc;
    JsType Typ;
    int Lo;
    int Hi;
    int Step;
    int TT;
    int Cat;
    unsigned int F;

    unsigned int validValue;
    union {
        int64_t sint;
        uint64_t uint;
        char *text;
    } val;
} JsConfig;

typedef struct {
    unsigned int config_count;
    JsConfig *config;

    unsigned int category_count;
    char **categories;
} JsInfo;

JsInfo *js_init();
void js_free(JsInfo *js);
int js_parse_json_schema(JsInfo *js, size_t size, unsigned char *buf);
JsConfig *js_decode_config_value(JsInfo *js, size_t size, const unsigned char *buf);
void js_config_print(JsInfo *js, JsConfig *config);
JsConfig *js_config_find(JsInfo *js, const unsigned char *name);
int js_config_get_type_is_valid(JsType type);
size_t js_config_get_type_size(JsType type);
int js_config_get_type_bits(JsType type);
int js_config_get_type_is_numeric(JsType type);
int js_config_get_type_is_signed(JsType type);
 
#endif
