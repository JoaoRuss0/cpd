export OMPI_CC=gcc-15
CC=mpicc
TARGET=docs-mpi
CFLAGS=-O2 -fopenmp

all: clean $(TARGET)

warn: CFLAGS += -Wall -Wextra
warn: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c

clean:
	rm $(TARGET)
