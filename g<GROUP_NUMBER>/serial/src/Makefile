CC=gcc
TARGET=docs
CFLAGS=-O2

all: clean $(TARGET)

warn: CFLAGS += -Wall -Wextra
warn: clean $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c

clean:
	rm -f $(TARGET)
