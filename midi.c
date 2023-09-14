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
