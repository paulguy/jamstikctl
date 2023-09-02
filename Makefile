OBJS   = json_schema.o midi.o main.o
TARGET = jamstikctl
CFLAGS = -Wall -Wextra -Wno-unused-parameter `pkg-config --cflags json-c` `pkg-config --cflags ncurses` -ggdb 
LDFLAGS = -ljack `pkg-config --libs json-c` `pkg-config --libs ncurses`

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

all: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean
