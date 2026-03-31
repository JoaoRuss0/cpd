export OMPI_CC=gcc-15
CC=mpicc
TARGETS=docs-mpi-checker docs-mpi-checker-omp docs-mpi-checker-full docs-mpi-row docs-mpi-row-omp
CFLAGS=-O2 -fopenmp

.PHONY: all warn clean

all: $(TARGETS)

warn: CFLAGS += -Wall -Wextra
warn: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)