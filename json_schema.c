#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

#include "json_schema.h"
#include "midi.h"
#include "packed_values.h"

typedef enum {
    ttCheckbox = 0,
    ttSpin,
    ttManualEntryDecimal,
    ttReadOnlyHex,
    ttReadOnlyDecimal
} JsControlType;

#define SF_ENGINEERING (1)
#define SF_ADVANCED (2)
#define SF_CRITICAL (4)
#define SF_BETA (8)
#define SF_NEVERSHOW (16)
#define SF_WIFIONLY (32)
#define SF_BTONLY (64)
#define SF_MAX_FLAG SF_BTONLY

const size_t JS_VALUE_SIZES[] = {
    1, 2, 5, 5, 0, 0, 3, 3, 10, 10
};

const char *js_config_type_to_name(JsType type) {
    switch(type) {
        case JsTypeUInt7:
            return("unsigned 7 bit");
        case JsTypeUInt8:
            return("unsigned 8 bit (2 byte packed)");
        case JsTypeUInt32:
            return("unsigned 32 bit (5 byte packed)");
        case JsTypeInt32:
            return("signed 32 bit (5 byte packed)");
        case JsTypeASCII7:
            return("7 bit ASCII");
        case JsTypeASCII8:
            return("8 bit ASCII (packed)");
        case JsTypeInt16:
            return("signed 16 bit (3 byte packed)");
        case JsTypeUInt16:
            return("unsigned 16 bit (3 byte packed)");
        case JsTypeInt64:
            return("signed 64 bit (9 byte packed ?)");
        case JsTypeUInt64:
            return("unsigned 64 bit (9 byte packed ?)");
        default:
            break;
    }

    return("unknown");
}

const char *js_config_type_to_short_name(JsType type) {
    switch(type) {
        case JsTypeUInt7:
            return("uint7");
        case JsTypeUInt8:
            return("uint8");
        case JsTypeUInt32:
            return("uint32");
        case JsTypeInt32:
            return("int32");
        case JsTypeASCII7:
            return("ascii7");
        case JsTypeASCII8:
            return("ascii8");
        case JsTypeInt16:
            return("int16");
        case JsTypeUInt16:
            return("uint16");
        case JsTypeInt64:
            return("int64");
        case JsTypeUInt64:
            return("uint64");
        default:
            break;
    }

    return("unknown");
}

const char *js_config_control_to_name(JsControlType control) {
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

const char *js_config_flag_to_name(int flag) {
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

int js_config_get_type_is_valid(JsType type) {
    if(type < 0 || type >= JsTypeMax) {
        return(0);
    }

    return(1);
}

size_t js_config_get_type_size(JsType type) {
    if(!js_config_get_type_is_valid(type)) {
        return(-1);
    }

    return(JS_VALUE_SIZES[type]);
}

int js_config_get_type_bits(JsType type) {
    switch(type) {
        case JsTypeUInt7:
            return(7);
        case JsTypeUInt8:
            return(8);
        case JsTypeUInt32:
        case JsTypeInt32:
            return(32);
        case JsTypeASCII7:
            return(0);
        case JsTypeASCII8:
            return(0);
        case JsTypeInt16:
        case JsTypeUInt16:
            return(16);
        case JsTypeInt64:
        case JsTypeUInt64:
            return(64);
        default:
            break;
    }

    return(-1);
}

int js_config_get_type_is_numeric(JsType type) {
    switch(type) {
        case JsTypeUInt7:
        case JsTypeUInt8:
        case JsTypeUInt16:
        case JsTypeUInt32:
        case JsTypeUInt64:
        case JsTypeInt16:
        case JsTypeInt32:
        case JsTypeInt64:
            return(1);
        case JsTypeASCII7:
        case JsTypeASCII8:
            return(0);
        default:
            break;
    }

    return(-1);
}

int js_config_get_type_is_signed(JsType type) {
    switch(type) {
        case JsTypeUInt7:
        case JsTypeUInt8:
        case JsTypeUInt16:
        case JsTypeUInt32:
        case JsTypeUInt64:
            return(0);
        case JsTypeInt16:
        case JsTypeInt32:
        case JsTypeInt64:
            return(1);
        default:
            break;
    }

    return(-1);
}

json_object *json_tokenize_whole_string(size_t len, const unsigned char *buf) {
    json_tokener *tok = json_tokener_new();
    json_object *jobj = NULL;
    enum json_tokener_error jerr;
    size_t parsed = 0;

    do {
        jobj = json_tokener_parse_ex(tok, (char *)&(buf[parsed]), len);
        jerr = json_tokener_get_error(tok);
        parsed = json_tokener_get_parse_end(tok);
    } while(jerr == json_tokener_continue);
    if(jerr != json_tokener_success) {
        fprintf(stderr, "Error: %s\n", json_tokener_error_desc(jerr));
        return(NULL);
    }

    if(parsed < len) {
        fprintf(stderr, "Extra chars %lu  parsed %lu\n", len - parsed, parsed);
    }

    json_tokener_free(tok);

    return(jobj);
}

void default_config(JsConfig *config) {
    config->CC = NULL;
    config->Desc = NULL;
    config->Typ = -1;
    config->Lo = 0;
    config->Hi = 0;
    config->Step = 0;
    config->TT = -1;
    config->Cat = -1;
    config->F = -1;
    config->validValue = 0;
}

int find_category(JsInfo *js, const char *name) {
    unsigned int i;
    char **categories;

    if(strlen(name) != JS_SCHEMA_NAME_LEN) {
        fprintf(stderr, "Get category in schema that's the wrong length, should be JSON_SCHEMA_NAME_LEN!\n");
        return(-1);
    }

    for(i = 0; i < js->category_count; i++) {
        if(strncmp(js->categories[i], name, JS_SCHEMA_NAME_LEN+1) == 0) {
            return(i);
        }
    }

    categories = realloc(js->categories, sizeof(char **) * (js->category_count + 1));
    if(categories == NULL) {
        fprintf(stderr, "Failed to allocate memory!\n");
        return(-1);
    }
    js->categories = categories;

    js->categories[js->category_count] = midi_copy_string(name);
    js->category_count++;

    return(js->category_count-1);
}

JsInfo *js_init() {
    JsInfo *js;

    js = malloc(sizeof(JsInfo));
    if(js == NULL) {
        fprintf(stderr, "Failed to allocate memory!\n");
        return(NULL);
    }

    js->config_count = 0;
    js->config = NULL;
    js->category_count = 0;
    js->categories = NULL;

    return(js);
}

int js_parse_json_schema(JsInfo *js, size_t size, unsigned char *buf) {
    json_object *jobj;
    json_object *schema;
    json_object *json_item;
    struct json_object_iterator schema_iter, schema_iter_end;
    const char *item_name;
    json_object *item_value;
    json_bool json_got;
    const char *json_string;
    int category;
    unsigned int i;

    /* make it safe to pass to json-c */
    buf[size - MIDI_SYSEX_TAIL] = '\0';
    buf = &(buf[JS_SCHEMA_START]);

    jobj = json_tokenize_whole_string(size - JS_SCHEMA_START - MIDI_SYSEX_TAIL, buf);
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
    js->config_count = json_object_array_length(schema);
    js->config = malloc(sizeof(JsConfig) * js->config_count);
    if(js->config == NULL) {
        fprintf(stderr, "Failed to allocate memory for schema list.\n");
        goto error_put_json;
    }
    for(i = 0; i < js->config_count; i++) {
        default_config(&(js->config[i]));

        json_item = json_object_array_get_idx(schema, i);
        if(json_object_get_type(json_item) != json_type_object) {
            fprintf(stderr, "Schema item type isn't object.");
            goto error_free_memory;
        }
        schema_iter = json_object_iter_begin(json_item);
        schema_iter_end = json_object_iter_end(json_item);
        while(!json_object_iter_equal(&schema_iter, &schema_iter_end)) {
            item_name = json_object_iter_peek_name(&schema_iter);
            item_value = json_object_iter_peek_value(&schema_iter);
            if(strcmp(item_name, "CC") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    fprintf(stderr, "Item CC is not string.");
                    goto error_free_memory;
                }
                json_string = json_object_get_string(item_value);
                if(strlen(json_string) != JS_SCHEMA_NAME_LEN) {
                    fprintf(stderr, "Get name in schema that's the wrong length, should be JSON_SCHEMA_NAME_LEN!\n");
                    goto error_free_memory;
                }
                js->config[i].CC = midi_copy_string(json_string);
            } else if(strcmp(item_name, "Desc") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    fprintf(stderr, "Item Desc is not string.");
                    goto error_free_memory;
                }
                js->config[i].Desc = midi_copy_string(json_object_get_string(item_value));
            } else if(strcmp(item_name, "Typ") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Typ is not int.");
                    goto error_free_memory;
                }
                js->config[i].Typ = json_object_get_int(item_value);
                if(!js_config_get_type_is_valid(js->config[i].Typ)) {
                    fprintf(stderr, "Got unknown/invalid type %i!", js->config[i].Typ);
                    goto error_free_memory;
                }
            } else if(strcmp(item_name, "Lo") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Lo is not int.");
                    goto error_free_memory;
                }
                js->config[i].Lo = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Hi") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Hi is not int.");
                    goto error_free_memory;
                }
                js->config[i].Hi = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Step") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item Step is not int.");
                    goto error_free_memory;
                }
                js->config[i].Step = json_object_get_int(item_value);
            } else if(strcmp(item_name, "TT") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item TT is not int.");
                    goto error_free_memory;
                }
                js->config[i].TT = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Cat") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    fprintf(stderr, "Item Cat is not string.");
                    goto error_free_memory;
                }
                json_string = json_object_get_string(item_value);
                category = find_category(js, json_string);
                if(category < 0) {
                    goto error_free_memory;
                }
                js->config[i].Cat = category;
            } else if(strcmp(item_name, "F") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    fprintf(stderr, "Item F is not int.");
                    goto error_free_memory;
                }
                js->config[i].F = json_object_get_int(item_value);
            } else {
                fprintf(stderr, "Unknown field %s type %s.", item_name, json_type_to_name(json_object_get_type(item_value)));
            }
            json_object_iter_next(&schema_iter);
        }
    }

    json_object_put(jobj);

    return(0);

error_free_memory:
    free(js->config);
error_put_json:
    json_object_put(jobj);
error:
    return(-1);
}

void js_free(JsInfo *js) {
    unsigned int i;

    if(js->categories != NULL) {
        for(i = 0; i < js->category_count; i++) {
            free(js->categories[i]);
        }
        free(js->categories);
    }

    if(js->config != NULL) {
        for(i = 0; i < js->config_count; i++) {
            free(js->config[i].CC);
            free(js->config[i].Desc);
        }
        free(js->config);
    }

    free(js);
}

void js_config_print(JsInfo *js, JsConfig *config) {
    unsigned int i;

    printf("Category: ");
    if(config->Cat < 0 || (unsigned int)config->Cat > js->category_count) {
        printf("(uncategorized)");
    } else {
        printf("%s", js->categories[config->Cat]);
    }

    printf("  Name: %s", config->CC);
    if(config->Desc != NULL && strcmp(config->CC, config->Desc) != 0) {
        printf("  Description: %s", config->Desc);
    }

    printf("  Type: %s", js_config_type_to_name(config->Typ));
    if(config->Lo != config->Hi) {
        printf("  Low: %d  Hi: %d", config->Lo, config->Hi);
    }
    if(config->Step > 0) {
        printf("  Step: %d", config->Step);
    }
    printf("  Control: %s", js_config_control_to_name(config->TT));
    printf("  Flags:");
    for(i = 1; i <= SF_MAX_FLAG; i <<= 1) {
        printf(" %s", js_config_flag_to_name(config->F & i));
    }
    printf("\n");

    printf(" Value: ");
    if(!config->validValue) {
        printf("Not set!\n");
    } else {
        if(js_config_get_type_is_numeric(config->Typ)) {
            if(js_config_get_type_is_signed(config->Typ)) {
                printf("%ld\n", config->val.sint);
            } else {
                printf("%lu\n", config->val.uint);
            }
        } else {
            printf("\"%s\"\n", config->val.text);
        }
    }
}

JsConfig *js_config_find(JsInfo *js, const unsigned char *name) {
    unsigned int i;

    for(i = 0; i < js->config_count; i++) {
        if(memcmp(js->config[i].CC, name, JS_CONFIG_NAME_LEN) == 0) {
            return(&(js->config[i]));
        }
    }

    return(NULL);
}

JsConfig *js_decode_config_value(JsInfo *js, size_t size, const unsigned char *buf) {
    if((size < JS_CONFIG_TYPE + 1u + MIDI_SYSEX_TAIL)) {
        fprintf(stderr, "Config packet too short for necessary fields! (%lu)\n", size);
        return(NULL);
    } else {
        int type_size = js_config_get_type_size(buf[JS_CONFIG_TYPE]);
        if(type_size < 0) {
            fprintf(stderr, "Invalid config type %hhu!\n", buf[JS_CONFIG_TYPE]);
            return(NULL);
        }
        if(size < JS_CONFIG_VALUE + (unsigned int)type_size + MIDI_SYSEX_TAIL) {
            fprintf(stderr, "Config packet too short! (%lu)\n", size);
            return(NULL);
        }
    }

    if(js->config_count == 0) {
        fprintf(stderr, "WARNING: Ignored a too-early ");
        fwrite(&(buf[JS_CONFIG_NAME]), JS_CONFIG_NAME_LEN, 1, stderr);
        fprintf(stderr, " report!\n");
        return(NULL);
    }

    JsConfig *config = js_config_find(js, &(buf[JS_CONFIG_NAME]));
    if(config == NULL) {
        fprintf(stderr, "WARNING: Got config for item \"%s\" not in schema!", &(buf[JS_CONFIG_NAME]));
        fprintf(stderr, "  New value will be added to schema.\n");
        JsConfig *tmp = malloc(sizeof(JsConfig) * (js->config_count + 1));
        if(tmp == NULL) {
            fprintf(stderr, "Failed to allocate memory!\n");
            return(NULL);
        }
        js->config = tmp;
        js->config_count++;
        config = &(js->config[js->config_count-1]);
        default_config(config);
        config->CC = malloc(JS_SCHEMA_NAME_LEN + 1);
        if(config->CC == NULL) {
            fprintf(stderr, "Failed to allocate memory!\n");
            return(NULL);
        }
        memcpy(config->CC, &(buf[JS_CONFIG_NAME]), JS_CONFIG_NAME_LEN);
        config->CC[JS_CONFIG_NAME_LEN] = '\0';
        config->Desc = midi_copy_string(config->CC);
        /* type is already validated earlier */
        config->Typ = buf[JS_CONFIG_TYPE];
    }

    if(config->Typ != buf[JS_CONFIG_TYPE]) {
        fprintf(stderr, "WARNING: Received value with mismatched type from schema!\n");
        fprintf(stderr, "  Old: %hhu (%s)  New: %hhu (%s)\n",
                config->Typ, js_config_type_to_short_name(config->Typ),
                buf[JS_CONFIG_TYPE], js_config_type_to_short_name(buf[JS_CONFIG_TYPE]));
        fprintf(stderr, "  New type will be recorded.\n");
    }

    switch(buf[JS_CONFIG_TYPE]) {
        case JsTypeUInt7:
            config->val.uint = buf[JS_CONFIG_VALUE];
            break;
        case JsTypeUInt8:
            config->val.uint = decode_packed_uint8(&(buf[JS_CONFIG_VALUE]));
            break;
        case JsTypeUInt32:
            config->val.uint = decode_packed_uint32(&(buf[JS_CONFIG_VALUE]));
            break;
        case JsTypeInt32:
            config->val.sint = decode_packed_int32(&(buf[JS_CONFIG_VALUE]));
            break;
        case JsTypeASCII7:
        case JsTypeASCII8:
            /* no clue how ascii8 is formatted because there's no values returned of this type */
            config->val.text = malloc(size - JS_CONFIG_VALUE - MIDI_SYSEX_TAIL + 1);
            if(config->val.text == NULL) {
                return(NULL);
            }
            memcpy(config->val.text, &(buf[JS_CONFIG_VALUE]), size - JS_CONFIG_VALUE - MIDI_SYSEX_TAIL);
            config->val.text[size - JS_CONFIG_VALUE - MIDI_SYSEX_TAIL] = '\0';
            break;
        case JsTypeInt16:
            config->val.sint = decode_packed_int16(&(buf[JS_CONFIG_VALUE]));
            break;
        case JsTypeUInt16:
            config->val.uint = decode_packed_uint16(&(buf[JS_CONFIG_VALUE]));
            break;
        case JsTypeInt64:
            config->val.sint = decode_packed_int64(&(buf[JS_CONFIG_VALUE]));
            break;
        case JsTypeUInt64:
            config->val.uint = decode_packed_uint64(&(buf[JS_CONFIG_VALUE]));
            break;
        default:
            return(NULL);
    }

    config->Typ = buf[JS_CONFIG_TYPE];
    config->validValue = 1;

    return(config);
}
