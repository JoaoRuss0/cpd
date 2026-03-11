CC=/opt/homebrew/bin/g++-15
TARGET=docs-omp
CCFLAGS=-O2 -fopenmp

all: clean $(TARGET)

warn: CCFLAGS += -Wall -Wextra
warn: clean $(TARGET)

$(TARGET):
	$(CC) $(CCFLAGS) -o $(TARGET) $(TARGET).c

clean:
	rm -f $(TARGET)
