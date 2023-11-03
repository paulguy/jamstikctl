/*
 * Copyright 2023 paulguy <paulguy119@gmail.com>
 *
 * This file is part of jamstikctl.
 *
 * jamstikctl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * jamstikctl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with jamstikctl.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <json-c/json.h>

#include "terminal.h"
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
        term_print("Error: %s", json_tokener_error_desc(jerr));
        return(NULL);
    }

    if(parsed < len) {
        term_print("Extra chars %lu  parsed %lu", len - parsed, parsed);
    }

    json_tokener_free(tok);

    return(jobj);
}

void default_config(JsConfig *config) {
    config->CC = NULL;
    config->Desc = NULL;
    config->Typ = -1;
    config->Lo.uint = 0;
    config->Hi.uint = 0;
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
        term_print("Get category in schema that's the wrong length, should be JSON_SCHEMA_NAME_LEN!");
        return(-1);
    }

    for(i = 0; i < js->category_count; i++) {
        if(strncmp(js->categories[i], name, JS_SCHEMA_NAME_LEN+1) == 0) {
            return(i);
        }
    }

    categories = realloc(js->categories, sizeof(char **) * (js->category_count + 1));
    if(categories == NULL) {
        term_print("Failed to allocate memory!");
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
        term_print("Failed to allocate memory!");
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

    json_object *lo_value;
    json_object *hi_value;

    /* make it safe to pass to json-c */
    buf[size - MIDI_SYSEX_TAIL] = '\0';
    buf = &(buf[JS_SCHEMA_START]);

    jobj = json_tokenize_whole_string(size - JS_SCHEMA_START - MIDI_SYSEX_TAIL, buf);
    if(jobj == NULL) {
        term_print("Couldn't parse JSON string.");
        goto error;
    }

    json_got = json_object_object_get_ex(jobj, "Schema", &schema);
    if(!json_got) {
        term_print("Couldn't get schema.");
        goto error_put_json;
    }
    if(json_object_get_type(schema) != json_type_array) {
        term_print("Schema type isn't array.");
        goto error_put_json;
    }
    js->config_count = json_object_array_length(schema);
    js->config = malloc(sizeof(JsConfig) * js->config_count);
    if(js->config == NULL) {
        term_print("Failed to allocate memory for schema list.");
        goto error_put_json;
    }
    for(i = 0; i < js->config_count; i++) {
        default_config(&(js->config[i]));

        json_item = json_object_array_get_idx(schema, i);
        if(json_object_get_type(json_item) != json_type_object) {
            term_print("Schema item type isn't object.");
            goto error_free_memory;
        }
        lo_value = NULL;
        hi_value = NULL;
        schema_iter = json_object_iter_begin(json_item);
        schema_iter_end = json_object_iter_end(json_item);
        while(!json_object_iter_equal(&schema_iter, &schema_iter_end)) {
            item_name = json_object_iter_peek_name(&schema_iter);
            item_value = json_object_iter_peek_value(&schema_iter);
            if(strcmp(item_name, "CC") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    term_print("Item CC is not string.");
                    goto error_free_memory;
                }
                json_string = json_object_get_string(item_value);
                if(strlen(json_string) != JS_SCHEMA_NAME_LEN) {
                    term_print("Get name in schema that's the wrong length, should be JSON_SCHEMA_NAME_LEN!");
                    goto error_free_memory;
                }
                js->config[i].CC = midi_copy_string(json_string);
            } else if(strcmp(item_name, "Desc") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    term_print("Item Desc is not string.");
                    goto error_free_memory;
                }
                js->config[i].Desc = midi_copy_string(json_object_get_string(item_value));
            } else if(strcmp(item_name, "Typ") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    term_print("Item Typ is not int.");
                    goto error_free_memory;
                }
                js->config[i].Typ = json_object_get_int(item_value);
                if(!js_config_get_type_is_valid(js->config[i].Typ)) {
                    term_print("Got unknown/invalid type %i!",
                               js->config[i].Typ);
                    goto error_free_memory;
                }
            } else if(strcmp(item_name, "Lo") == 0) {
                lo_value = item_value;
            } else if(strcmp(item_name, "Hi") == 0) {
                hi_value = item_value;
            } else if(strcmp(item_name, "Step") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    term_print("Item Step is not int.");
                    goto error_free_memory;
                }
                js->config[i].Step = json_object_get_int(item_value);
            } else if(strcmp(item_name, "TT") == 0) {
                if(json_object_get_type(item_value) != json_type_int) {
                    term_print("Item TT is not int.");
                    goto error_free_memory;
                }
                js->config[i].TT = json_object_get_int(item_value);
            } else if(strcmp(item_name, "Cat") == 0) {
                if(json_object_get_type(item_value) != json_type_string) {
                    term_print("Item Cat is not string.");
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
                    term_print("Item F is not int.");
                    goto error_free_memory;
                }
                js->config[i].F = json_object_get_int(item_value);
            } else {
                term_print("Unknown field %s type %s.",
                           item_name, json_type_to_name(json_object_get_type(item_value)));
            }
            json_object_iter_next(&schema_iter);
        }
        if(lo_value != NULL) {
            if(js_config_get_type_is_signed(js->config[i].Typ)) {
                js->config[i].Lo.sint = json_object_get_int64(lo_value);
            } else {
                js->config[i].Lo.uint = json_object_get_uint64(lo_value);
            }
        }
        if(hi_value != NULL) {
            if(js_config_get_type_is_signed(js->config[i].Typ)) {
                js->config[i].Hi.sint = json_object_get_int64(hi_value);
            } else {
                js->config[i].Hi.uint = json_object_get_uint64(hi_value);
            }
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
    const char *category = "(uncategorized)";
    /* This will probably be more of a debug function in the future, so just
     * hard code this for convenience for now. */
    const char *flags[7];

    if(config->Cat >= 0 || (unsigned int)config->Cat <= js->category_count) {
        category = js->categories[config->Cat];
    }

    for(i = 0; i < 7; i++) {
        flags[i] = js_config_flag_to_name(config->F & (1 << i));
    }

    if(js_config_get_type_is_signed(config->Typ)) {
        term_print("Category: %s  Name: %s  Description: %s  Type: %s  Lo: %ld  Hi: %ld  Step: %ld  Control: %s  Flags: %u (%s %s %s %s %s %s %s)",
                   category, config->CC, config->Desc,
                   js_config_type_to_name(config->Typ),
                   config->Lo.sint, config->Hi.sint, config->Step,
                   js_config_control_to_name(config->TT), config->F,
                   flags[0], flags[1], flags[2], flags[3], flags[4], flags[5], flags[6]);
    } else {
        term_print("Category: %s  Name: %s  Description: %s  Type: %s  Lo: %lu  Hi: %lu  Step: %ld  Control: %s  Flags: %u (%s %s %s %s %s %s %s)",
                   category, config->CC, config->Desc,
                   js_config_type_to_name(config->Typ),
                   config->Lo.uint, config->Hi.uint, config->Step,
                   js_config_control_to_name(config->TT), config->F,
                   flags[0], flags[1], flags[2], flags[3], flags[4], flags[5], flags[6]);
    }

    if(!config->validValue) {
        term_print("Not set!");
    } else {
        if(js_config_get_type_is_numeric(config->Typ)) {
            if(js_config_get_type_is_signed(config->Typ)) {
                term_print("Value: %ld", config->val.sint);
            } else {
                term_print("Value: %lu", config->val.uint);
            }
        } else {
            term_print("Value: \"%s\"", config->val.text);
        }
    }
}

JsConfig *js_config_find(JsInfo *js, const char *name) {
    unsigned int i;

    for(i = 0; i < js->config_count; i++) {
        if(memcmp(js->config[i].CC, name, JS_CONFIG_NAME_LEN) == 0) {
            return(&(js->config[i]));
        }
    }

    return(NULL);
}

JsConfig *js_decode_config_value(JsInfo *js, size_t size, const unsigned char *buf) {
    char temp[JS_CONFIG_NAME_LEN+1];

    if((size < JS_CONFIG_TYPE + 1u + MIDI_SYSEX_TAIL)) {
        term_print("Config packet too short for necessary fields! (%lu)", size);
        return(NULL);
    } else {
        int type_size = js_config_get_type_size(buf[JS_CONFIG_TYPE]);
        if(type_size < 0) {
            term_print("Invalid config type %hhu!", buf[JS_CONFIG_TYPE]);
            return(NULL);
        }
        if(size < JS_CONFIG_VALUE + (unsigned int)type_size + MIDI_SYSEX_TAIL) {
            term_print("Config packet too short! (%lu)", size);
            return(NULL);
        }
    }

    if(js->config_count == 0) {
        memcpy(temp, &(buf[JS_CONFIG_NAME]), JS_CONFIG_NAME_LEN);
        temp[sizeof(temp)-1] = '\0';
        term_print("WARNING: Ignored a too-early %s report!", temp);
        return(NULL);
    }

    JsConfig *config = js_config_find(js, (const char *)&(buf[JS_CONFIG_NAME]));
    if(config == NULL) {
        term_print("WARNING: Got config for item \"%s\" not in schema!", &(buf[JS_CONFIG_NAME]));
        term_print("  New value will be added to schema.");
        JsConfig *tmp = malloc(sizeof(JsConfig) * (js->config_count + 1));
        if(tmp == NULL) {
            term_print("Failed to allocate memory!");
            return(NULL);
        }
        js->config = tmp;
        js->config_count++;
        config = &(js->config[js->config_count-1]);
        default_config(config);
        config->CC = malloc(JS_SCHEMA_NAME_LEN + 1);
        if(config->CC == NULL) {
            term_print("Failed to allocate memory!");
            return(NULL);
        }
        memcpy(config->CC, &(buf[JS_CONFIG_NAME]), JS_CONFIG_NAME_LEN);
        config->CC[JS_CONFIG_NAME_LEN] = '\0';
        config->Desc = midi_copy_string(config->CC);
        /* type is already validated earlier */
        config->Typ = buf[JS_CONFIG_TYPE];
    }

    if(config->Typ != buf[JS_CONFIG_TYPE]) {
        term_print("WARNING: Received value with mismatched type from schema!");
        term_print("  Old: %hhu (%s)  New: %hhu (%s)",
                   config->Typ, js_config_type_to_short_name(config->Typ),
                   buf[JS_CONFIG_TYPE], js_config_type_to_short_name(buf[JS_CONFIG_TYPE]));
        term_print("  New type will be recorded.");
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

int js_config_get_bool_value(JsConfig *config) {
    if(!js_config_get_type_is_numeric(config->Typ)) {
        term_print("Tried to get boolean value from nonnumeric type!");
    } else {
        if(js_config_get_type_is_signed(config->Typ)) {
            if(config->val.sint) {
                return(JS_YES);
            } else {
                return(JS_NO);
            }
        } else {
            if(config->val.uint) {
                return(JS_YES);
            } else {
                return(JS_NO);
            }
        }
    }

    return(-1);
}
