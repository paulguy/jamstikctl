/*
 * Copyright 2020 paulguy <paulguy119@gmail.com>
 *
 * This file is part of crustymidi.
 *
 * crustymidi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * crustymidi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with crustymidi.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <curses.h>
#include <term.h>

#include <json-c/json.h>

#include "midi.h"
#include "json_schema.h"
#include "packed_values.h"

const char JACK_NAME[] = "jamstikctl";
const char INPORT_NAME[] = "Guitar In";
const char OUTPORT_NAME[] = "Guitar Out";

const unsigned char JS_PARAM_NAMES[][9] = {
    "EXPRESSN",
    "PITCHBEN",
    "MPE_MODE"
};

typedef enum {
    JsParamInvalid = -1,
    JsParamExpression = 0,
    JsParamPitchBend,
    JsParamMPEMode
} JsParamIndex;

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

int stdout_fd;
int stdin_fd;
struct termios original_termios;
struct sigaction ohup;
struct sigaction oint;
struct sigaction oterm;

char *read_file(size_t *len, const char *name) {
    FILE *in = fopen(name, "r");
    fseek(in, 0, SEEK_END);
    *len = ftell(in);
    fseek(in, 0, SEEK_SET);
    char *buf = malloc(*len);
    fread(buf, *len, 1, in);
    fclose(in);

    return(buf);
}

void term_cleanup() {
    int ret;

    ret = tcsetattr(stdout_fd, TCSADRAIN, &original_termios);
    if(ret < 0) {
        fprintf(stderr, "Couldn't reset termios: %s\n", strerror(errno));
    }

    if(sigaction(SIGHUP, &ohup, NULL) != 0 ||
       sigaction(SIGINT, &oint, NULL) != 0 ||
       sigaction(SIGTERM, &oterm, NULL) != 0) {
        fprintf(stderr, "Failed to set signal handler.\n");
    }
}

static void term_cleanup_handler(int signum) {
    term_cleanup();
    if(signum == SIGHUP && ohup.sa_handler != NULL) {
        ohup.sa_handler(signum);
    } else if(signum == SIGINT && oint.sa_handler != NULL) {
        oint.sa_handler(signum);
    } else if(signum == SIGTERM && oterm.sa_handler != NULL) {
        oterm.sa_handler(signum);
    }
}

int term_setup() {
    struct sigaction sa;
    sa.sa_handler = term_cleanup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    struct termios new_termios;

    stdout_fd = fileno(stdout);
    stdin_fd = fileno(stdin);

    if(tcgetattr(stdout_fd, &original_termios) < 0) {
        fprintf(stderr, "Couldn't get termios: %s\n", strerror(errno));
        return(-1);
    }

    if(sigaction(SIGHUP, &sa, &ohup) != 0 ||
       sigaction(SIGINT, &sa, &oint) != 0 ||
       sigaction(SIGTERM, &sa, &oterm) != 0) {
        fprintf(stderr, "Failed to set signal handler.\n");
    }

    memcpy(&new_termios, &original_termios, sizeof(struct termios));
    cfmakeraw(&new_termios);
    /* allow stuff like CTRL+C to work */
    new_termios.c_lflag |= ISIG;
    /* allow printed text to not look weird */
    new_termios.c_oflag |= OPOST;
    new_termios.c_cc[VMIN] = 1;
    new_termios.c_cc[VTIME] = 0;

    if(tcsetattr(stdout_fd, TCSADRAIN, &new_termios) < 0) {
        fprintf(stderr, "Couldn't set termios: %s\n", strerror(errno));
        return(-1);
    }

    if(setupterm(NULL, stdout_fd, NULL) == ERR) {
        return(-1);
    }

    return(0);
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

int build_config_query(unsigned char *buf, const unsigned char *name) {
    build_js_sysex(buf, JS_CONFIG_QUERY_LEN);
    buf[JS_CMD] = JS_CONFIG_QUERY;
    if(name != NULL) {
        memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
    }
    return(JS_CONFIG_QUERY_LEN);
}

int build_schema_query(unsigned char *buf, const unsigned char *name) {
    build_js_sysex(buf, JS_SCHEMA_QUERY_LEN);
    buf[JS_CMD] = JS_SCHEMA_QUERY;
    if(name != NULL) {
        memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
    }
    return(JS_SCHEMA_QUERY_LEN);
}

JsParamIndex lookup_param(size_t len, const unsigned char *name) {
    unsigned int i;

    if(len < 8) {
        return(JsParamInvalid);
    }

    for(i = 0; i < sizeof(JS_PARAM_NAMES) / sizeof(JS_PARAM_NAMES[0]); i++) {
        if(strncmp((char *)name, (char *)JS_PARAM_NAMES[i], 8) == 0) {
            return(i);
        }
    }

    return(JsParamInvalid);
}

int getkey() {
    fd_set readset;
    struct timeval tv;

    FD_ZERO(&readset);
    FD_SET(stdin_fd, &readset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    select(stdin_fd+1, &readset, 0, 0, &tv);
    if(FD_ISSET(stdin_fd, &readset)) {
        return(fgetc(stdin));
    }

    return(-1);
}

int main(int argc, char **argv) {
    size_t size;
    unsigned char buffer[MIDI_MAX_BUFFER_SIZE];
    unsigned int i, j;
    const char *inport;
    const char *outport;
    unsigned int schema_count;
    SchemaItem *schema;
    unsigned int category_count;
    const char **categories;
    unsigned int cur_category;
    int expression;
    int pitchend;
    int MPE;

    fprintf(stderr, "Setting up JACK...\n");

    if(midi_setup(JACK_NAME, INPORT_NAME, OUTPORT_NAME, pthread_self()) < 0) {
        fprintf(stderr, "Failed to set up JACK.\n");
        goto error;
    }

    fprintf(stderr, "JACK client activated...\n");

    inport = midi_find_port(".*Jamstik MIDI IN$", JackPortIsInput);
    if(inport == NULL) {
        fprintf(stderr, "Failed to find input port.\n");
        goto error_midi_cleanup;
    }
    outport = midi_find_port(".*Jamstik MIDI IN$", JackPortIsOutput);
    if(outport == NULL) {
        fprintf(stderr, "Failed to find output port.\n");
        goto error_midi_cleanup;
    }

    if(midi_attach_in_port_by_name(outport) < 0) {
        fprintf(stderr, "Failed to connect input port.\n");
    }
    if(midi_attach_out_port_by_name(inport) < 0) {
        fprintf(stderr, "Failed to connect output port.\n");
    }

    /* if the above calls succeed (which I haven't seen happen, yet) this might be a bit racey */
    if(!midi_ready()) {
        fprintf(stderr, "One or more connections failed to connect automatically, "
                        "they must be connected manually.\n"
                        "Connect these:\n"
                        "%s\nto\n%s:%s\nand\n%s:%s\nto\n%s\n",
                        outport, JACK_NAME, INPORT_NAME,
                        JACK_NAME, OUTPORT_NAME, inport);
    }

    if(term_setup() < 0) {
        fprintf(stderr, "Failed to setup terminal.\n");
        goto error_midi_cleanup;
    }

    while(!midi_ready() && midi_activated()) {
        usleep(1000000);
    }

    if(midi_activated()) {
        size = build_schema_query(buffer, NULL);
        if(midi_write_event(size, buffer) < 0) {
            fprintf(stderr, "Failed to write event.\n");
            goto error_term_cleanup;
        }

        while(midi_activated()) {
            int keypress = getkey();
            switch(keypress) {
                case 'e':
                    break;
                case 'b':
                    break;
                case 'm':
                    break;
                case 'q':
                    term_cleanup();
                    midi_cleanup();
                    /* will fall through loop and terminate */
            }

            for(;;) {
                size = midi_read_event(sizeof(buffer), buffer);
                if(size > 0) {
                    if(buffer[MIDI_CMD] == MIDI_SYSEX) {
                        if(buffer[JS_CMD] == JS_SCHEMA_RETURN) {
                            buffer[size - MIDI_SYSEX_TAIL] = '\0';
                            unsigned char *schema_json = &(buffer[JS_SCHEMA_START]);

                            schema = jamstik_parse_json_schema(&schema_count, size - JS_SCHEMA_EXCESS, schema_json);
                            if(schema == NULL) {
                                fprintf(stderr, "Failed to parse schema.\n");
                            }
                            category_count = 0;
                            categories = NULL;
                            int found;
                            const char **tmp;
                            for(i = 0; i < schema_count; i++) {
                                found = 0;
                                for(j = 0; j < category_count; j++) {
                                    if(strcmp(schema[i].Cat, categories[j]) == 0) {
                                        found = 1;
                                        break;
                                    }
                                }
                                if(found) {
                                    continue;
                                }

                                tmp = realloc(categories, (category_count + 1) * sizeof(char *));
                                if(tmp == NULL) {
                                    fprintf(stderr, "Failed to allocate memory for categories list.\n");
                                    goto error_term_cleanup;
                                }
                                categories = tmp;
                                categories[category_count] = schema[i].Cat;
                                category_count++;
                            }

                            for(i = 0; i < category_count; i++) {
                                printf("Category: %s\n", categories[i]);
                                for(j = 0; j < schema_count; j++) {
                                    if(strcmp(categories[i], schema[j].Cat) == 0) {
                                        printf("  ");
                                        schema_print_item(&(schema[j]));
                                    }
                                }
                            }
                            fflush(stdout);
                            free(schema);

                            cur_category = 0;

                            size = build_config_query(buffer, (unsigned char *)categories[cur_category]);
                            if(midi_write_event(size, buffer) < 0) {
                                fprintf(stderr, "Failed to write event.\n");
                                goto error_term_cleanup;
                            }
                        } else if(buffer[JS_CMD] == JS_CONFIG_RETURN) {
                            if((size < JS_CONFIG_TYPE + 1u + MIDI_SYSEX_TAIL)) {
                                fprintf(stderr, "Config packet too short for necessary fields! (%lu)\n", size);
                                goto error_term_cleanup;
                            } else {
                                int type_size = schema_get_type_size(buffer[JS_CONFIG_TYPE]);
                                if(type_size < 0) {
                                    fprintf(stderr, "Invalid config type %hhu!\n", buffer[JS_CONFIG_TYPE]);
                                    goto error_term_cleanup;
                                }
                                if(size < JS_CONFIG_TYPE + 1 + (unsigned int)type_size + MIDI_SYSEX_TAIL) {
                                    fprintf(stderr, "Config packet too short! (%lu)\n", size);
                                    goto error_term_cleanup;
                                }
                            }

                            printf("%s ", categories[cur_category]);
                            fwrite(&(buffer[JS_CONFIG_NAME]), JS_CONFIG_NAME_LEN, 1, stdout);
                            switch(buffer[JS_CONFIG_TYPE]) {
                                case JsTypeUInt7:
                                    printf(" (uint7) %hhu\n", buffer[JS_CONFIG_VALUE]);
                                    break;
                                case JsTypeUInt8:
                                    printf(" (uint8) %hhu\n", decode_packed_uint8(&(buffer[JS_CONFIG_VALUE])));
                                    break;
                                case JsTypeUInt32:
                                    printf(" (uint32) %u\n", decode_packed_uint32(&(buffer[JS_CONFIG_VALUE])));
                                    break;
                                case JsTypeInt32:
                                    printf(" (int32) %d\n", decode_packed_int32(&(buffer[JS_CONFIG_VALUE])));
                                    break;
                                case JsTypeASCII7:
                                    printf(" (ascii7) \"");
                                    fwrite(&(buffer[JS_CONFIG_VALUE]), size - JS_CONFIG_VALUE - MIDI_SYSEX_TAIL, 1, stdout);
                                    printf("\"\n");
                                    break;
                                case JsTypeASCII8:
                                    /* not implemented */
                                    print_hex(size - JS_CONFIG_VALUE, &(buffer[JS_CONFIG_VALUE]));
                                    break;
                                case JsTypeInt16:
                                    printf(" (int16) %hd\n", decode_packed_int16(&(buffer[JS_CONFIG_VALUE])));
                                    break;
                                case JsTypeUInt16:
                                    printf(" (uint16) %hu\n", decode_packed_uint16(&(buffer[JS_CONFIG_VALUE])));
                                    break;
                                case JsTypeInt64:
                                    printf(" (int64) %ld\n", decode_packed_int64(&(buffer[JS_CONFIG_VALUE])));
                                    break;
                                case JsTypeUInt64:
                                    printf(" (uint64) %lu\n", decode_packed_uint64(&(buffer[JS_CONFIG_VALUE])));
                                    break;
                                default:
                                    print_hex(size - JS_CONFIG_VALUE, &(buffer[JS_CONFIG_VALUE]));
                            }
                        } else if(buffer[JS_CMD] == JS_CONFIG_DONE) {
                            cur_category += 1;
                            if(cur_category < category_count) {
                                size = build_config_query(buffer, (unsigned char *)categories[cur_category]);
                                if(midi_write_event(size, buffer) < 0) {
                                    fprintf(stderr, "Failed to write event.\n");
                                    goto error_term_cleanup;
                                }
                            } else {
                                fprintf(stderr, "Done reading config.\n");
                            }
                        } else {
                            print_hex(size, buffer);
                        }
                    } else {
                        print_hex(size, buffer);
                    }
                } else {
                    /* if no packets, sleep for a bit */
                    break;
                }
            }
            usleep(100000);
        }
    }

    return(EXIT_SUCCESS);

error_term_cleanup:
    term_cleanup();
error_midi_cleanup:
    midi_cleanup();
error:
    return(EXIT_FAILURE);
}
