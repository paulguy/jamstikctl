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

/* TODO: thru port with optional SYSEX filter */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "terminal.h"
#include "midi.h"

#define MIDI_MAX_EVENTS (256) /* should also be plenty, maybe */

typedef struct midi_event {
    size_t size;
    unsigned char buffer[MIDI_MAX_BUFFER_SIZE];
} midi_event;

/* probably awful crappy ring buffer but I have no idea how to do these so this
   will have to do */
typedef struct {
    midi_event event[MIDI_MAX_EVENTS];
    jack_ringbuffer_t *rb;
    unsigned int next_event;
    size_t sysex;
} EventRB;

typedef struct {
    jack_client_t *jack;
    int activated;
    int ready;
    int filter_sysex;
    pthread_t pid;

    jack_port_t *in;
    jack_port_t *out;
    jack_port_t *thru;

    char *this_inport_name;
    char *this_outport_name;

    char *guitar_inport_name;
    char *guitar_outport_name;

    EventRB inEv, outEv;

    struct sigaction ohup;
    struct sigaction oint;
    struct sigaction oterm;
    struct sigaction ousr1;
} MIDI_ctx_t;

/* must be global so signal handlers work. */
MIDI_ctx_t midictx;

void print_hex(size_t size, unsigned char *buffer) {
    unsigned int i;
    unsigned int j;
    unsigned int row;
    char c;
    char temp[5*16+1];

    for(i = 0; i < size / 16; i++) {
        row = (size - i < 16) ? (size - i) : 16;
        for(j = 0; j < row; j++) {
            c = buffer[i * 16 + j];
            snprintf(&(temp[j * 5]), sizeof(temp) - (j * 5), "%02X %c ",
                     c, (c >= ' ' && c <= '~') ? c : ' ');
        }
        temp[row * 5] = '\0';
        term_print("%s", temp);
    }
}

char *midi_copy_string(const char *src) {
    char *dst;
    size_t len;

    if(src == NULL) {
        return(NULL);
    }

    len = strlen(src);
    dst = malloc(len + 1);
    if(dst == NULL) {
        return(NULL);
    }
    strncpy(dst, src, len + 1);

    return(dst);
}

int midi_activated() {
    return(midictx.activated);
}

void midi_cleanup() {
    if(midictx.activated) {
        if(jack_deactivate(midictx.jack)) {
            term_print("Failed to deactivate JACK client.");
        } else {
            term_print("JACK client deactivated.");
        }
    }

    midictx.activated = 0;

    if(jack_client_close(midictx.jack)) {
        term_print("Error closing JACK connection.");
    } else {
        term_print("JACK connection closed.");
    }

    if(sigaction(SIGHUP, &(midictx.ohup), NULL) != 0 ||
       sigaction(SIGINT, &(midictx.oint), NULL) != 0 ||
       sigaction(SIGTERM, &(midictx.oterm), NULL) != 0) {
        term_print("Failed to set signal handler.");
    }
    if(sigaction(SIGUSR1, &(midictx.ousr1), NULL) != 0) {
        term_print("Failed to reset signal handlers.");
    }

    if(midictx.this_inport_name != NULL) {
        free(midictx.this_inport_name);
        midictx.this_inport_name = NULL;
    }
    if(midictx.this_outport_name != NULL) {
        free(midictx.this_outport_name);
        midictx.this_outport_name = NULL;
    }
    if(midictx.guitar_inport_name != NULL) {
        free(midictx.guitar_inport_name);
        midictx.guitar_inport_name = NULL;
    }
    if(midictx.guitar_outport_name != NULL) {
        free(midictx.guitar_outport_name);
        midictx.guitar_outport_name = NULL;
    }

    if(midictx.inEv.rb != NULL) {
        jack_ringbuffer_free(midictx.inEv.rb);
    }

    if(midictx.outEv.rb != NULL) {
        jack_ringbuffer_free(midictx.outEv.rb);
    }
}

static void _midi_cleanup_handler(int signum) {
    midi_cleanup();
    if(signum == SIGHUP && midictx.ohup.sa_handler != NULL) {
        midictx.ohup.sa_handler(signum);
    } else if(signum == SIGINT && midictx.oint.sa_handler != NULL) {
        midictx.oint.sa_handler(signum);
    } else if(signum == SIGTERM && midictx.oterm.sa_handler != NULL) {
        midictx.oterm.sa_handler(signum);
    }
}

static void _midi_usr1_handler(int signum) {
}

midi_event *_midi_get_event(EventRB *e) {
    midi_event *ev;

    if(jack_ringbuffer_read_space(e->rb) < sizeof(midi_event *)) {
        return(NULL);
    }

    if(jack_ringbuffer_peek(e->rb, (char *)(&ev), sizeof(midi_event *))
       < sizeof(midi_event *)) {
        return(NULL);
    }

    return(ev);
}

int _midi_consume_event(EventRB *e) {
    if(jack_ringbuffer_read_space(e->rb) < sizeof(midi_event *)) {
        return(-1);
    }

    jack_ringbuffer_read_advance(e->rb, sizeof(midi_event *));

    return(0);
}

/* returns 0 on success
 *        -1 on failure
 *         1 on sysex message complete
 *         2 on sysex message in progress */
int _midi_add_event(EventRB *e, size_t size, unsigned char *buf) {
    midi_event *ev;

    if(jack_ringbuffer_write_space(e->rb) < sizeof(midi_event *)) {
        return(-1);
    }

    ev = &(e->event[e->next_event]);
    if(e->sysex) {
        ev->size += size;
        memcpy(&(ev->buffer[e->sysex]), buf, size);
        /* if the end of the sysex command is not found, there's more */
        if(buf[size-1] != 0xF7) {
            /* advance pointer */
            e->sysex += size;
            /* indicate success, but a full packet hasn't been received */
            return(2);
        }
    } else {
        ev->size = size;
        memcpy(ev->buffer, buf, size);
        if(buf[0] == 0xF0) {
            /* if this is part of a sysex, indicate next time to continue ingesting */
            e->sysex = size;
            if(buf[size-1] != 0xF7) {
                /* return now */
                return(2);
            }
        }
    }

    if(jack_ringbuffer_write(e->rb, (char *)(&ev), sizeof(midi_event *)) < sizeof(midi_event *)) {
        return(-1);
    }

    e->next_event += 1;
    if(e->next_event == MIDI_MAX_EVENTS) {
        e->next_event = 0;
    }

    if(e->sysex) {
        e->sysex = 0;
        return(1);
    }

    return(0);
}

/* simple function to just transfer data through */
int _midi_process(jack_nframes_t nframes, void *arg) {
    jack_midi_event_t jackEvent;
    midi_event *event;

    char *in;
    char *out;
    char *thru;
    uint32_t i;
    int has_output = 0;
    int retval;

    thru = jack_port_get_buffer(midictx.thru, nframes);

    /* process queued up input events */
    in = jack_port_get_buffer(midictx.in, nframes);
    for(i = 0;; i++) {
        if(jack_midi_event_get(&jackEvent, in, i)) {
            break;
        }
        retval = _midi_add_event(&(midictx.inEv), jackEvent.size, jackEvent.buffer);

        if(retval < 0) {
            term_print("Failed to add event.");
            return(-1);
        }else if(retval == 0 || retval == 1) {
            /* 0 is success, 1 is completed a sysex */
            has_output = 1;
        } /* 2 indicate a partial sysex packet that is being consumed but not fully received */

        /* pass through non-sysex events unconditionally, pass through sysex
         * events if requested */
        if(retval == 0 ||
           ((retval == 1 || retval == 2) && !midictx.filter_sysex)) {
            /*
            if(jack_midi_event_write(thru, jackEvent.time,
                                           jackEvent.buffer,
                                           jackEvent.size)) {
                term_print("Failed to write thru event.");
                return(-3);
            }
            */
            term_print("%d", jack_midi_event_write(thru, jackEvent.time,
                                           jackEvent.buffer,
                                           jackEvent.size));
        }
    }

    /* process queued up output events */
    out = jack_port_get_buffer(midictx.out, nframes);
    jack_midi_clear_buffer(out);
    off_t offset = 0;
    while(offset < nframes) {
        event = _midi_get_event(&(midictx.outEv));
        if(event == NULL) {
            break;
        }

        if(midictx.outEv.sysex) {
            /* if actively sending out a packet, continue */
            size_t to_write = event->size - midictx.outEv.sysex;
            to_write = nframes < to_write ? nframes : to_write;
            if(jack_midi_event_write(out, 0,
                                     &(event->buffer[midictx.outEv.sysex]),
                                     to_write)) {
                term_print("Failed to write event.");
                return(-3);
            }
            midictx.outEv.sysex += to_write;

            /* if this is the end of the packet, indicate done-ness */
            if(midictx.outEv.sysex >= event->size) {
                midictx.outEv.sysex = 0;
                offset += to_write;
            } else {
                /* don't consume if not done */
                break;
            }
        } else {
            if(offset + event->size > nframes) {
                /* if the packet doesn't fit, send just enough to fill */
                midictx.outEv.sysex = nframes - offset;
                if(jack_midi_event_write(out, offset,
                                         event->buffer,
                                         nframes - offset)) {
                    term_print("Failed to write event.");
                    return(-3);
                }
                /* don't consume the packet that hasn't fully sent */
                break;
            } else {
                if(jack_midi_event_write(out, offset,
                                         event->buffer,
                                         event->size)) {
                    term_print("Failed to write event.");
                    return(-3);
                }
                offset += event->size;
            }
        }

        _midi_consume_event(&(midictx.outEv));
    }

    if(has_output != 0) {
        pthread_kill(midictx.pid, SIGUSR1);
    }

    return(0);
}

int midi_write_event(size_t size, unsigned char *buffer) {
    int ret;

    /* if the device closed in another thread, don't try to do anything */
    if(!midictx.activated) {
        return(-1);
    }

    ret = _midi_add_event(&(midictx.outEv), size, buffer);
    /* don't allow to queue partial sysexes externally */
    if(ret < 0 || ret > 1) {
        return(-1);
    }

    return(0);
}

int midi_read_event(size_t size, unsigned char *buffer) {
    midi_event *ev;
    size_t evsize;

    if(!midictx.activated) {
        return(0);
    }

    ev = _midi_get_event(&(midictx.inEv));
    if(ev == NULL) {
        return(0);
    }
    if(ev->size > size) {
        return(ev->size);
    }

    memcpy(buffer, ev->buffer, ev->size);
    evsize = ev->size;

    _midi_consume_event(&(midictx.inEv));

    return(evsize);
}

void _midi_print_ports() {
    const char **search;
    unsigned int i;
    jack_port_t *port;

    search = jack_get_ports(midictx.jack, NULL, NULL, 0);
    for(i = 0; search[i] != NULL; i++) {
        port = jack_port_by_name(midictx.jack, search[i]);
        term_print("%s %02X %s", search[i], jack_port_flags(port), jack_port_type(port));
    }
    jack_free(search);
}

#define _MIDI_INPORT_MASK  (1 << 0)
#define _MIDI_OUTPORT_MASK (1 << 1)
#define _MIDI_PORT_ID_GUITAR_IN  (1)
#define _MIDI_PORT_ID_GUITAR_OUT (2)
#define _MIDI_PORT_ID_THIS_IN    (3)
#define _MIDI_PORT_ID_THIS_OUT   (4)
void _midi_port_connect_cb(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
    const char *namea, *nameb;
    int porta_id = 0;
    int portb_id = 0;

    namea = jack_port_name(jack_port_by_id(midictx.jack, a));
    nameb = jack_port_name(jack_port_by_id(midictx.jack, b));

    /* meaningful port identities
     * guitar out, guitar in, this out, this in
     * both a or b could be any of those 4 */
    if(midictx.guitar_inport_name != NULL &&
       strcmp(namea, midictx.guitar_inport_name) == 0) {
        porta_id = _MIDI_PORT_ID_GUITAR_IN;
    } else if(midictx.guitar_outport_name != NULL &&
              strcmp(namea, midictx.guitar_outport_name) == 0) {
        porta_id = _MIDI_PORT_ID_GUITAR_OUT;
    } else if(strcmp(namea, midictx.this_inport_name) == 0) {
        porta_id = _MIDI_PORT_ID_THIS_IN;
    } else if(strcmp(namea, midictx.this_outport_name) == 0) {
        porta_id = _MIDI_PORT_ID_THIS_OUT;
    }

    if(midictx.guitar_inport_name != NULL &&
       strcmp(nameb, midictx.guitar_inport_name) == 0) {
        portb_id = _MIDI_PORT_ID_GUITAR_IN;
    } else if(midictx.guitar_outport_name != NULL &&
              strcmp(nameb, midictx.guitar_outport_name) == 0) {
        portb_id = _MIDI_PORT_ID_GUITAR_OUT;
    } else if(strcmp(nameb, midictx.this_inport_name) == 0) {
        portb_id = _MIDI_PORT_ID_THIS_IN;
    } else if(strcmp(nameb, midictx.this_outport_name) == 0) {
        portb_id = _MIDI_PORT_ID_THIS_OUT;
    }

    if((porta_id == _MIDI_PORT_ID_GUITAR_OUT && portb_id == _MIDI_PORT_ID_THIS_IN) ||
       (portb_id == _MIDI_PORT_ID_GUITAR_OUT && porta_id == _MIDI_PORT_ID_THIS_IN)) {
        if(connect) {
            midictx.ready |= _MIDI_INPORT_MASK;
        } else {
            midictx.ready &= ~_MIDI_INPORT_MASK;
        }
        goto connected;
    } else if((porta_id == _MIDI_PORT_ID_THIS_OUT && portb_id == _MIDI_PORT_ID_GUITAR_IN) ||
               (portb_id == _MIDI_PORT_ID_THIS_OUT && porta_id == _MIDI_PORT_ID_GUITAR_IN)) {
        if(connect) {
            midictx.ready |= _MIDI_OUTPORT_MASK;
        } else {
            midictx.ready &= ~_MIDI_OUTPORT_MASK;
        }
        goto connected;
    }

    if(connect) {
        if(porta_id != 0 || portb_id != 0) {
            term_print("No, that wasn't right, connect\n%s\nto\n%s\nand\n%s\nto\n%s",
                       midictx.guitar_outport_name, midictx.this_inport_name,
                       midictx.this_outport_name, midictx.guitar_inport_name);
        }
        return;
    }

connected:
    if(midictx.ready == _MIDI_INPORT_MASK || midictx.ready == _MIDI_OUTPORT_MASK) {
        term_print("1 connection remaining");
    } else if(midictx.ready == (_MIDI_INPORT_MASK | _MIDI_OUTPORT_MASK)) {
        term_print("sequence complete");
    }

    pthread_kill(midictx.pid, SIGUSR1);
}

int midi_ready() {
    return(midictx.ready == (_MIDI_INPORT_MASK | _MIDI_OUTPORT_MASK));
}

int midi_setup(const char *client_name, const char *inport_name,
               const char *outport_name, const char *thruport_name,
               int filter_sysex, pthread_t pid) {
    /* jack stuff */
    jack_status_t jstatus;
    struct sigaction sa;
    sa.sa_handler = _midi_cleanup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    midictx.activated = 0;
    midictx.ready = 0;
    midictx.filter_sysex = filter_sysex;
    midictx.pid = pid;
    midictx.this_inport_name = NULL;
    midictx.this_outport_name = NULL;
    midictx.guitar_inport_name = NULL;
    midictx.guitar_outport_name = NULL;
    midictx.inEv.next_event = 0;
    midictx.inEv.rb = NULL;
    midictx.inEv.sysex = 0;
    midictx.outEv.next_event = 0;
    midictx.outEv.rb = NULL;
    midictx.outEv.sysex = 0;

    midictx.jack = jack_client_open(client_name, JackNoStartServer, &jstatus);
    if(midictx.jack == NULL) {
        term_print("Failed to open JACK connection.");
        return(-1);
    }

    if(sigaction(SIGHUP, &sa, &(midictx.ohup)) != 0 ||
       sigaction(SIGINT, &sa, &(midictx.oint)) != 0 ||
       sigaction(SIGTERM, &sa, &(midictx.oterm)) != 0) {
        term_print("Failed to set signal handler.");
    }
    sa.sa_handler = _midi_usr1_handler;
    if(sigaction(SIGUSR1, &sa, &(midictx.ousr1)) != 0) {
        term_print("Failed to set signal handler.");
    }

    midictx.in = jack_port_register(midictx.jack,
                                    inport_name,
                                    JACK_DEFAULT_MIDI_TYPE,
                                    JackPortIsInput,
                                    0);
    if(midictx.in == NULL) {
        term_print("Failed to register in port.");
        midi_cleanup();
        return(-1);
    }
    midictx.this_inport_name = midi_copy_string(jack_port_name(midictx.in));
    if(midictx.this_inport_name == NULL) {
        midi_cleanup();
        return(-1);
    }

    midictx.out = jack_port_register(midictx.jack,
                                     outport_name,
                                     JACK_DEFAULT_MIDI_TYPE,
                                     JackPortIsOutput,
                                     0);
    if(midictx.out == NULL) {
        term_print("Failed to register out port.");
        midi_cleanup();
        return(-1);
    }
    midictx.this_outport_name = midi_copy_string(jack_port_name(midictx.out));
    if(midictx.this_outport_name == NULL) {
        midi_cleanup();
        return(-1);
    }

    midictx.thru = jack_port_register(midictx.jack,
                                      thruport_name,
                                      JACK_DEFAULT_MIDI_TYPE,
                                      JackPortIsOutput,
                                      0);
    if(midictx.thru == NULL) {
        term_print("Failed to register thru port.");
        midi_cleanup();
        return(-1);
    }

    midictx.inEv.rb = jack_ringbuffer_create(sizeof(midi_event *) * MIDI_MAX_EVENTS);
    if(midictx.inEv.rb == NULL) {
        term_print("Failed to create input ringbuffer.");
        midi_cleanup();
        return(-1);
    }

    midictx.outEv.rb = jack_ringbuffer_create(sizeof(midi_event *) * MIDI_MAX_EVENTS);
    if(midictx.outEv.rb == NULL) {
        term_print("Failed to create output ringbuffer.");
        midi_cleanup();
        return(-1);
    }

    if(jack_set_port_connect_callback(midictx.jack, _midi_port_connect_cb, NULL)) {
        term_print("Failed to set JACK port connect callback.");
        midi_cleanup();
        return(-1);
    }

    if(jack_set_process_callback(midictx.jack, _midi_process, NULL)) {
        term_print("Failed to set JACK process callback.");
        midi_cleanup();
        return(-1);
    }

    if(jack_activate(midictx.jack)) {
        term_print("Failed to activate JACK client.");
        midi_cleanup();
        return(-1);
    }
    midictx.activated = 1;

    return(0);
}

char *midi_find_port(const char *pattern, unsigned long flags) {
    const char **search;
    char *name;

    search = jack_get_ports(midictx.jack, pattern, NULL, flags);
    if(search == NULL || search[0] == NULL) {
        term_print("No ports found for criteria.");
        goto error;
    }

    name = midi_copy_string(search[0]);
    if(name == NULL) {
        goto error;
    }

    jack_free(search);

    return(name);

error:
    jack_free(search);
    return(NULL);
}

int _midi_connect(const char *src, const char *dst) {
    jack_port_t *srcport, *dstport;
    int err, srcflags, dstflags;
    const char *srctype, *dsttype;

    err = jack_connect(midictx.jack, src, dst);
    if(err == 0) {
        return(0);
    }

    term_print("jack_connect() returned error %d (%s)",
               err, strerror(err));

    if(midictx.jack == NULL) {
        term_print("Jack client is NULL.");
        return(err);
    }
    if(src == NULL) {
        term_print("Source port name is NULL.");
        return(err);
    }
    if(dst == NULL) {
        term_print("Destination port name is NULL.");
        return(err);
    }
    srcport = jack_port_by_name(midictx.jack, src);
    if(srcport == NULL) {
        term_print("Got NULL source port.");
        return(err);
    }
    dstport = jack_port_by_name(midictx.jack, dst);
    if(dstport == NULL) {
        term_print("Got NULL destination port.");
        return(err);
    }
    srcflags = jack_port_flags(srcport);
    if(!(srcflags & JackPortIsOutput)) {
        term_print("Source port isn't an output. Flags: %02X", srcflags);
        return(err);
    }
    dstflags = jack_port_flags(dstport);
    if(!(dstflags & JackPortIsInput)) {
        term_print("Destination port isn't an input. Flags: %02X", dstflags);
        return(err);
    }
    srctype = jack_port_type(srcport);
    dsttype = jack_port_type(dstport);
    if(strcmp(srctype, dsttype) != 0) {
        term_print("Different source and destination port types. %s != %s",
                   srctype, dsttype);
        return(err);
    }

    term_print("Unknown error. %s 0x%02X %s, %s 0x%02X %s",
               src, srcflags, srctype, dst, dstflags, dsttype);

    return(err);
}

int midi_attach_in_port_by_name(const char *name) {
    midictx.guitar_outport_name = midi_copy_string(name);
    if(midictx.guitar_outport_name == NULL) {
        return(-1);
    }

    /* source out to this in */
    if(_midi_connect(midictx.guitar_outport_name, midictx.this_inport_name) != 0) {
        return(-1);
    }

    return(0);
}

int midi_attach_out_port_by_name(const char *name) {
    midictx.guitar_inport_name = midi_copy_string(name);
    if(midictx.guitar_inport_name == NULL) {
        return(-1);
    }

    /* this out to source in */
    if(_midi_connect(midictx.this_outport_name, midictx.guitar_inport_name) != 0) {
        return(-1);
    }

    return(0);
}

const char NOTE_LOOKUP[] = "AABCCDDEEFFG";
const char OCTAVE_LOOKUP[] = "0123456789";

int midi_num_to_note(size_t size, char *buf, unsigned int note, int flat) {
    if(note > 127) {
        /* nothing written */
        return(0);
    }
    int num = ((int)note - 21) % 12;
    int octave = ((int)note - 21) / 12;
    int len;

    switch(num) {
        case 0:
        case 2:
        case 3:
        case 5:
        case 7:
        case 9:
        case 11:
            /* naturals */
            if(octave < 0) {
                if(size < 4) {
                    return(4);
                }
                len = 4;
                buf[0] = NOTE_LOOKUP[num];
                buf[1] = ' ';
                buf[2] = '-';
                buf[3] = OCTAVE_LOOKUP[-octave];
            } else {
                if(size < 3) {
                    return(3);
                }
                len = 3;
                buf[0] = NOTE_LOOKUP[num];
                buf[1] = ' ';
                buf[2] = OCTAVE_LOOKUP[octave];
            }
            break;
        default:
            if(octave < 0) {
                if(size < 4) {
                    return(4);
                }
                len = 4;
                if(flat) {
                    buf[0] = NOTE_LOOKUP[num+1];
                    buf[1] = 'b';
                } else {
                    buf[0] = NOTE_LOOKUP[num];
                    buf[1] = '#';
                }
                buf[2] = '-';
                buf[3] = OCTAVE_LOOKUP[-octave];
            } else {
                if(size < 3) {
                    return(3);
                }
                len = 3;
                if(flat) {
                    buf[0] = NOTE_LOOKUP[num+1];
                    buf[1] = 'b';
                } else {
                    buf[0] = NOTE_LOOKUP[num];
                    buf[1] = '#';
                }
                buf[2] = OCTAVE_LOOKUP[octave];
            }
    }

    return(len);
}

const char *midi_cc_to_string(unsigned int cc) {
    switch(cc) {
        case MIDI_CC_BANK_SELECT_MSB:
            return("Bank Select MSB");
        case MIDI_CC_MOD_WHEEL_MSB:
            return("Modulation Wheel MSB");
        case MIDI_CC_BREATH_CONTROL_MSB:
            return("Breath Controller MSB");
        case MIDI_CC_UNDEFINED_1_MSB:
            return("Undefined 1 MSB");
        case MIDI_CC_FOOT_PEDAL_MSB:
            return("Foot Pedal MSB");
        case MIDI_CC_PORTAMENTO_TIME_MSB:
            return("Portamento Time MSB");
        case MIDI_CC_DATA_ENTRY_MSB:
            return("Data Entry MSB");
        case MIDI_CC_VOLUME_MSB:
            return("Volume MSB");
        case MIDI_CC_BALANCE_MSB:
            return("Balance MSB");
		case MIDI_CC_UNDEFINED_2_MSB:
            return("Undefined 2 MSB");
		case MIDI_CC_PAN_MSB:
            return("Pan MSB");
		case MIDI_CC_EXPRESSION_MSB:
            return("Expression MSB");
		case MIDI_CC_EFFECT_CONTROL_1_MSB:
            return("Effect Control 1 MSB");
		case MIDI_CC_EFFECT_CONTROL_2_MSB:
            return("Effect Control 2 MSB");
		case MIDI_CC_UNDEFINED_3_MSB:
            return("Undefined 3 MSB");
		case MIDI_CC_UNDEFINED_4_MSB:
            return("Undefined 4 MSB");
		case MIDI_CC_GENERAL_PURPOSE_1_MSB:
            return("General Purpose Controller 1 MSB");
		case MIDI_CC_GENERAL_PURPOSE_2_MSB:
            return("General Purpose Controller 2 MSB");
		case MIDI_CC_GENERAL_PURPOSE_3_MSB:
            return("General Purpose Controller 3 MSB");
		case MIDI_CC_GENERAL_PURPOSE_4_MSB:
            return("General Purpose Controller 4 MSB");
		case MIDI_CC_UNDEFINED_5_MSB:
            return("Undefined 5 MSB");
		case MIDI_CC_UNDEFINED_6_MSB:
            return("Undefined 6 MSB");
		case MIDI_CC_UNDEFINED_7_MSB:
            return("Undefined 7 MSB");
		case MIDI_CC_UNDEFINED_8_MSB:
            return("Undefined 8 MSB");
		case MIDI_CC_UNDEFINED_9_MSB:
            return("Undefined 9 MSB");
		case MIDI_CC_UNDEFINED_10_MSB:
            return("Undefined 10 MSB");
		case MIDI_CC_UNDEFINED_11_MSB:
            return("Undefined 11 MSB");
		case MIDI_CC_UNDEFINED_12_MSB:
            return("Undefined 12 MSB");
		case MIDI_CC_UNDEFINED_13_MSB:
            return("Undefined 13 MSB");
		case MIDI_CC_UNDEFINED_14_MSB:
            return("Undefined 14 MSB");
		case MIDI_CC_UNDEFINED_15_MSB:
            return("Undefined 15 MSB");
		case MIDI_CC_UNDEFINED_16_MSB:
            return("Undefined 16 MSB");
		case MIDI_CC_BANK_SELECT_LSB:
            return("Bank Select LSB");
		case MIDI_CC_MOD_WHEEL_LSB:
            return("Modulation Wheel LSB");
		case MIDI_CC_BREATH_CONTROL_LSB:
            return("Breath Controller LSB");
		case MIDI_CC_UNDEFINED_1_LSB:
            return("Undefined 1 LSB");
		case MIDI_CC_FOOT_PEDAL_LSB:
            return("Foot Pedal LSB");
		case MIDI_CC_PORTAMENTO_TIME_LSB:
            return("Portamento Time LSB");
		case MIDI_CC_DATA_ENTRY_LSB:
            return("Data Entry LSB");
		case MIDI_CC_VOLUME_LSB:
            return("Volume LSB");
		case MIDI_CC_BALANCE_LSB:
            return("Balance LSB");
		case MIDI_CC_UNDEFINED_2_LSB:
            return("Undefined 2 LSB");
		case MIDI_CC_PAN_LSB:
            return("Pan LSB");
		case MIDI_CC_EXPRESSION_LSB:
            return("Expression LSB");
		case MIDI_CC_EFFECT_CONTROL_1_LSB:
            return("Effect Control 1 LSB");
		case MIDI_CC_EFFECT_CONTROL_2_LSB:
            return("Effect Control 2 LSB");
		case MIDI_CC_UNDEFINED_3_LSB:
            return("Undefined 3 LSB");
		case MIDI_CC_UNDEFINED_4_LSB:
            return("Undefined 4 LSB");
		case MIDI_CC_GENERAL_PURPOSE_1_LSB:
            return("General Purpose Controller 1 MSB");
		case MIDI_CC_GENERAL_PURPOSE_2_LSB:
            return("General Purpose Controller 2 MSB");
		case MIDI_CC_GENERAL_PURPOSE_3_LSB:
            return("General Purpose Controller 3 MSB");
		case MIDI_CC_GENERAL_PURPOSE_4_LSB:
            return("General Purpose Controller 4 MSB");
		case MIDI_CC_UNDEFINED_5_LSB:
            return("Undefined 5 LSB");
		case MIDI_CC_UNDEFINED_6_LSB:
            return("Undefined 6 LSB");
		case MIDI_CC_UNDEFINED_7_LSB:
            return("Undefined 7 LSB");
		case MIDI_CC_UNDEFINED_8_LSB:
            return("Undefined 8 LSB");
		case MIDI_CC_UNDEFINED_9_LSB:
            return("Undefined 9 LSB");
		case MIDI_CC_UNDEFINED_10_LSB:
            return("Undefined 10 LSB");
		case MIDI_CC_UNDEFINED_11_LSB:
            return("Undefined 11 LSB");
		case MIDI_CC_UNDEFINED_12_LSB:
            return("Undefined 12 LSB");
		case MIDI_CC_UNDEFINED_13_LSB:
            return("Undefined 13 LSB");
		case MIDI_CC_UNDEFINED_14_LSB:
            return("Undefined 14 LSB");
		case MIDI_CC_UNDEFINED_15_LSB:
            return("Undefined 15 LSB");
		case MIDI_CC_UNDEFINED_16_LSB:
            return("Undefined 16 LSB");
		case MIDI_CC_DAMPER_MODE:
            return("Damper Pedal On/Off");
		case MIDI_CC_PORTAMENTO_MODE:
            return("Portamento On/Off");
		case MIDI_CC_SOSTENUDO_MODE:
            return("Sostenudo On/Off");
		case MIDI_CC_SOFT_MODE:
            return("Soft Pedal On/Off");
		case MIDI_CC_LEGATO_MODE:
            return("Legato On/Off");
		case MIDI_CC_HOLD_2_MODE:
            return("Hold 2 On/Off");
		case MIDI_CC_SOUND_CONTROL_1:
            return("Sound Controller 1 (Default: Sound Variation)");
		case MIDI_CC_SOUND_CONTROL_2:
            return("Sound Controller 2 (Default: Timbre/Harmonic Intensity)");
		case MIDI_CC_SOUND_CONTROL_3:
            return("Sound Controller 3 (Default: Release Time)");
		case MIDI_CC_SOUND_CONTROL_4:
            return("Sound Controller 4 (Default: Attack Time)");
		case MIDI_CC_SOUND_CONTROL_5:
            return("Sound Controller 5 (Default: Brightness)");
		case MIDI_CC_SOUND_CONTROL_6:
            return("Sound Controller 6 (Default: Decay Time)");
		case MIDI_CC_SOUND_CONTROL_7:
            return("Sound Controller 7 (Default: Vibrato Rate)");
		case MIDI_CC_SOUND_CONTROL_8:
            return("Sound Controller 8 (Default: Vibrato Depth)");
		case MIDI_CC_SOUND_CONTROL_9:
            return("Sound Controller 9 (Default: Vibrato Delay)");
		case MIDI_CC_SOUND_CONTROL_10:
            return("Sound Controller 10 (Default: Undefined)");
		case MIDI_CC_GENERAL_PURPOSE_5:
            return("Geneal Purpose Controller 5");
		case MIDI_CC_GENERAL_PURPOSE_6:
            return("Geneal Purpose Controller 6");
		case MIDI_CC_GENERAL_PURPOSE_7:
            return("Geneal Purpose Controller 7");
		case MIDI_CC_GENERAL_PURPOSE_8:
            return("Geneal Purpose Controller 8");
		case MIDI_CC_PORTAMENTO:
            return("Portamento Control");
		case MIDI_CC_UNDEFINED_17:
            return("Undefined 17");
		case MIDI_CC_UNDEFINED_18:
            return("Undefined 18");
		case MIDI_CC_UNDEFINED_19:
            return("Undefined 19");
		case MIDI_CC_HIRES_VELOCITY_PREFIX:
		case MIDI_CC_UNDEFINED_20:
            return("Undefined 20");
		case MIDI_CC_UNDEFINED_21:
            return("Undefined 21");
		case MIDI_CC_FX_1_DEPTH:
            return("Effects 1 Depth (Default: Reverb Send Level)");
		case MIDI_CC_FX_2_DEPTH:
            return("Effects 2 Depth (Default: Tremolo Depth)");
		case MIDI_CC_FX_3_DEPTH:
            return("Effects 3 Depth (Default: Chorus Send Level)");
		case MIDI_CC_FX_4_DEPTH:
            return("Effects 4 Depth (Default: Celeste/Detune Depth)");
		case MIDI_CC_FX_5_DEPTH:
            return("Effects 5 Depth (Default: Phaser Depth)");
		case MIDI_CC_DATA_INCREMENT:
            return("Data Increment");
		case MIDI_CC_DATA_DECREMENT:
            return("Data Decrement");
		case MIDI_CC_NRPN_LSB:
            return("Non-Registered Parameter Number LSB");
		case MIDI_CC_NRPN_MSB:
            return("Non-Registered Parameter Number MSB");
		case MIDI_CC_RPN_LSB:
            return("Registered Parameter Number LSB");
		case MIDI_CC_RPN_MSB:
            return("Registered Parameter Number MSB");
		case MIDI_CC_UNDEFINED_22:
            return("Undefined 22");
		case MIDI_CC_UNDEFINED_23:
            return("Undefined 23");
		case MIDI_CC_UNDEFINED_24:
            return("Undefined 24");
		case MIDI_CC_UNDEFINED_25:
            return("Undefined 25");
		case MIDI_CC_UNDEFINED_26:
            return("Undefined 26");
		case MIDI_CC_UNDEFINED_27:
            return("Undefined 27");
		case MIDI_CC_UNDEFINED_28:
            return("Undefined 28");
		case MIDI_CC_UNDEFINED_29:
            return("Undefined 29");
		case MIDI_CC_UNDEFINED_30:
            return("Undefined 30");
		case MIDI_CC_UNDEFINED_31:
            return("Undefined 31");
		case MIDI_CC_UNDEFINED_32:
            return("Undefined 32");
		case MIDI_CC_UNDEFINED_33:
            return("Undefined 33");
		case MIDI_CC_UNDEFINED_34:
            return("Undefined 34");
		case MIDI_CC_UNDEFINED_35:
            return("Undefined 35");
		case MIDI_CC_UNDEFINED_36:
            return("Undefined 36");
		case MIDI_CC_UNDEFINED_37:
            return("Undefined 37");
		case MIDI_CC_UNDEFINED_38:
            return("Undefined 38");
		case MIDI_CC_UNDEFINED_39:
            return("Undefined 39");
		case MIDI_CC_ALL_SOUND_OFF:
            return("All Sound Off");
		case MIDI_CC_RESET_ALL_CONTROLLERS:
            return("Reset All Controllers");
		case MIDI_CC_LOCAL_CONTROL_MODE:
            return("Local Control On/Off");
		case MIDI_CC_ALL_NOTES_OFF:
            return("All Notes Off");
		case MIDI_CC_OMNI_MODE_OFF:
            return("Omni Mode Off");
		case MIDI_CC_OMNI_MODE_ON:
            return("Omno Mode On");
		case MIDI_CC_MONO_MODE_ON:
            return("Mono Mode On");
		case MIDI_CC_POLY_MODE_ON:
            return("Poly Mode On");
    }

    return("Unknown");
}

const char *midi_rpn_to_string(unsigned short rpn) {
    switch(rpn) {
		case MIDI_RPN_PITCH_BEND_SENSITIVITY:
            return("Pitch Bend Sensitivity");
		case MIDI_RPN_CHANNEL_FINE_TUNING:
            return("Channel Fine Tuning");
		case MIDI_RPN_CHANNEL_COARSE_TUNING:
            return("Channel Coarse Tuning");
		case MIDI_RPN_TUNING_PROGRAM_CHANGE:
            return("Tuning Program Change");
		case MIDI_RPN_TUNING_BANK_SELECT:
            return("Tuning Bank Select");
		case MIDI_RPN_MODULATION_DEPTH_CHANGE:
            return("Modulation Depth Change");
		case MIDI_RPN_MPE_CONFIGURATION_MESSAGE:
            return("MPE Configuration Message");
		case MIDI_RPN_3D_AZIMUTH:
            return("3D Controller Azimuth Angle");
		case MIDI_RPN_3D_ELEVATION:
            return("3D Controller Elevation");
		case MIDI_RPN_3D_GAIN:
            return("3D Controller Gain");
		case MIDI_RPN_3D_DISTANCE_RATIO:
            return("3D Controller Distance Ratio");
		case MIDI_RPN_3D_MAXIMUM_DISTANCE:
            return("3D Controller Maximum Distance");
		case MIDI_RPN_3D_GAIN_AT_MAX_DISTANCE:
            return("3D Controller Gain at Maximum Distance");
		case MIDI_RPN_3D_REFERENCE_DISTANCE_RATIO:
            return("3D Controller Reference Distance Ratio");
		case MIDI_RPN_3D_PAN_SPREAD_ANGLE:
            return("3D Controller Pan Spread Angle");
		case MIDI_RPN_3D_ROLL_ANGLE:
            return("3D Controller Roll Angle");
		case MIDI_RPN_NULL:
            return("Null Value");
    }

    return("Unknown");
}

int midi_parse_rpn(unsigned char channel, unsigned short rpn, unsigned short data) {
    switch(rpn) {
        case MIDI_RPN_PITCH_BEND_SENSITIVITY:
            term_print("Channel %hhd pitchbend sensitivity is now %hd cents.",
                       channel, MIDI_2BYTE_WORD_HIGH(data) * 100 + MIDI_2BYTE_WORD_LOW(data));
            return(0);
        case MIDI_RPN_CHANNEL_FINE_TUNING:
            term_print("Channel %hhd fine tuning is now %f cents.",
                       channel, ((float)data / MIDI_2BYTE_WORD_MAX * 200.0) - 100.0);
            return(0);
        case MIDI_RPN_CHANNEL_COARSE_TUNING:
            term_print("Channel %hhd coarse tuning is now %f cents.",
                       channel, ((float)data / MIDI_2BYTE_WORD_MAX * 12700.0) - 6400.0);
            return(0);
        case MIDI_RPN_MPE_CONFIGURATION_MESSAGE:
            if(channel == 0) {
                term_print("MPE channel range is %hd.",
                           MIDI_2BYTE_WORD_HIGH(data));
                return(0);
            }
    }

    return(-1);
}
