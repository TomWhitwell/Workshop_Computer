CXX = c++
CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra -Werror -DTEMPERANCE_HOST_TEST -I../src -I../vendor -fsanitize=address,undefined -fno-sanitize-recover=all
GATE_BIN ?= /tmp/temperance-flash-gate-tests
TEST_BIN ?= /tmp/temperance-16mb-flash-tests

test:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) flash_tests.cpp ../src/FlashStore.cpp -o $(TEST_BIN)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) gate_tests.cpp ../src/FlashStore.cpp -o $(GATE_BIN)
	$(GATE_BIN) 2
	$(GATE_BIN) 16
	$(TEST_BIN) 2
	$(TEST_BIN) 16
	$(TEST_BIN) 2 bad-range
	$(TEST_BIN) 16 bad-range
	$(TEST_BIN) 2 end-range 0
	$(TEST_BIN) 2 end-range 1
	$(TEST_BIN) 16 end-range 0
	$(TEST_BIN) 16 end-range 1

.PHONY: test
