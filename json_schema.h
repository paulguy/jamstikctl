#ifndef _JSON_SCHEMA_H
#define _JSON_SCHEMA_H

#include <json-c/json.h>

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
} JsValueType;

typedef struct {
    char *CC;
    char *Desc;
    JsValueType Typ;
    int Lo;
    int Hi;
    int Step;
    int TT;
    char *Cat;
    unsigned int F;
} SchemaItem;

json_object *json_tokenize_whole_string(size_t len, const unsigned char *buffer);
SchemaItem *jamstik_parse_json_schema(unsigned int *count, size_t len, const unsigned char *buffer);
void schema_print_item(SchemaItem *item);
size_t schema_get_type_size(JsValueType type);

#endif
