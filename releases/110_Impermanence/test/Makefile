# Host (computer) build of the instrument, for tests and offline renders.
#   make -C test          build and run the tests
#   make -C test render   write example WAVs to test/out/
CXX ?= c++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -I../src -fsanitize=undefined -fno-sanitize-recover=undefined

all: test

host_tests: host_tests.cpp Sim.h ../src/*.h
	$(CXX) $(CXXFLAGS) -o $@ host_tests.cpp

render_bin: render.cpp Sim.h ../src/*.h
	$(CXX) $(CXXFLAGS) -o $@ render.cpp

test: host_tests
	./host_tests

render: render_bin
	mkdir -p out
	./render_bin out

clean:
	rm -rf host_tests render_bin out

.PHONY: all test render clean
