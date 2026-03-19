CC=gcc-15
TARGET=docs-omp
CCFLAGS=-O2 -fopenmp

all: $(TARGET)

warn: CCFLAGS += -Wall -Wextra
warn: $(TARGET)

$(TARGET):
	$(CC) $(CCFLAGS) -o $(TARGET) $(TARGET).c
