CXX=/opt/homebrew/bin/g++-15
TARGET=docs-omp
CXXFLAGS=-O2 -std=c++17 -fopenmp

all: clean $(TARGET)

warn: CXXFLAGS += -Wall -Wextra
warn: clean $(TARGET)

$(TARGET):
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(TARGET).cpp

clean:
	rm -f $(TARGET)
