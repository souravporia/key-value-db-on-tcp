CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -I./include
LDFLAGS = -lstdc++fs

SRC = main.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = blinkdb-server

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) kvstore.dat

.PHONY: all clean
