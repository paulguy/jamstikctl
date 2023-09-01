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

#include <json-c/json.h>

#include "midi.h"
#include "json_schema.h"

const char JACK_NAME[] = "jamstikctl";
const char INPORT_NAME[] = "Guitar In";
const char OUTPORT_NAME[] = "Guitar Out";

#define SCHEMA_START (13)
#define SCHEMA_TAIL (2)
#define SCHEMA_EXCESS (SCHEMA_START + SCHEMA_TAIL)

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

int main(int argc, char **argv) {
    size_t size;
    unsigned char buffer[MIDI_MAX_BUFFER_SIZE];
    unsigned int i;
    const char *inport;
    const char *outport;
    SchemaItem *schema;
    unsigned int schema_count;

    fprintf(stderr, "Setting up JACK...\n");

    if(midi_setup(JACK_NAME, INPORT_NAME, OUTPORT_NAME, pthread_self()) < 0) {
        fprintf(stderr, "Failed to set up JACK.\n");
        return(EXIT_FAILURE);
    }

    fprintf(stderr, "JACK client activated...\n");

    inport = midi_find_port(".*Jamstik MIDI IN$", JackPortIsInput);
    if(inport == NULL) {
        fprintf(stderr, "Failed to find input port.\n");
        goto error;
    }
    outport = midi_find_port(".*Jamstik MIDI IN$", JackPortIsOutput);
    if(outport == NULL) {
        fprintf(stderr, "Failed to find output port.\n");
        goto error;
    }

    if(midi_attach_in_port_by_name(outport) < 0) {
        fprintf(stderr, "Failed to connect input port.\n");
    }
    if(midi_attach_out_port_by_name(inport) < 0) {
        fprintf(stderr, "Failed to connect output port.\n");
    }

    if(!midi_ready()) {
        fprintf(stderr, "One or more connections failed to connect automatically, "
                        "they must be connected manually.\n"
                        "Connect these:\n"
                        "%s\nto\n%s:%s\nand\n%s:%s\nto\n%s\n",
                        outport, JACK_NAME, INPORT_NAME,
                        JACK_NAME, OUTPORT_NAME, inport);
    }

    while(!midi_ready() && midi_activated()) {
        usleep(1000000);
    }

    if(midi_activated()) {
        buffer[0] = 0xF0;
        buffer[1] = 0x00;
        buffer[2] = 0x02;
        buffer[3] = 0x02;
        buffer[4] = 0x44;
        buffer[5] = 0x00;
        buffer[6] = 0x00;
        buffer[7] = 0x00;
        buffer[8] = 0x00;
        buffer[9] = 0x00;
        buffer[10] = 0x00;
        buffer[11] = 0x00;
        buffer[12] = 0x00;
        buffer[13] = 0x55;
        buffer[14] = 0xF7;

        /*
        buffer[0] = 0xF0;
        buffer[1] = 0x00;
        buffer[2] = 0x02;
        buffer[3] = 0x02;
        buffer[4] = 0x60;
        buffer[5] = 'H';
        buffer[6] = 'W';
        buffer[7] = 'D';
        buffer[8] = 'E';
        buffer[9] = 'V';
        buffer[10] = 'T';
        buffer[11] = 'Y';
        buffer[12] = 'P';
        buffer[13] = 0x55;
        buffer[14] = 0xF7;
        */

        /*
        buffer[0] = 0xf0;
        buffer[1] = 0x00;
        buffer[2] = 0x02;
        buffer[3] = 0x02;
        buffer[4] = 0x66;
        buffer[5] = '_';
        buffer[6] = 'B';
        buffer[7] = 'L';
        buffer[8] = 'E';
        buffer[9] = '_';
        buffer[10] = '_';
        buffer[11] = '_';
        buffer[12] = '_';
        buffer[13] = 0x55;
        buffer[14] = 0xf7;
        */

    /*
        buffer[0] = 0xF0;
        buffer[1] = 0x00;
        buffer[2] = 0x02;
        buffer[3] = 0x02;
        buffer[4] = 0x0A;
        buffer[5] = 0x55;
        buffer[6] = 0xF7;
        */

        if(midi_write_event(15, buffer) < 0) {
            fprintf(stderr, "Failed to write event.\n");
            goto error;
        }

        while(midi_activated()) {
            for(;;) {
                size = midi_read_event(sizeof(buffer), buffer);
                if(size > 0) {
                    if(buffer[0] == 0xF0) {
                        if(buffer[4] == 0x45) {
                            buffer[size - SCHEMA_TAIL] = '\0';
                            char *schema_json = &(buffer[SCHEMA_START]);

                            schema = jamstik_parse_json_schema(&schema_count, size - SCHEMA_EXCESS, schema_json);
                            if(schema == NULL) {
                                fprintf(stderr, "Failed to parse schema.\n");
                            }
                            for(i = 0; i < schema_count; i++) {
                                printf("%s\n", schema[i].CC);
                            }
                            free(schema);

                            fprintf(stderr, "\n");
                        } else {
                            print_hex(size, buffer);
                        }
                    } /* else {
                        print_hex(size, buffer);
                    } don't handle MIDI events that aren't sysex */
                } else {
                    break;
                }
            }
            usleep(1000000);
        }
    }

    return(EXIT_SUCCESS);

error:
    midi_cleanup();
    return(EXIT_FAILURE);
}
