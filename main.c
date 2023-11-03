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

/* TODO: Print status/log output using static/scrolling print functions */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <limits.h>

#include "terminal.h"
#include "midi.h"
#include "json_schema.h"
#include "packed_values.h"
#include "guitar.h"

const char JACK_NAME[] = "jamstikctl";
const char INPORT_NAME[] = "Guitar In";
const char OUTPORT_NAME[] = "Guitar Out";
const char THRU_NAME[] = "Guitar Thru";

unsigned char buffer[MIDI_MAX_BUFFER_SIZE];

/* TODO: Some kind of table of declarations of parameters, names, descriptions, hotkeys, and handler callbacks */
#define JS_PARAM_STRING_OFFSET (1)
#define JS_PARAM_STRING_CHAR 'x'
const char JS_PARAM_NAMES[][9] = {
    "EXPRESSN",
    "PITCHBEN",
    "MPE_MODE",
    "TRANSPSE",
    "SINGLECH",
    "MIDICHAN",
    "PTCHBSEM",
    "PTCHBCEN",
    "TRANSCRI",
    "MIN__VEL",
    "MAX__VEL",
    "Sx__NOTE",
    "Sx__TRIG"
};

typedef enum {
    JsParamUnknown = -1,
    JsParamExpression = 0,
    JsParamPitchBend,
    JsParamMPEMode,
    JsParamTranspose,
    JsParamSingleChan,
    JsParamMIDIChannel,
    JsParamPitchBendSemitones,
    JsParamPitchBendCents,
    JsParamTranscription,
    JsParamMinVelocity,
    JsParamMaxVelocity,
    JsParamOpenNote,
    JsParamTrigger
} JsParamIndex;

JsParamIndex do_lookup_param(const char *name) {
    unsigned int i;

    for(i = 0; i < sizeof(JS_PARAM_NAMES) / sizeof(JS_PARAM_NAMES[0]); i++) {
        if(strncmp(name, JS_PARAM_NAMES[i], 8) == 0) {
            return(i);
        }
    }
    
    return(JsParamUnknown);
}

JsParamIndex lookup_param(const char *name) {
    JsParamIndex idx;

    idx = do_lookup_param(name);
    if(idx != JsParamUnknown) {
        return(idx);
    }

    /* try for one of the string parameters */
    char param_name[sizeof(JS_PARAM_NAMES[0])];
    memcpy(param_name, name, sizeof(JS_PARAM_NAMES[0]));
    param_name[JS_PARAM_STRING_OFFSET] = JS_PARAM_STRING_CHAR;

    return(do_lookup_param(param_name));
}

void build_js_sysex(unsigned char *buf, size_t len) {
    buf[MIDI_CMD] = MIDI_SYSEX;
    buf[MIDI_SYSEX_VENDOR] = JS_VENDOR_0;
    buf[MIDI_SYSEX_VENDOR+1] = JS_VENDOR_1;
    buf[MIDI_SYSEX_VENDOR+2] = JS_VENDOR_2;
    buf[len-2] = MIDI_SYSEX_DUMMY_LEN;
    buf[len-1] = MIDI_SYSEX_END;
    memset(&(buf[MIDI_SYSEX_BODY]), 0, len-MIDI_SYSEX_HEAD-MIDI_SYSEX_TAIL);
}

int build_config_query(unsigned char *buf, const char *name) {
    build_js_sysex(buf, JS_CONFIG_QUERY_LEN);
    buf[JS_CMD] = JS_CONFIG_QUERY;
    if(name != NULL) {
        memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
    }
    return(JS_CONFIG_QUERY_LEN);
}

int build_schema_query(unsigned char *buf, const char *name) {
    build_js_sysex(buf, JS_SCHEMA_QUERY_LEN);
    buf[JS_CMD] = JS_SCHEMA_QUERY;
    if(name != NULL) {
        memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
    }
    return(JS_SCHEMA_QUERY_LEN);
}

int build_config_set_sint(unsigned char *buf, const char *name,
                          JsType type, long long int value) {
    int size;

    if(!js_config_get_type_is_valid(type) ||
       !js_config_get_type_is_numeric(type)) {
        return(-1);
    }

    size = JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL;

    build_js_sysex(buf, size);
    buf[JS_CMD] = JS_CONFIG_SET;
    buf[JS_CONFIG_TYPE] = type;
    memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);

    switch(js_config_get_type_bits(type)) {
        case 16:
            if(value < SHRT_MIN || value > SHRT_MAX) {
                return(-1);
            }

            encode_packed_int16(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 32:
            if(value < INT_MIN || value > INT_MAX) {
                return(-1);
            }

            encode_packed_int32(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 64:
            /* long long int is this size */
            encode_packed_int64(value, &(buf[JS_CONFIG_VALUE]));
            break;
        default:
            return(-1);
    }

    return(size);
}

int build_config_set_uint(unsigned char *buf, const char *name,
                          JsType type, long long unsigned int value) {
    int size;

    if(!js_config_get_type_is_valid(type) ||
       !js_config_get_type_is_numeric(type)) {
        return(-1);
    }

    size = JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL;

    build_js_sysex(buf, size);
    buf[JS_CMD] = JS_CONFIG_SET;
    buf[JS_CONFIG_TYPE] = type;
    memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);

    switch(js_config_get_type_bits(type)) {
        case 7:
            if(value > SCHAR_MAX) {
                return(-1);
            }

            buf[JS_CONFIG_VALUE] = value;
            break;
        case 8:
            if(value > UCHAR_MAX) {
                return(-1);
            }

            encode_packed_uint8(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 16:
            if(value > USHRT_MAX) {
                return(-1);
            }

            encode_packed_uint16(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 32:
            if(value > UINT_MAX) {
                return(-1);
            }

            encode_packed_uint32(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 64:
            /* long long int is this size */
            encode_packed_uint64(value, &(buf[JS_CONFIG_VALUE]));
            break;
        default:
            return(-1);
    }

    return(size);
}

#define BUILD_CONFIG(BUF, NAME, TYPE, VAL) (js_config_get_type_is_signed((TYPE)) ? \
                                                build_config_set_sint((BUF), (NAME), (TYPE), (VAL)) : \
                                                build_config_set_uint((BUF), (NAME), (TYPE), (VAL)))


int print_numeric_value(JsConfig *config, const char *name) {
    if(!js_config_get_type_is_numeric(config->Typ)) {
        term_print("Tried to get numeric value from nonnumeric type!");
        return(-1);
    }

    if(js_config_get_type_is_signed(config->Typ)) {
        term_print("%s is %ld.", name, config->val.sint);
    } else {
        term_print("%s is %lu.", name, config->val.uint);
    }

    return(0);
}

#define PRINT_BOOL_VALUE(CONFIG, NAME, VALUE) \
    (VALUE) = js_config_get_bool_value(CONFIG); \
    if((VALUE) == JS_YES) { \
        term_print(NAME " is ON."); \
    } else if((VALUE) == JS_NO) { \
        term_print(NAME " is OFF."); \
    }

int send_toggle_value(JsInfo *js, unsigned int param_num, const char *name) {
    JsConfig *config;
    int value;
    int size;

    config = js_config_find(js, JS_PARAM_NAMES[param_num]);
    if(config == NULL) {
        term_print("Couldn't find config entry for %s.", name);
        return(-1);
    }

    value = js_config_get_bool_value(config);
    if(value == JS_NO) {
        term_print("Turning %s ON.", name);
        size = BUILD_CONFIG(buffer, JS_PARAM_NAMES[param_num], config->Typ, JS_YES);
    } else if(value == JS_YES) {
        term_print("Turning %s OFF.", name);
        size = BUILD_CONFIG(buffer, JS_PARAM_NAMES[param_num], config->Typ, JS_NO);
    } else {
        return(-1);
    }

    if(size < 0) {
        term_print("Invalid type!");
        return(-1);
    } else {
        if(midi_write_event(size, buffer) < 0) {
            term_print("Failed to write event.");
            return(-1);
        }
    }

    return(0);
}

int do_send_numeric_value(JsInfo *js, const char *param_name, const char *name,
                          unsigned long long int numEntry, int numEntryNeg) {
    JsConfig *config;
    int size;

    config = js_config_find(js, param_name);
    if(config == NULL) {
        term_print("Couldn't find config entry for %s.", name);
        return(-1);
    }
    if(!js_config_get_type_is_numeric(config->Typ)) {
        term_print("Tried to set nonnumeric type value with number!");
        return(-1);
    }
    if(js_config_get_type_is_signed(config->Typ)) {
        if(js_config_get_type_bits(config->Typ) == 16 && numEntry > INT_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        } else if(js_config_get_type_bits(config->Typ) == 32 && numEntry > INT_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        } else if(numEntry > LLONG_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        }
        long long int jsSInt = (long long int)numEntry * numEntryNeg;
        if(jsSInt < config->Lo.sint || jsSInt > config->Hi.sint) {
            term_print("WARNING: Entered value %lld is out of reported range %ld to %ld!",
                       jsSInt, config->Lo.sint, config->Hi.sint);
        }
        term_print("Setting %s to %lld.", name, jsSInt);
        size = BUILD_CONFIG(buffer, param_name, config->Typ, jsSInt);
    } else {
        if(js_config_get_type_bits(config->Typ) == 7 && numEntry > CHAR_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        } else if(js_config_get_type_bits(config->Typ) == 8 && numEntry > UCHAR_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        } else if(js_config_get_type_bits(config->Typ) == 16 && numEntry > INT_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        } else if(js_config_get_type_bits(config->Typ) == 32 && numEntry > UINT_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        } else if(numEntry > LLONG_MAX) {
            term_print("Entered value would be too big.");
            return(-1);
        }
        if(numEntry < config->Lo.uint || numEntry > config->Hi.uint) {
            term_print("WARNING: Entered value %llu is out of reported range %lu to %lu!",
                       numEntry, config->Lo.uint, config->Hi.uint);
        }
        term_print("Setting %s to %llu.", name, numEntry);
        size = BUILD_CONFIG(buffer, param_name, config->Typ, numEntry);
    } 

    if(size < 0) {
        term_print("Invalid type!");
        return(-1);
    } else {
        if(midi_write_event(size, buffer) < 0) {
            term_print("Failed to write event.");
            return(-1);
        }
    }

    return(0);
}

int send_numeric_value(JsInfo *js, unsigned int param_num, const char *name,
                       unsigned long long int numEntry, int numEntryNeg) {
    return(do_send_numeric_value(js, JS_PARAM_NAMES[param_num], name, numEntry, numEntryNeg));
}

int send_string_value(JsInfo *js, unsigned int param_num, char string, const char *name,
                      unsigned long long int numEntry, int numEntryNeg) {
    char param_name[sizeof(JS_PARAM_NAMES[0])];

    memcpy(param_name, JS_PARAM_NAMES[param_num], sizeof(JS_PARAM_NAMES[0]));
    param_name[JS_PARAM_STRING_OFFSET] = string;

    return(do_send_numeric_value(js, param_name, name, numEntry, numEntryNeg));
}

unsigned long long int add_entry_digit(unsigned long long int numEntry, unsigned int num) {
    if(numEntry > ULLONG_MAX / 10) {
        return(ULLONG_MAX);
    }
    if(numEntry == ULLONG_MAX / 10 && num > ULLONG_MAX % 10) {
        return(ULLONG_MAX);
    }
    numEntry *= 10;
    numEntry += num;

    return(numEntry);
}

void print_entry(unsigned long long int numEntry, int numEntryNeg) {
    if(numEntryNeg < 0) {
        term_print("Entered number: -%llu", numEntry);
    } else {
        term_print("Entered number: %llu", numEntry);
    }
}

int main(int argc, char **argv) {
    int size;
    unsigned int i;
    const char *inport;
    const char *outport;
    int failed_connect = 0;
    GuitarState *g;
    JsInfo *js;
    JsConfig *config;

    unsigned char channel;
    unsigned char velocity;
    unsigned char note;

    unsigned char cc;
    unsigned char value;
    unsigned short RPN[16] = { MIDI_RPN_NULL };
    unsigned short RPNdata[16][16384] = { 0 };

    for(i = 0; i < sizeof(RPNdata) / sizeof(RPNdata[0]); i++ ) {
        /* seems to be a common default */
		RPNdata[i][MIDI_RPN_PITCH_BEND_SENSITIVITY] = MIDI_2BYTE_WORD(48, 0);
        /* from CC spec */
		RPNdata[i][MIDI_RPN_CHANNEL_FINE_TUNING] = MIDI_2BYTE_WORD(0x40, 0);
		RPNdata[i][MIDI_RPN_CHANNEL_COARSE_TUNING] = MIDI_2BYTE_WORD(0x40, 0);
    }

    unsigned int cur_category;

    int keypress;

    unsigned long long int numEntry = 0;
    int numEntryNeg = 1;

    char string = '0';

    js = js_init();
    if(js == NULL) {
       goto error;
    }

    g = guitar_init();
    if(g == NULL) {
        goto error_js_cleanup;
    }

    /* disable the "TUI" mode for now, it's hopelessly broken */
    if(term_setup(1) < 0) {
        fprintf(stderr, "Failed to setup terminal.");
        goto error_guitar_cleanup;
    }
    /*term_cleanup();*/

    term_print("Setting up JACK...");

    if(midi_setup(JACK_NAME, INPORT_NAME, OUTPORT_NAME, pthread_self()) < 0) {
        term_print("Failed to set up JACK.");
        goto error_term_cleanup;
    }

    term_print("JACK client activated...");

    inport = midi_find_port(".*Jamstik MIDI IN$", JackPortIsInput);
    if(inport == NULL) {
        term_print("Failed to find input port.");
        goto error_midi_cleanup;
    }
    outport = midi_find_port(".*Jamstik MIDI IN$", JackPortIsOutput);
    if(outport == NULL) {
        term_print("Failed to find output port.");
        goto error_midi_cleanup;
    }

    if(midi_attach_in_port_by_name(outport) < 0) {
        term_print("Failed to connect input port.");
        failed_connect = 1;
    } else {
        /* yield to be interrupted by a signal once connection is complete */
        usleep(1000000);
    }

    if(midi_attach_out_port_by_name(inport) < 0) {
        term_print("Failed to connect output port.");
        failed_connect = 1;
    } else {
        /* yield to be interrupted by a signal once connection is complete */
        usleep(1000000);
    }

    if(failed_connect) {
        term_print("One or more connections failed to connect automatically, "
                   "they must be connected manually."
                   "Connect these:\n"
                   "%s\nto\n%s:%s\nand\n%s:%s\nto\n%s",
                   outport, JACK_NAME, INPORT_NAME,
                   JACK_NAME, OUTPORT_NAME, inport);
    }

    /* wait until connections have been made, but stop if interrupted */
    while(!midi_ready() && midi_activated()) {
        /* this would be interrupted early when a connection is made */
        usleep(1000000);
    }

    /* TODO: Something here to sync up with something because this doesn't work but adding a delay "fixes" it */
    usleep(1000000);

    /* fetch all state */
    size = build_schema_query(buffer, NULL);
    /* should just error if things were interrupted before this point */
    if(midi_write_event(size, buffer) < 0) {
        term_print("Failed to write event.");
        goto error_midi_cleanup;
    }

    while(midi_activated()) {
        while((keypress = term_getkey()) >= 0) {
            switch(keypress) {
                case 'C':
                    numEntry = 0;
                    numEntryNeg = 0;
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '-':
                    numEntryNeg = -numEntryNeg;
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '0':
                    numEntry = add_entry_digit(numEntry, 0);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '1':
                    numEntry = add_entry_digit(numEntry, 1);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '2':
                    numEntry = add_entry_digit(numEntry, 2);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '3':
                    numEntry = add_entry_digit(numEntry, 3);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '4':
                    numEntry = add_entry_digit(numEntry, 4);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '5':
                    numEntry = add_entry_digit(numEntry, 5);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '6':
                    numEntry = add_entry_digit(numEntry, 6);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '7':
                    numEntry = add_entry_digit(numEntry, 7);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '8':
                    numEntry = add_entry_digit(numEntry, 8);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case '9':
                    numEntry = add_entry_digit(numEntry, 9);
                    print_entry(numEntry, numEntryNeg);
                    break;
                case 'w':
                    send_toggle_value(js, JsParamExpression, "expression");
                    break;
                case 'e':
                    send_toggle_value(js, JsParamPitchBend, "pitch bend");
                    break;
                case 'r':
                    send_toggle_value(js, JsParamMPEMode, "MPE mode");
                    break;
                case 't':
                    send_numeric_value(js, JsParamTranspose, "transposition", numEntry, numEntryNeg);
                    break;
                case 'y':
                    send_toggle_value(js, JsParamSingleChan, "single channel mode");
                    break;
                case 'u':
                    send_numeric_value(js, JsParamMIDIChannel, "MIDI channel", numEntry, numEntryNeg);
                    break;
                case 'i':
                    send_numeric_value(js, JsParamPitchBendSemitones, "pitch bend semitones", numEntry, numEntryNeg);
                    break;
                case 'o':
                    send_numeric_value(js, JsParamPitchBendCents, "pitch bend cents", numEntry, numEntryNeg);
                    break;
                case 'p':
                    send_toggle_value(js, JsParamTranscription, "transcription mode");
                    break;
                case 'a':
                    send_numeric_value(js, JsParamMinVelocity, "minimum velocity", numEntry, numEntryNeg);
                    break;
                case 's':
                    send_numeric_value(js, JsParamMaxVelocity, "maximum velocity", numEntry, numEntryNeg);
                    break;
                case 'd':
                    send_string_value(js, JsParamOpenNote, string, "string open note", numEntry, numEntryNeg);
                    break;
                case 'f':
                    send_string_value(js, JsParamTrigger, string, "string trigger sensitivity", numEntry, numEntryNeg);
                    break;
                case 'z':
                    string = '0';
                    term_print("String 1 (low E) selected.");
                    break;
                case 'x':
                    string = '1';
                    term_print("String 2 (A) selected.");
                    break;
                case 'c':
                    string = '2';
                    term_print("String 3 (D) selected.");
                    break;
                case 'v':
                    string = '3';
                    term_print("String 4 (G) selected.");
                    break;
                case 'b':
                    string = '4';
                    term_print("String 5 (B) selected.");
                    break;
                case 'n':
                    string = '5';
                    term_print("String 6 (high E) selected.");
                    break;
                case 'q':
                    term_cleanup();
                    midi_cleanup();
                    /* will fall through loop and terminate */
            }
        }

        for(;;) {
            size = midi_read_event(sizeof(buffer), buffer);
            if(size > 0) {
                if(buffer[MIDI_CMD] == MIDI_SYSEX) {
                    switch(buffer[JS_CMD]) {
                        case JS_SCHEMA_RETURN:
                            if(js_parse_json_schema(js, size, buffer) < 0) {
                                term_print("Failed to parse schema.");
                                goto error_midi_cleanup;
                            }

                            cur_category = 0;

                            size = build_config_query(buffer, js->categories[cur_category]);
                            if(midi_write_event(size, buffer) < 0) {
                                term_print("Failed to write event.");
                                goto error_midi_cleanup;
                            }
                            break;
                        case JS_CONFIG_RETURN:
                        case JS_CONFIG_SET_RETURN:
                            config = js_decode_config_value(js, size, buffer);
                            if(config == NULL) {
                                term_print("WARNING: Got no value back!");
                                continue;
                            }

                            switch(lookup_param(config->CC)) {
                                case JsParamExpression:
                                    PRINT_BOOL_VALUE(config, "Expression", value)
                                    break;
                                case JsParamPitchBend:
                                    PRINT_BOOL_VALUE(config, "Pitch bend", value)
                                    break;
                                case JsParamMPEMode:
                                    if(js_config_get_bool_value(config)) {
                                        guitar_set_mpe_mode(g, 1);
                                    } else {
                                        /* indicate to set back to previous mode */
                                        guitar_set_mpe_mode(g, 0);
                                    }
                                    break;
                                case JsParamTranspose:
                                    print_numeric_value(config, "Transposition");
                                    break;
                                case JsParamSingleChan:
                                    if(js_config_get_bool_value(config)) {
                                        guitar_set_single_channel_mode(g, 1);
                                    } else {
                                        guitar_set_single_channel_mode(g, 0);
                                    }
                                    break;
                                case JsParamMIDIChannel:
                                    if(js_config_get_type_is_signed(config->Typ)) {
                                        guitar_set_channel(g, config->val.sint);
                                    } else {
                                        guitar_set_channel(g, config->val.uint);
                                    }
                                    break;
                                case JsParamPitchBendSemitones:
                                    print_numeric_value(config, "Pitch bend semitones");
                                    break;
                                case JsParamPitchBendCents:
                                    print_numeric_value(config, "Pitch bend cents");
                                    break;
                                case JsParamTranscription:
                                    PRINT_BOOL_VALUE(config, "Transcription mode", value)
                                    break;
                                case JsParamMinVelocity:
                                    print_numeric_value(config, "Minimum velocity");
                                    break;
                                case JsParamMaxVelocity:
                                    print_numeric_value(config, "Maximum velocity");
                                    break;
                                case JsParamOpenNote:
                                    print_numeric_value(config, "String open note");
                                    break;
                                case JsParamTrigger:
                                    print_numeric_value(config, "String trigger sensitivity");
                                    break;
                                default:
                                    /*
                                    js_config_print(js, config);
                                    */
                                    break;
                            }

                            break;
                        case JS_CONFIG_DONE:
                            if(cur_category < js->category_count) {
                                size = build_config_query(buffer, js->categories[cur_category]);
                                if(midi_write_event(size, buffer) < 0) {
                                    term_print("Failed to write event.");
                                    goto error_midi_cleanup;
                                }
                                cur_category++;
                            } else if(cur_category == js->category_count) {
                                term_print("Done reading config.");
                                /*
                                for(i = 0; i < js->config_count; i++) {
                                    js_config_print(js, &(js->config[i]));
                                }
                                */
                                cur_category++;
                            }
                            break;
                        default:
                            print_hex(size, buffer);
                    }
                } else {
                    channel = buffer[MIDI_CMD] & MIDI_CHANNEL_MASK;

                    switch(buffer[MIDI_CMD] & MIDI_CMD_MASK) {
                        case MIDI_CMD_NOTE_OFF:
                            if(size != MIDI_CMD_NOTE_SIZE) {
                                term_print("WARNING: Got note off of invalid size! (%d != %d)",
                                           size, MIDI_CMD_NOTE_SIZE);
                                if(size < MIDI_CMD_NOTE_SIZE) {
                                    break;
                                }
                            }
                            note = buffer[MIDI_CMD_NOTE];
                            velocity = buffer[MIDI_CMD_NOTE_VEL];
                            guitar_note_off(g, channel, note, velocity);
                            break;
                        case MIDI_CMD_NOTE_ON:
                            if(size != MIDI_CMD_NOTE_SIZE) {
                                term_print("WARNING: Got note on of invalid size! (%d != %d)",
                                           size, MIDI_CMD_NOTE_SIZE);
                                if(size < MIDI_CMD_NOTE_SIZE) {
                                    break;
                                }
                            }
                            note = buffer[MIDI_CMD_NOTE];
                            velocity = buffer[MIDI_CMD_NOTE_VEL];
                            guitar_note_on(g, channel, note, velocity);
                            break;
                        case MIDI_CMD_POLYTOUCH:
                            if(size != MIDI_CMD_POLYTOUCH_SIZE) {
                                term_print("WARNING: Got polyphonic aftertouch event of invalid size! (%d != %d)",
                                           size, MIDI_CMD_NOTE_SIZE);
                                if(size < MIDI_CMD_POLYTOUCH_SIZE) {
                                    break;
                                }
                            }
                            term_print("Polyphonic Aftertouch (%hhd): %hhd Pressure: %hhd",
                                       channel, buffer[MIDI_CMD_NOTE], buffer[MIDI_CMD_POLYTOUCH_PRESSURE]);
                            break;
                        case MIDI_CMD_CC:
                            if(size != MIDI_CMD_CC_SIZE) {
                                term_print("WARNING: Got control change event of invalid size! (%d != %d)",
                                           size, MIDI_CMD_NOTE_SIZE);
                                if(size < MIDI_CMD_CC_SIZE) {
                                    break;
                                }
                            }
                            cc = buffer[MIDI_CMD_CC_CONTROL];
                            value = buffer[MIDI_CMD_CC_VALUE];
                            switch(cc) {
                                case MIDI_CC_RPN_MSB:
                                    RPN[channel] = MIDI_2BYTE_WORD(value, MIDI_2BYTE_WORD_LOW(RPN[channel]));
                                    term_print("Selected RPN for channel %hhd is now %s (%hd) (MSB=%hhd).",
                                           channel, midi_rpn_to_string(RPN[channel]), RPN[channel], value);
                                    break;
                                case MIDI_CC_RPN_LSB:
                                    RPN[channel] = MIDI_2BYTE_WORD(MIDI_2BYTE_WORD_HIGH(RPN[channel]), value);
                                    term_print("Selected RPN for channel %hhd is now %s (%hd) (LSB=%hhd).",
                                               channel, midi_rpn_to_string(RPN[channel]), RPN[channel], value);
                                    break;
                                case MIDI_CC_DATA_ENTRY_MSB:
                                    RPNdata[channel][RPN[channel]] =
                                        MIDI_2BYTE_WORD(value, MIDI_2BYTE_WORD_LOW(RPNdata[channel][RPN[channel]]));
                                    if(midi_parse_rpn(channel, RPN[channel], RPNdata[channel][RPN[channel]]) < 0) {
                                        term_print("RPN value %s (%hd) for channel %hhd is now %hd (MSB=%hhd).",
                                                   midi_rpn_to_string(RPN[channel]), RPN[channel],
                                                   channel, RPNdata[channel][RPN[channel]], value);
                                    }
                                    break;
                                case MIDI_CC_DATA_ENTRY_LSB:
                                    RPNdata[channel][RPN[channel]] =
                                        MIDI_2BYTE_WORD(MIDI_2BYTE_WORD_HIGH(RPNdata[channel][RPN[channel]]), value);
                                    if(midi_parse_rpn(channel, RPN[channel], RPNdata[channel][RPN[channel]]) < 0) {
                                        term_print("RPN value %s (%hd) for channel %hhd is now %hd (LSB=%hhd).",
                                                   midi_rpn_to_string(RPN[channel]), RPN[channel],
                                                   channel, RPNdata[channel][RPN[channel]], value);
                                    }
                                    break;
                                case MIDI_CC_EXPRESSION_LSB:
                                    guitar_set_expression_lsb(g, channel, value);
                                    break;
                                case MIDI_CC_EXPRESSION_MSB:
                                    guitar_set_expression_msb(g, channel, value);
                                    break;
                                default:
                                    term_print("Control Change (%hhd): Control: %s (%hhd) Value: %hhd",
                                               channel, midi_cc_to_string(cc), cc, value);
                            }
                            break;
                        case MIDI_CMD_PROGCH:
                            if(size != MIDI_CMD_PROGCH_SIZE) {
                                term_print("WARNING: Got program change event of invalid size! (%d != %d)",
                                           size, MIDI_CMD_NOTE_SIZE);
                                if(size < MIDI_CMD_PROGCH_SIZE) {
                                    break;
                                }
                            }
                            term_print("Control Change (%hhd): Program: %hhd",
                                       channel, buffer[MIDI_CMD_PROGCH_PROGRAM]);
                            break;
                        case MIDI_CMD_CHANTOUCH:
                            if(size != MIDI_CMD_CHANTOUCH_SIZE) {
                                term_print("WARNING: Got channel aftertouch event of invalid size! (%d != %d)",
                                           size, MIDI_CMD_NOTE_SIZE);
                                if(size < MIDI_CMD_CHANTOUCH_SIZE) {
                                    break;
                                }
                            }
                            term_print("Channel Aftertouch (%hhd): Pressure: %hhd",
                                       channel, buffer[MIDI_CMD_CHANTOUCH_PRESSURE]);
                            break;
                        case MIDI_CMD_PITCHBEND:
                            if(size != MIDI_CMD_PITCHBEND_SIZE) {
                                term_print("WARNING: Got pitchbend event of invalid size! (%d != %d)",
                                           size, MIDI_CMD_NOTE_SIZE);
                                if(size < MIDI_CMD_PITCHBEND_SIZE) {
                                    break;
                                }
                            }
                            guitar_bend(g, channel,
                                        MIDI_2BYTE_WORD(buffer[MIDI_CMD_PITCHBEND_HIGH],
                                                        buffer[MIDI_CMD_PITCHBEND_LOW]) -
                                        MIDI_CMD_PITCHBEND_OFFSET);
                            break;
                        default:
                            print_hex(size, buffer);
                    }
                }
            } else {
                /* if no packets, sleep for a bit */
                break;
            }
        }
        usleep(100000);
    }

    return(EXIT_SUCCESS);

error_midi_cleanup:
    midi_cleanup();
error_term_cleanup:
    term_cleanup();
error_guitar_cleanup:
    free(g);
error_js_cleanup:
    js_free(js);
error:
    return(EXIT_FAILURE);
}
