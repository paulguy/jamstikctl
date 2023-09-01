#ifndef _JSON_SCHEMA_H
#define _JSON_SCHEMA_H

#include <json-c/json.h>

typedef struct {
    char *CC;
    char *Desc;
    int Typ;
    int Lo;
    int Hi;
    int Step;
    int TT;
    char *Cat;
    int F;
} SchemaItem;

json_object *json_tokenize_whole_string(size_t len, const char *buffer);
SchemaItem *jamstik_parse_json_schema(unsigned int *count, size_t len, const char *buffer);

#endif
