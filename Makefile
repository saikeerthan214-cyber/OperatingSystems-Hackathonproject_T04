CC     = gcc
CFLAGS = -Wall -Wextra -g -fsanitize=address,undefined -Iinclude
# Suppress the known false-positive LeakSanitizer warnings from swapcontext
export ASAN_OPTIONS=detect_leaks=0
export ASAN_OPTIONS
SRC    = src/fiber.c

.PHONY: all clean run run_m1 run_m2 run_m3

all: test_basic test_m1 test_m2 test_m3

test_basic: $(SRC) tests/test_basic.c
	$(CC) $(CFLAGS) $^ -o $@

test_m1: $(SRC) tests/test_m1.c
	$(CC) $(CFLAGS) $^ -o $@

test_m2: $(SRC) tests/test_m2.c
	$(CC) $(CFLAGS) $^ -o $@

test_m3: $(SRC) tests/test_m3.c
	$(CC) $(CFLAGS) $^ -o $@

run: test_basic
	./test_basic

run_m1: test_m1
	./test_m1

run_m2: test_m2
	./test_m2

run_m3: test_m3
	./test_m3

clean:
	rm -f test_basic test_m1 test_m2 test_m3
