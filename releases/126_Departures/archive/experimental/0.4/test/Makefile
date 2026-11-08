CXX = c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I../src -fsanitize=address,undefined -fno-sanitize-recover=all

all: check
host_tests: host_tests.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
render: render.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
audio_input: audio_input.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
phrasing: phrasing.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
destinations: destinations.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
check: host_tests audio_input phrasing destinations
	./host_tests
	./audio_input
	./phrasing
	./destinations
preview: render
	mkdir -p out
	./render out/Departures_0.4.0-experimental.wav 180
.PHONY: all check preview
