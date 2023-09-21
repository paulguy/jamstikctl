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
#include <limits.h>

#include <json-c/json.h>

#include "midi.h"
#include "json_schema.h"
#include "packed_values.h"

const char JACK_NAME[] = "jamstikctl";
const char INPORT_NAME[] = "Guitar In";
const char OUTPORT_NAME[] = "Guitar Out";

const char JS_PARAM_NAMES[][9] = {
    "EXPRESSN",
    "PITCHBEN",
    "MPE_MODE"
};

typedef enum {
    JsParamUnknown = -1,
    JsParamExpression = 0,
    JsParamPitchBend,
    JsParamMPEMode
} JsParamIndex;

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

int build_config_set_int(unsigned char *buf, const char *name,
                         JsType type, long long int value) {
    if(!js_config_get_type_is_valid(type) ||
       !js_config_get_type_is_numeric(type)) {
        return(-1);
    }

    switch(js_config_get_type_bits(type)) {
        case 16:
            if(value < SHRT_MIN || value > SHRT_MAX) {
                return(-1);
            }

            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            encode_packed_int16(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 32:
            if(value < INT_MIN || value > INT_MAX) {
                return(-1);
            }

            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            encode_packed_int32(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 64:
            /* long long int is this size */
            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            encode_packed_int64(value, &(buf[JS_CONFIG_VALUE]));
            break;
        default:
            return(-1);
    }

    return(0);
}

int build_config_set_uint(unsigned char *buf, const char *name,
                          JsType type, long long unsigned int value) {

    if(!js_config_get_type_is_valid(type) ||
       !js_config_get_type_is_numeric(type)) {
        return(-1);
    }

    switch(js_config_get_type_bits(type)) {
        case 7:
            if(value > SCHAR_MAX) {
                return(-1);
            }

            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            buf[JS_CONFIG_VALUE] = value;
            break;
        case 8:
            if(value > UCHAR_MAX) {
                return(-1);
            }

            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            encode_packed_uint8(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 16:
            if(value > USHRT_MAX) {
                return(-1);
            }

            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            encode_packed_uint16(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 32:
            if(value > UINT_MAX) {
                return(-1);
            }

            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            encode_packed_uint32(value, &(buf[JS_CONFIG_VALUE]));
            break;
        case 64:
            /* long long int is this size */
            build_js_sysex(buf, JS_CONFIG_VALUE + js_config_get_type_size(type) + MIDI_SYSEX_TAIL);
            memcpy(&(buf[JS_CONFIG_NAME]), name, JS_CONFIG_NAME_LEN);
            encode_packed_uint64(value, &(buf[JS_CONFIG_VALUE]));
            break;
        default:
            return(-1);
    }

    return(0);
}

JsParamIndex lookup_param(const char *name) {
    unsigned int i;

    for(i = 0; i < sizeof(JS_PARAM_NAMES) / sizeof(JS_PARAM_NAMES[0]); i++) {
        if(strncmp(name, JS_PARAM_NAMES[i], 8) == 0) {
            return(i);
        }
    }

    return(JsParamUnknown);
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
    unsigned int i;
    const char *inport;
    const char *outport;
    JsInfo *js;
    JsConfig *config;

    unsigned int cur_category;

    js = js_init();
    if(js == NULL) {
       goto error;
    }

    fprintf(stderr, "Setting up JACK...\n");

    if(midi_setup(JACK_NAME, INPORT_NAME, OUTPORT_NAME, pthread_self()) < 0) {
        fprintf(stderr, "Failed to set up JACK.\n");
        goto error_js_cleanup;
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
                            if(js_parse_json_schema(js, size, buffer) < 0) {
                                fprintf(stderr, "Failed to parse schema.\n");
                                goto error_term_cleanup;
                            }

                            cur_category = 0;

                            size = build_config_query(buffer, js->categories[cur_category]);
                            if(midi_write_event(size, buffer) < 0) {
                                fprintf(stderr, "Failed to write event.\n");
                                goto error_term_cleanup;
                            }
                        } else if(buffer[JS_CMD] == JS_CONFIG_RETURN) {
                            config = js_decode_config_value(js, size, buffer);
                            if(config == NULL) {
                                fprintf(stderr, "WARNING: Got no value back!\n");
                                continue;
                            }

                            switch(lookup_param(config->CC)) {
                                case JsParamExpression:
                                case JsParamPitchBend:
                                case JsParamMPEMode:
                                default:
                                    break;
                            }

                            if(cur_category >= js->category_count) {
                                js_config_print(js, config);
                            }
                        } else if(buffer[JS_CMD] == JS_CONFIG_DONE) {
                            cur_category++;
                            if(cur_category < js->category_count) {
                                size = build_config_query(buffer, js->categories[cur_category]);
                                if(midi_write_event(size, buffer) < 0) {
                                    fprintf(stderr, "Failed to write event.\n");
                                    goto error_term_cleanup;
                                }
                            } else {
                                fprintf(stderr, "Done reading config.\n");
                                for(i = 0; i < js->config_count; i++) {
                                    js_config_print(js, &(js->config[i]));
                                }
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
error_js_cleanup:
    js_free(js);
error:
    return(EXIT_FAILURE);
}
