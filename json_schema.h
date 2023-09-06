#ifndef _JSON_SCHEMA_H
#define _JSON_SCHEMA_H

#include <json-c/json.h>

typedef enum {
    js_uint7 = 0,
    js_uint8,
    js_uint32,
    js_int32,
    js_ascii7,
    js_ascii8,
    js_int16,
    js_uint16,
    js_int64,
    js_uint64
} js_value_type;

typedef struct {
    char *CC;
    char *Desc;
    js_value_type Typ;
    int Lo;
    int Hi;
    int Step;
    int TT;
    char *Cat;
    unsigned int F;
} SchemaItem;

json_object *json_tokenize_whole_string(size_t len, const char *buffer);
SchemaItem *jamstik_parse_json_schema(unsigned int *count, size_t len, const char *buffer);
void schema_print_item(SchemaItem *item);

#endif
