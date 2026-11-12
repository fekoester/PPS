CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
CPPFLAGS += -Iinclude

.PHONY: all headless test clean

# Viewer + headless runner (needs SFML: sudo apt install libsfml-dev)
all: PPS

PPS: main.cpp include/pps.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -DPPS_WITH_SFML main.cpp -o PPS -lsfml-graphics -lsfml-window -lsfml-system

# Headless runner only (no SFML needed)
headless: main.cpp include/pps.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) main.cpp -o PPS

tests/test_pps: tests/test_pps.cpp include/pps.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) tests/test_pps.cpp -o tests/test_pps

test: tests/test_pps
	./tests/test_pps

clean:
	rm -f PPS tests/test_pps
