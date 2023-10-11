#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

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
    pthread_t pid;

    jack_port_t *in;
    jack_port_t *out;

    char *this_inport_name;
    char *this_outport_name;

    char *guitar_inport_name;
    char *guitar_outport_name;

    EventRB inEv, outEv;

    struct sigaction ohup;
    struct sigaction oint;
    struct sigaction oterm;
    struct sigaction ousr1;
} ThreadCTX;

/* must be global so signal handlers work. */
ThreadCTX tctx;

void print_hex(size_t size, unsigned char *buffer) {
    unsigned int i;

    for(i = 0; i < size; i++) {
        printf("%02X ", buffer[i]);
        if(buffer[i] >= ' ' && buffer[i] <= '~') {
            printf("%c ", buffer[i]);
        }
    }
    printf("\n");
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
    return(tctx.activated);
}

void midi_cleanup() {
    if(tctx.activated) {
        if(jack_deactivate(tctx.jack)) {
            fprintf(stderr, "Failed to deactivate JACK client.\n");
        } else {
            fprintf(stderr, "JACK client deactivated.\n");
        }
    }

    tctx.activated = 0;

    if(jack_client_close(tctx.jack)) {
        fprintf(stderr, "Error closing JACK connection.\n");
    } else {
        fprintf(stderr, "JACK connection closed.\n");
    }

    if(sigaction(SIGHUP, &(tctx.ohup), NULL) != 0 ||
       sigaction(SIGINT, &(tctx.oint), NULL) != 0 ||
       sigaction(SIGTERM, &(tctx.oterm), NULL) != 0) {
        fprintf(stderr, "Failed to set signal handler.\n");
    }
    if(sigaction(SIGUSR1, &(tctx.ousr1), NULL) != 0) {
        fprintf(stderr, "Failed to reset signal handlers.\n");
    }

    if(tctx.this_inport_name != NULL) {
        free(tctx.this_inport_name);
        tctx.this_inport_name = NULL;
    }
    if(tctx.this_outport_name != NULL) {
        free(tctx.this_outport_name);
        tctx.this_outport_name = NULL;
    }
    if(tctx.guitar_inport_name != NULL) {
        free(tctx.guitar_inport_name);
        tctx.guitar_inport_name = NULL;
    }
    if(tctx.guitar_outport_name != NULL) {
        free(tctx.guitar_outport_name);
        tctx.guitar_outport_name = NULL;
    }

    if(tctx.inEv.rb != NULL) {
        jack_ringbuffer_free(tctx.inEv.rb);
    }

    if(tctx.outEv.rb != NULL) {
        jack_ringbuffer_free(tctx.outEv.rb);
    }
}

static void _midi_cleanup_handler(int signum) {
    midi_cleanup();
    if(signum == SIGHUP && tctx.ohup.sa_handler != NULL) {
        tctx.ohup.sa_handler(signum);
    } else if(signum == SIGINT && tctx.oint.sa_handler != NULL) {
        tctx.oint.sa_handler(signum);
    } else if(signum == SIGTERM && tctx.oterm.sa_handler != NULL) {
        tctx.oterm.sa_handler(signum);
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

int _midi_add_event(EventRB *e, size_t size, unsigned char *buf) {
    midi_event *ev;

    if(jack_ringbuffer_write_space(e->rb) < sizeof(midi_event *)) {
        return(-1);
    }

    ev = &(e->event[e->next_event]);
    if(e->sysex) {
        ev->size += size;
        memcpy(&(ev->buffer[e->sysex]), buf, size);
        if(buf[size-1] == 0xF7) {
            /* if the end of the sysex command is found, it's done */
            /* maybe need to check for 0x55 or packet size? */
            e->sysex = 0;
        } else {
            /* advance pointer */
            e->sysex += size;
            /* indicate success, but a full packet hasn't been received */
            return(1);
        }
    } else {
        ev->size = size;
        memcpy(ev->buffer, buf, size);
        if(buf[0] == 0xF0 && buf[size-1] != 0xF7) {
            /* if this is part of a sysex, indicate next time to continue ingesting */
            e->sysex = size;
            /* return now */
            return(1);
        }
    }

    if(jack_ringbuffer_write(e->rb, (char *)(&ev), sizeof(midi_event *)) < sizeof(midi_event *)) {
        return(-1);
    }

    e->next_event += 1;
    if(e->next_event == MIDI_MAX_EVENTS) {
        e->next_event = 0;
    }

    return(0);
}

/* simple function to just transfer data through */
int _midi_process(jack_nframes_t nframes, void *arg) {
    jack_midi_event_t jackEvent;
    midi_event *event;

    char *in;
    char *out;
    uint32_t i;
    int has_output = 0;
    int retval;

    /* process queued up input events */
    in = jack_port_get_buffer(tctx.in, nframes);
    for(i = 0;; i++) {
        if(jack_midi_event_get(&jackEvent, in, i)) {
            break;
        }
        retval = _midi_add_event(&(tctx.inEv), jackEvent.size, jackEvent.buffer);

        if(retval < 0) {
            printf("Failed to add event.\n");
            return(-1);
        }else if(retval == 0) {
            has_output = 1;
        } /* positive values indicate a partial sysex packet that is being consumed but not fully received */
    }

    /* process queued up output events */
    out = jack_port_get_buffer(tctx.out, nframes);
    jack_midi_clear_buffer(out);
    off_t offset = 0;
    while(offset < nframes) {
        event = _midi_get_event(&(tctx.outEv));
        if(event == NULL) {
            break;
        }

        if(tctx.outEv.sysex) {
            /* if actively sending out a packet, continue */
            size_t to_write = event->size - tctx.outEv.sysex;
            to_write = nframes < to_write ? nframes : to_write;
            if(jack_midi_event_write(out, 0,
                                     &(event->buffer[tctx.outEv.sysex]),
                                     to_write)) {
                printf("Failed to write event.\n");
                return(-3);
            }
            tctx.outEv.sysex += to_write;

            /* if this is the end of the packet, indicate done-ness */
            if(tctx.outEv.sysex >= event->size) {
                tctx.outEv.sysex = 0;
                offset += to_write;
            } else {
                /* don't consume if not done */
                break;
            }
        } else {
            if(offset + event->size > nframes) {
                /* if the packet doesn't fit, send just enough to fill */
                tctx.outEv.sysex = nframes - offset;
                if(jack_midi_event_write(out, offset,
                                         event->buffer,
                                         nframes - offset)) {
                    printf("Failed to write event.\n");
                    return(-3);
                }
                /* don't consume the packet that hasn't fully sent */
                break;
            } else {
                if(jack_midi_event_write(out, offset,
                                         event->buffer,
                                         event->size)) {
                    printf("Failed to write event.\n");
                    return(-3);
                }
                offset += event->size;
            }
        }

        _midi_consume_event(&(tctx.outEv));
    }

    if(has_output != 0) {
        pthread_kill(tctx.pid, SIGUSR1);
    }

    return(0);
}

int midi_write_event(size_t size, unsigned char *buffer) {
    /* if the device closed in another thread, don't try to do anything */
    if(!tctx.activated) {
        return(-1);
    }

    return(_midi_add_event(&(tctx.outEv), size, buffer));
}

int midi_read_event(size_t size, unsigned char *buffer) {
    midi_event *ev;
    size_t evsize;

    if(!tctx.activated) {
        return(0);
    }

    ev = _midi_get_event(&(tctx.inEv));
    if(ev == NULL) {
        return(0);
    }
    if(ev->size > size) {
        return(ev->size);
    }

    memcpy(buffer, ev->buffer, ev->size);
    evsize = ev->size;

    _midi_consume_event(&(tctx.inEv));

    return(evsize);
}

void _midi_print_ports() {
    const char **search;
    unsigned int i;
    jack_port_t *port;

    search = jack_get_ports(tctx.jack, NULL, NULL, 0);
    for(i = 0; search[i] != NULL; i++) {
        port = jack_port_by_name(tctx.jack, search[i]);
        fprintf(stderr, "%s %02X %s\n", search[i], jack_port_flags(port), jack_port_type(port));
    }
    jack_free(search);
}

void _midi_port_connect_cb(jack_port_id_t a, jack_port_id_t b, int connect, void *arg) {
    const char *namea, *nameb;
    int is_inport = 0;
    int is_outport = 0;

    namea = jack_port_name(jack_port_by_id(tctx.jack, a));
    nameb = jack_port_name(jack_port_by_id(tctx.jack, b));

    if(tctx.guitar_outport_name != NULL) {
        if(strcmp(namea, tctx.this_inport_name) == 0) {
            is_inport = 1;
        }else if(strcmp(nameb, tctx.this_inport_name) == 0) {
            is_inport = 2;
        }

        if(is_inport == 1 &&
           strcmp(nameb, tctx.guitar_outport_name) == 0) {
            if(connect) {
                tctx.ready |= (1 << 0);
            } else {
                tctx.ready &= ~(1 << 0);
            }
            goto connected;
        }else if(is_inport == 2 &&
                 strcmp(namea, tctx.guitar_outport_name) == 0) {
            if(connect) {
                tctx.ready |= (1 << 0);
            } else {
                tctx.ready &= ~(1 << 0);
            }
            goto connected;
        }
    }

    if(tctx.guitar_outport_name != NULL) {
        if(strcmp(namea, tctx.this_outport_name) == 0) {
            is_outport = 1;
        }else if(strcmp(nameb, tctx.this_outport_name) == 0) {
            is_outport = 2;
        }

        if(is_outport == 1 &&
           strcmp(nameb, tctx.guitar_inport_name) == 0) {
            if(connect) {
                tctx.ready |= (1 << 1);
            } else {
                tctx.ready &= ~(1 << 1);
            }
            goto connected;
        }else if(is_outport == 2 &&
                 strcmp(namea, tctx.guitar_inport_name) == 0) {
            if(connect) {
                tctx.ready |= (1 << 1);
            } else {
                tctx.ready &= ~(1 << 1);
            }
            goto connected;
        }
    }

    if(connect) {
        if(is_inport != 0) {
            fprintf(stderr, "No, that wasn't right, connect\n%s\nto\n%s\n",
                    tctx.guitar_outport_name, tctx.this_inport_name);
        }
        if(is_outport != 0) {
            fprintf(stderr, "No, that wasn't right, connect\n%s\nto\n%s\n",
                    tctx.this_outport_name, tctx.guitar_inport_name);
        }
        return;
    }

connected:
    if(tctx.ready == (1 << 0) || tctx.ready == (1 << 1)) {
        fprintf(stderr, "1 connection remaining\n");
    } else if(tctx.ready == ((1 << 0) | (1 << 1))) {
        fprintf(stderr, "sequence complete\n");
    }

    pthread_kill(tctx.pid, SIGUSR1);
}

int midi_ready() {
    return(tctx.ready == ((1 << 0) | (1 << 1)));
}

int midi_setup(const char *client_name,
               const char *inport_name, const char *outport_name,
               pthread_t pid) {
    /* jack stuff */
    jack_status_t jstatus;
    struct sigaction sa;
    sa.sa_handler = _midi_cleanup_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    tctx.activated = 0;
    tctx.ready = 0;
    tctx.pid = pid;
    tctx.this_inport_name = NULL;
    tctx.this_outport_name = NULL;
    tctx.guitar_inport_name = NULL;
    tctx.guitar_outport_name = NULL;
    tctx.inEv.next_event = 0;
    tctx.inEv.rb = NULL;
    tctx.inEv.sysex = 0;
    tctx.outEv.next_event = 0;
    tctx.outEv.rb = NULL;
    tctx.outEv.sysex = 0;

    tctx.jack = jack_client_open(client_name, JackNoStartServer, &jstatus);
    if(tctx.jack == NULL) {
        fprintf(stderr, "Failed to open JACK connection.\n");
        return(-1);
    }

    if(sigaction(SIGHUP, &sa, &(tctx.ohup)) != 0 ||
       sigaction(SIGINT, &sa, &(tctx.oint)) != 0 ||
       sigaction(SIGTERM, &sa, &(tctx.oterm)) != 0) {
        fprintf(stderr, "Failed to set signal handler.\n");
    }
    sa.sa_handler = _midi_usr1_handler;
    if(sigaction(SIGUSR1, &sa, &(tctx.ousr1)) != 0) {
        fprintf(stderr, "Failed to set signal handler.\n");
    }

    tctx.in = jack_port_register(tctx.jack,
                                 inport_name,
                                 JACK_DEFAULT_MIDI_TYPE,
                                 JackPortIsInput,
                                 0);
    if(tctx.in == NULL) {
        fprintf(stderr, "Failed to register in port.\n");
        midi_cleanup();
        return(-1);
    }
    tctx.this_inport_name = midi_copy_string(jack_port_name(tctx.in));
    if(tctx.this_inport_name == NULL) {
        midi_cleanup();
        return(-1);
    }

    tctx.out = jack_port_register(tctx.jack,
                                  outport_name,
                                  JACK_DEFAULT_MIDI_TYPE,
                                  JackPortIsOutput,
                                  0);
    if(tctx.out == NULL) {
        fprintf(stderr, "Failed to register out port.\n");
        midi_cleanup();
        return(-1);
    }
    tctx.this_outport_name = midi_copy_string(jack_port_name(tctx.out));
    if(tctx.this_outport_name == NULL) {
        midi_cleanup();
        return(-1);
    }

    tctx.inEv.rb = jack_ringbuffer_create(sizeof(midi_event *) * MIDI_MAX_EVENTS);
    if(tctx.inEv.rb == NULL) {
        fprintf(stderr, "Failed to create input ringbuffer.\n");
        midi_cleanup();
        return(-1);
    }

    tctx.outEv.rb = jack_ringbuffer_create(sizeof(midi_event *) * MIDI_MAX_EVENTS);
    if(tctx.outEv.rb == NULL) {
        fprintf(stderr, "Failed to create output ringbuffer.\n");
        midi_cleanup();
        return(-1);
    }

    if(jack_set_port_connect_callback(tctx.jack, _midi_port_connect_cb, NULL)) {
        fprintf(stderr, "Failed to set JACK port connect callback.\n");
        midi_cleanup();
        return(-1);
    }

    if(jack_set_process_callback(tctx.jack, _midi_process, NULL)) {
        fprintf(stderr, "Failed to set JACK process callback.\n");
        midi_cleanup();
        return(-1);
    }

    if(jack_activate(tctx.jack)) {
        fprintf(stderr, "Failed to activate JACK client.\n");
        midi_cleanup();
        return(-1);
    }
    tctx.activated = 1;

    return(0);
}

char *midi_find_port(const char *pattern, unsigned long flags) {
    const char **search;
    char *name;

    search = jack_get_ports(tctx.jack, pattern, NULL, flags);
    if(search == NULL || search[0] == NULL) {
        fprintf(stderr, "No ports found for criteria.\n");
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

    err = jack_connect(tctx.jack, src, dst);
    if(err == 0) {
        return(0);
    }

    fprintf(stderr, "jack_connect() returned error %d (%s)\n", err, strerror(err));

    if(tctx.jack == NULL) {
        fprintf(stderr, "Jack client is NULL.\n");
        return(err);
    }
    if(src == NULL) {
        fprintf(stderr, "Source port name is NULL.\n");
        return(err);
    }
    if(dst == NULL) {
        fprintf(stderr, "Destination port name is NULL.\n");
        return(err);
    }
    srcport = jack_port_by_name(tctx.jack, src);
    if(srcport == NULL) {
        fprintf(stderr, "Got NULL source port.\n");
        return(err);
    }
    dstport = jack_port_by_name(tctx.jack, dst);
    if(dstport == NULL) {
        fprintf(stderr, "Got NULL destination port.\n");
        return(err);
    }
    srcflags = jack_port_flags(srcport);
    if(!(srcflags & JackPortIsOutput)) {
        fprintf(stderr, "Source port isn't an output. Flags: %02X\n", srcflags);
        return(err);
    }
    dstflags = jack_port_flags(dstport);
    if(!(dstflags & JackPortIsInput)) {
        fprintf(stderr, "Destination port isn't an input. Flags: %02X\n", dstflags);
        return(err);
    }
    srctype = jack_port_type(srcport);
    dsttype = jack_port_type(dstport);
    if(strcmp(srctype, dsttype) != 0) {
        fprintf(stderr, "Different source and destination port types. %s != %s", srctype, dsttype);
        return(err);
    }

    fprintf(stderr, "Unknown error. %s 0x%02X %s, %s 0x%02X %s\n", src, srcflags, srctype, dst, dstflags, dsttype);

    return(err);
}

int midi_attach_in_port_by_name(const char *name) {
    tctx.guitar_outport_name = midi_copy_string(name);
    if(tctx.guitar_outport_name == NULL) {
        return(-1);
    }

    /* source out to this in */
    if(_midi_connect(tctx.guitar_outport_name, tctx.this_inport_name) != 0) {
        return(-1);
    }

    return(0);
}

int midi_attach_out_port_by_name(const char *name) {
    tctx.guitar_inport_name = midi_copy_string(name);
    if(tctx.guitar_inport_name == NULL) {
        return(-1);
    }

    /* this out to source in */
    if(_midi_connect(tctx.this_outport_name, tctx.guitar_inport_name) != 0) {
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
    int num = (note - 21) % 12;
    int octave = (note - 21) / 12;
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
                if(size < 3) {
                    return(3);
                }
                len = 3;
                buf[0] = NOTE_LOOKUP[num];
                buf[1] = '-';
                buf[2] = OCTAVE_LOOKUP[-octave];
            } else {
                if(size < 2) {
                    return(2);
                }
                len = 2;
                buf[0] = NOTE_LOOKUP[num];
                buf[1] = OCTAVE_LOOKUP[octave];
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
