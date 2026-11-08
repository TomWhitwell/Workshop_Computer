CXX = c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I../src -fsanitize=address,undefined -fno-sanitize-recover=all

all: check
host_tests: host_tests.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
render: render.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
check: host_tests
	./host_tests
preview: render
	mkdir -p out
	./render out/Departures_0.1.0.wav 90
.PHONY: all check preview
