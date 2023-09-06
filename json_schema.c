#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

#include "json_schema.h"
#include "midi.h"

typedef enum {
    ttCheckbox = 0,
    ttSpin,
    ttManualEntryDecimal,
    ttReadOnlyHex,
    ttReadOnlyDecimal
} schema_control_type;

#define SF_ENGINEERING (1)
#define SF_ADVANCED (2)
#define SF_CRITICAL (4)
#define SF_BETA (8)
#define SF_NEVERSHOW (16)
#define SF_WIFIONLY (32)
#define SF_BTONLY (64)
#define SF_MAX_FLAG SF_BTONLY

json_object *json_tokenize_whole_string(size_t len, const char *buffer) {
    json_tokener *tok = json_tokener_new();
    json_object *jobj = NULL;
    enum json_tokener_error jerr;
    size_t parsed = 0;

    do {
        jobj = json_tokener_parse_ex(tok, &(buffer[parsed]), len);
        jerr = json_tokener_get_error(tok);
        parsed = json_tokener_get_parse_end(tok);
    } while(jerr == json_tokener_continue);
    if(jerr != json_tokener_success) {
        fprintf(stderr, "Error: %s\n", json_tokener_error_desc(jerr));
        return(NULL);
    }

    if(parsed < len) {
        fprintf(stderr, "Extra chars %d  parsed %d\n", len - parsed, parsed);
    }

    json_tokener_free(tok);

    return(jobj);
}

SchemaItem *jamstik_parse_json_schema(unsigned int *count, size_t len, const char *buffer) {
    json_object *jobj;
    json_object *schema;
    json_object *json_item;
    struct json_object_iterator schema_iter, schema_iter_end;
    const char *item_name;
    json_object *item_value;
    SchemaItem *schema_item;
    json_bool json_got;
    unsigned int i;

    jobj = json_tokenize_whole_string(len, buffer);
    if(jobj == NULL) {
        fprintf(stderr, "Couldn't parse JSON string.\n");
        goto error;
    }

    json_got = json_object_object_get_ex(jobj, "Schema", &schema);
    if(!json_got) {
        fprintf(stderr, "Couldn't get schema.");
        goto error_put_json;
    }
    if(json_object_get_type(schema) != json_type_array) {
        fprintf(stderr, "Schema type isn't array.");
        goto error_put_json;
    }
    *count = json_object_array_length(schema);
    schema_item = malloc(sizeof(SchemaItem) * *count);
    if(schema_item == NULL) {
        fprintf(stderr, "Failed to allocate memory for schema list.\n");
        goto error_put_json;
    }
    for(i = 0; i < *count; i++) {
        json_item = json_object_array_get_idx(schema, i);
        if(json_object_get_type(json_item) != json_type_object) {
            fprintf(stderr, "Schema item type isn't object.");
            goto error_free_memory;
        }
        schema_iter = json_object_iter_begin(json_item);
        schema_iter_end = json_object_iter_end(json_item);
        schema_item[i].CC = NULL;
        schema_item[i].Desc = NULL;
        schema_item[i].Typ = -1;
        schema_item[i].Lo = 0;
        schema_item[i].Hi = 0;
        schema_item[i].Step = 0;
        schema_item[i].TT = -1;
        schema_item[i].Cat = NULL;
        schema_item[i].F = -1;
        while(!json_object_iter_equal(&schema_iter, &schema_iter_end)) {
            item_name = json_object_iter_peek_name(&schema_iter);
            item_value = json_object_iter_peek_value(&schema_iter);
            if(strcmp(item_name, "CC") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    fprintf(stderr, "Item CC is not string.");
                    goto error_free_memory;
                }
                schema_item[i].CC = midi_copy_string(json_object_get_string(item_value));
            } else if(strcmp(item_name, "Desc") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    fprintf(stderr, "Item Desc is not string.");
                    goto error_free_memory;
                }
                schema_item[i].Desc = midi_copy_string(json_object_get_string(item_value));
            } else if(strcmp(item_name, "Typ") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Typ is not int.");
                    goto error_free_memory;
                }
                schema_item[i].Typ = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Lo") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Lo is not int.");
                    goto error_free_memory;
                }
                schema_item[i].Lo = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Hi") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Hi is not int.");
                    goto error_free_memory;
                }
                schema_item[i].Hi = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Step") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Step is not int.");
                    goto error_free_memory;
                }
                schema_item[i].Step = json_object_get_int(item_value);
            } else if(strcmp(item_name, "TT") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item TT is not int.");
                    goto error_free_memory;
                }
                schema_item[i].TT = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Cat") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    fprintf(stderr, "Item Cat is not string.");
                    goto error_free_memory;
                }
                schema_item[i].Cat = midi_copy_string(json_object_get_string(item_value));
            } else if(strcmp(item_name, "F") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item F is not int.");
                    goto error_free_memory;
                }
                schema_item[i].F = json_object_get_int(item_value);
            } else {
                fprintf(stderr, "Unknown field %s type %s.", item_name, json_type_to_name(json_object_get_type(item_value)));
            }
            json_object_iter_next(&schema_iter);
        }
    }

    json_object_put(jobj);

    return(schema_item);

error_free_memory:
    free(schema_item);
error_put_json:
    json_object_put(jobj);
error:
    return(NULL);
}

const char *schema_type_to_name(int type) {
    switch(type) {
        case js_uint7:
            return("unsigned 7 bit");
        case js_uint8:
            return("unsigned 8 bit (2 byte packed)");
        case js_uint32:
            return("unsigned 32 bit (5 byte packed)");
        case js_int32:
            return("signed 32 bit (5 byte packed)");
        case js_ascii7:
            return("7 bit ASCII");
        case js_ascii8:
            return("8 bit ASCII (packed)");
        case js_int16:
            return("signed 16 bit (3 byte packed)");
        case js_uint16:
            return("unsigned 16 bit (3 byte packed)");
        case js_int64:
            return("signed 64 bit (9 byte packed ?)");
        case js_uint64:
            return("unsigned 64 bit (9 byte packed ?)");
    }

    return("unknown");
}

const char *schema_control_to_name(int control) {
    switch(control) {
        case ttCheckbox:
            return("Checkbox");
        case ttSpin:
            return("Spinner");
        case ttManualEntryDecimal:
            return("Decimal Entry");
        case ttReadOnlyHex:
            return("Hex Display");
        case ttReadOnlyDecimal:
            return("Decimal Display");
    }

    return("unknown");
}

const char *schema_flag_to_name(int flag) {
    switch(flag) {
        case 0:
            return("");
        case SF_ENGINEERING:
            return("Engineering");
        case SF_ADVANCED:
            return("Advanced");
        case SF_CRITICAL:
            return("Critical-Function");
        case SF_BETA:
            return("Beta-Feature");
        case SF_NEVERSHOW:
            return("Never-Show");
        case SF_WIFIONLY:
            return("Wifi-Devices-Only");
        case SF_BTONLY:
            return("Bluetooth-Devices--Only");
    }

    return("unknown");
}

void schema_print_item(SchemaItem *item) {
    unsigned int i;

    printf("Name: %s", item->CC);
    if(item->Desc != NULL && strcmp(item->CC, item->Desc) != 0) {
        printf("  Description: %s", item->Desc);
    }
    printf("  Type: %s", schema_type_to_name(item->Typ));
    if(item->Lo != item->Hi) {
        printf("  Low: %d  Hi: %d", item->Lo, item->Hi);
    }
    if(item->Step > 0) {
        printf("  Step: %d", item->Step);
    }
    printf("  Control: %s", schema_control_to_name(item->TT));
    printf("  Flags:");
    for(i = 1; i <= SF_MAX_FLAG; i <<= 1) {
        printf(" %s", schema_flag_to_name(item->F & i));
    }
    printf("\n");
}
