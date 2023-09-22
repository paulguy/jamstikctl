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

#define MIDI_CMD_MASK (0xF0)
#define MIDI_CHANNEL_MASK (0x0F)

#define MIDI_CMD_NOTE_OFF (0x80)
#define MIDI_CMD_NOTE_ON (0x90)
/* applie to above */
#define MIDI_CMD_NOTE_VEL (2)
#define MIDI_CMD_POLYTOUCH (0xA0)
#define MIDI_CMD_POLYTOUCH_PRESSURE (2)
/* applies to above 3 */
#define MIDI_CMD_NOTE (1)

#define MIDI_CMD_CC (0xB0)
#define MIDI_CMD_CC_CONTROL (1)
#define MIDI_CMD_CC_VALUE (2)

#define MIDI_CMD_PROGCH (0xC0)
#define MIDI_CMD_PROGCH_PROG (1)

#define MIDI_CMD_CHANTOUCH (0xD0)
#define MIDI_CMD_CHANTOUCH_PRESSURE (1)

#define MIDI_CMD_PITCHBEND (0xE0)
#define MIDI_CMD_PITCHBEND_LOW (1)
#define MIDI_CMD_PITCHBEND_HIGH (2)
#define MIDI_CMD_PITCHBEND_OFFSET (8192)

#define MIDI_CMD_2_VAL(LOW, HIGH) ((LOW) | ((HIGH) << 7))

#define JS_CONFIG_NAME (JS_CMD + 1)
#define JS_CONFIG_NAME_LEN (8)
#define JS_CONFIG_TYPE (JS_CONFIG_NAME + JS_CONFIG_NAME_LEN)
#define JS_CONFIG_VALUE (JS_CONFIG_TYPE + 1)
#define JS_CONFIG_QUERY_LEN (JS_CONFIG_NAME + JS_CONFIG_NAME_LEN + MIDI_SYSEX_TAIL)
#define JS_CONFIG_QUERY (0x66)
#define JS_CONFIG_RETURN (0x61)
#define JS_CONFIG_SET (0x62)
#define JS_CONFIG_SET_RETURN (0x63)
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

#define JS_GET_NUM_VALUE(TYPE, VAR, CONFIG) \
    if (js_config_get_type_is_signed((CONFIG)->Typ)) { \
        (VAR) = (TYPE)((CONFIG)->val.sint); \
    } else { \
        (VAR) = (TYPE)((CONFIG)->val.uint); \
    }

#define JS_GET_TEXT_VALUE(TYPE, VAR, CONFIG) (VAR) = (TYPE)((CONFIG)->val.text);

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
