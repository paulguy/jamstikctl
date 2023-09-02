#include <pthread.h>

#include <jack/jack.h>

#define MIDI_MAX_BUFFER_SIZE (32768) /* should be plenty, I guess */

void print_hex(size_t size, unsigned char *buffer);
char *midi_copy_string(const char *src);

int midi_setup(const char *client_name,
               const char *inport_name, const char *outport_name,
               pthread_t pid,
               void (*original_handler)(void));
char *midi_find_port(const char *pattern, unsigned long flags);
int midi_ready();
void midi_cleanup();
int midi_activated();
int midi_read_event(size_t size, unsigned char *buffer);
int midi_write_event(size_t size, unsigned char *buffer);
int midi_attach_in_port_by_name(const char *name);
int midi_attach_out_port_by_name(const char *name);
