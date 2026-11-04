CXX = c++
CXXFLAGS = -std=c++17 -O2 -g -Wall -Wextra -Werror -DTEMPERANCE_HOST_TEST -I../src -I../vendor -fsanitize=address,undefined -fno-sanitize-recover=all
TEST_BIN ?= /tmp/temperance-16mb-flash-tests

test:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) flash_tests.cpp ../src/FlashStore.cpp -o $(TEST_BIN)
	$(TEST_BIN)

.PHONY: test
