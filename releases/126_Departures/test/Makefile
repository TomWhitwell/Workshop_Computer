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
scales: scales.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
palettes: palettes.cpp ../src/Departures.h
	$(CXX) $(CXXFLAGS) $< -o $@
check: host_tests audio_input phrasing destinations scales palettes
	./host_tests
	./audio_input
	./phrasing
	./destinations
	./scales
	./palettes
preview: render
	mkdir -p out
	./render out/Departures_0.5.0.wav 180
	./render out/Departures_0.5.0_wood.wav 180 2300 2048 wood
.PHONY: all check preview
