CC=gcc-15
TARGET=docs
CFLAGS=-O2 -fopenmp

all: $(TARGET)

warn: CFLAGS += -Wall -Wextra
warn: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c
