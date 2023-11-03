#include <stdlib.h>
#include <stdio.h>

#include "guitar.h"
#include "midi.h"
#include "terminal.h"

typedef enum {
    GuitarModeSingleChannel,
    GuitarModeStringPerChannel,
    GuitarModeMPE
} GuitarMode;

#define FIELD_ARRAY_NUM(FIELD) (sizeof(FIELD) / sizeof(FIELD[0]))

static char buffer[MIDI_MAX_BUFFER_SIZE];

void guitar_stop_strings(GuitarState *g) {
    unsigned int i;

    for(i = 0; i < sizeof(g->string) / sizeof(g->string[0]); i++) {
        g->string[i].note = -1;
        g->string[i].velocity = 0;
        g->string[i].bend = 0;
        g->string[i].expression = 0;
    }
}

GuitarState *guitar_init() {
    GuitarState *g;

    g = malloc(sizeof(GuitarState));
    if(g == NULL) {
        fprintf(stderr, "Failed to allocate memory.\n");
        return(NULL);
    }

    /* defaults don't really matter as they should be populated on startup */
    g->MPEOn = 0;
    g->singleChannelMode = 1;
    g->firstStringChannel = 0;
    /* I think it was 48 semitones + 100 cents default? */
    g->bendRangeSemitones = 4800;
    g->bendRangeCents = 100;

    guitar_stop_strings(g);

    return(g);
}

GuitarMode guitar_get_mode(GuitarState *g) {
    if(!g->MPEOn) {
        if(g->singleChannelMode) {
            return(GuitarModeSingleChannel);
        } else {
            return(GuitarModeStringPerChannel);
        }
    }

    return(GuitarModeMPE);
}

const char *guitar_mode_to_string(GuitarMode mode) {
    switch(mode) {
        case GuitarModeSingleChannel:
            return("Single Channel");
        case GuitarModeStringPerChannel:
            return("Multichannel");
        case GuitarModeMPE:
            return("MPE");
        default:
            break;
    }

    return("Unknown");
}

void guitar_print(GuitarState *g) {
    unsigned int i;
    int size;
    int pos = 0;

    const char *mode = guitar_mode_to_string(guitar_get_mode(g));
    const char *note[FIELD_ARRAY_NUM(g->string)];

    for(i = 0; i < FIELD_ARRAY_NUM(g->string); i++) {
        if(g->string[i].note < 0) {
            note[i] = "---";
        } else {
            size = midi_num_to_note(sizeof(buffer) - pos, &(buffer[pos]), g->string[i].note, 0);
            if(size <= 0) {
                size = snprintf(&(buffer[pos]), sizeof(buffer) - pos, "?%d", g->string[i].note);
            } else {
                buffer[pos+size] = '\0';
                note[i] = &(buffer[pos]);
                pos += size + 1;
            }
        }
    }

    term_print_static("Mode: %s\n"
                      "1 Nt: %s  Vl: %d  Bd: %d  Ex: %d\n"
                      "2 Nt: %s  Vl: %d  Bd: %d  Ex: %d\n"
                      "3 Nt: %s  Vl: %d  Bd: %d  Ex: %d\n"
                      "4 Nt: %s  Vl: %d  Bd: %d  Ex: %d\n"
                      "5 Nt: %s  Vl: %d  Bd: %d  Ex: %d\n"
                      "6 Nt: %s  Vl: %d  Bd: %d  Ex: %d",
                      mode,
                      note[0], g->string[0].velocity, g->string[0].bend, g->string[0].expression,
                      note[1], g->string[1].velocity, g->string[1].bend, g->string[1].expression,
                      note[2], g->string[2].velocity, g->string[2].bend, g->string[2].expression,
                      note[3], g->string[3].velocity, g->string[3].bend, g->string[3].expression,
                      note[4], g->string[4].velocity, g->string[4].bend, g->string[4].expression,
                      note[5], g->string[5].velocity, g->string[5].bend, g->string[5].expression);
}

void guitar_set_single_channel_mode(GuitarState *g, int single) {
    g->singleChannelMode = single;
    if(g->singleChannelMode) {
        term_print("Single channel mode is ON.");
    } else {
        term_print("Single channel mode is OFF (multichannel mode).");
    }
}

void guitar_set_mpe_mode(GuitarState *g, int MPEOn) {
    g->MPEOn = MPEOn;
    if(g->MPEOn) {
        term_print("MPE mode is ON.");
    } else {
        term_print("MPE mode is OFF.");
    }
}

void guitar_set_channel(GuitarState *g, int channel) {
    g->firstStringChannel = channel - 1;
    term_print("First string channel is %d.",
               g->firstStringChannel + 1);
}

void guitar_set_bend_semitones(GuitarState *g, int semitones) {
    if(g->bendRangeSemitones != semitones) {
        g->bendRangeSemitones = semitones;
        if(term_print_mode()) {
            term_print("Bend range is now %d semitones and %d cents.",
                       g->bendRangeSemitones, g->bendRangeCents);
        } else {
            guitar_print(g);
        }
    }
}

void guitar_set_bend_cents(GuitarState *g, int cents) {
    if(g->bendRangeCents != cents) {
        g->bendRangeCents = cents;
        if(term_print_mode()) {
            term_print("Bend range is now %d semitones and %d cents.",
                       g->bendRangeSemitones, g->bendRangeCents);
        } else {
            guitar_print(g);
        }
    }
}

void print_note_simple(GuitarState *g, int channel, int note, int velocity, int on) {
    const char *note_state = "Off";
    if(on) {
        note_state = "On";
    }

    int size = midi_num_to_note(sizeof(buffer), (char *)buffer, note, 0);
    if(size <= 0) {
        term_print("WARNING: Invalid note number!\n"
                   "Note %s (%d): %d Vel: %d",
                   note_state, channel, note, velocity);
    } else {
        buffer[size] = '\0';
        term_print("Note %s (%d): %s (%d) Vel: %d",
                   note_state, channel, buffer, note, velocity);
    }
}

void guitar_note_on(GuitarState *g, int channel, int note, int velocity) {
    unsigned int i;
    int foundChannel = -1;

    switch(guitar_get_mode(g)) {
        case GuitarModeSingleChannel:
            foundChannel = 0;
            for(i = 0; i < FIELD_ARRAY_NUM(g->string); i++) {
               if(g->string[i].note == -1) {
                  foundChannel = i;
                  break;
               }
            }
            break;
        case GuitarModeStringPerChannel:
            foundChannel = channel - g->firstStringChannel;
            break;
        case GuitarModeMPE:
            foundChannel = channel - 1;
            break;
    }

    if(foundChannel < 0 ||
       (unsigned long)foundChannel > FIELD_ARRAY_NUM(g->string) - 1) {
        print_note_simple(g, channel, note, velocity, 1);
        return;
    }

    g->string[foundChannel].note = note;
    g->string[foundChannel].velocity = velocity;

    if(term_print_mode()) {
        print_note_simple(g, channel, note, velocity, 1);
    } else {
        guitar_print(g);
    }
}

int guitar_find_channel(GuitarState *g, int channel, int note) {
    unsigned int i;
    int foundChannel = -1;

    switch(guitar_get_mode(g)) {
        case GuitarModeSingleChannel:
            foundChannel = 0;
            for(i = 0; i < FIELD_ARRAY_NUM(g->string); i++) {
               if(g->string[i].note == note) {
                  foundChannel = i;
                  break;
               }
            }
            break;
        case GuitarModeStringPerChannel:
            foundChannel = channel - g->firstStringChannel;
            break;
        case GuitarModeMPE:
            foundChannel = channel - 1;
            break;
    }

    return(foundChannel);
}

void guitar_note_off(GuitarState *g, int channel, int note, int velocity) {
    int foundChannel = guitar_find_channel(g, channel, note);
    if(foundChannel < 0 ||
       (unsigned long)foundChannel > FIELD_ARRAY_NUM(g->string) - 1) {
        print_note_simple(g, channel, note, velocity, 0);
        return;
    }

    g->string[foundChannel].note = -1;
    g->string[foundChannel].velocity = velocity;
    g->string[foundChannel].bend = 0;
    g->string[foundChannel].expression = 0;

    if(term_print_mode()) {
        print_note_simple(g, channel, note, velocity, 0);
    } else {
        guitar_print(g);
    }
}

int guitar_calc_bend(GuitarState *g, int bend) {
    return((int)((long long int)bend *
                 MIDI_CMD_PITCHBEND_OFFSET /
                 ((long long int)(g->bendRangeSemitones) * 100 + (long long int)(g->bendRangeCents))));
}

void guitar_bend(GuitarState *g, int channel, int bend) {
    int foundChannel = guitar_find_channel(g, channel, -1);
    if(foundChannel < 0 ||
       (unsigned long)foundChannel > FIELD_ARRAY_NUM(g->string) - 1) {
        term_print("Got invalid string channel %d for bend of %d!",
                   channel, bend);
        return;
    }

    /* do this calculation on probably overly large variables to not lose
     * precision */
    g->string[foundChannel].bend = guitar_calc_bend(g, bend);

    if(term_print_mode()) {
        term_print("Pitch bend (%d): %d", foundChannel, bend);
    } else {
        guitar_print(g);
    }
}

void guitar_set_expression_lsb(GuitarState *g, int channel, int value) {
    int foundChannel = guitar_find_channel(g, channel, -1);
    if(foundChannel < 0 ||
       (unsigned long)foundChannel > FIELD_ARRAY_NUM(g->string) - 1) {
        term_print("Got invalid string channel %d for expression LSB of %d!",
                   channel, value);
        return;
    }

    g->string[foundChannel].expression =
        (g->string[foundChannel].expression & 0xFF00) |
        (value & 0x00FF);

    /* LSB seems to always indicate a change ? */
    if(term_print_mode()) {
        term_print("Expression (%d): %d", foundChannel, value);
    } else {
        guitar_print(g);
    }
}

void guitar_set_expression_msb(GuitarState *g, int channel, int value) {
    int foundChannel = guitar_find_channel(g, channel, -1);
    if(foundChannel < 0 ||
       (unsigned long)foundChannel > FIELD_ARRAY_NUM(g->string) - 1) {
        term_print("Got invalid string channel %d for expression MSB of %d!",
                   channel, value);
        return;
    }

    g->string[foundChannel].expression =
        (g->string[foundChannel].expression & 0x00FF) |
        ((value & 0x00FF) << 16);
}
