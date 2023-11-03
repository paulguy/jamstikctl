typedef struct {
    int note;
    int velocity;
    int bend;
    int expression;
} GuitarString;

typedef struct {
    int MPEOn;
    int singleChannelMode;
    int firstStringChannel;
    int bendRangeSemitones;
    int bendRangeCents;
    GuitarString string[6];
} GuitarState;

GuitarState *guitar_init();
void guitar_set_single_channel_mode(GuitarState *g, int single);
void guitar_set_mpe_mode(GuitarState *g, int MPEOn);
void guitar_set_channel(GuitarState *g, int channel);
void guitar_set_bend_semitones(GuitarState *g, int semitones);
void guitar_set_bend_cents(GuitarState *g, int cents);
void guitar_note_on(GuitarState *g, int channel, int note, int velocity);
void guitar_note_off(GuitarState *g, int channel, int note, int velocity);
void guitar_bend(GuitarState *g, int channel, int bend);
void guitar_set_expression_lsb(GuitarState *g, int channel, int value);
void guitar_set_expression_msb(GuitarState *g, int channel, int value);
