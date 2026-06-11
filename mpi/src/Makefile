export OMPI_CC=gcc-15
CC=mpicc
TARGETS=docs-mpi-checker docs-mpi-row
CFLAGS=-O2 -fopenmp

.PHONY: all warn clean

all: $(TARGETS)

warn: CFLAGS += -Wall -Wextra
warn: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
