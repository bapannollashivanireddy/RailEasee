CC = gcc
CFLAGS ?= -Wall -Wextra -Wpedantic -std=c11 -O2
CPPFLAGS ?= -Iinclude
LDFLAGS ?=

SOURCES = src/models.c src/hashtable.c src/heap.c src/queue.c src/dll.c \
	src/avl.c src/segtree.c src/stack.c src/engine.c src/probability.c \
	src/storage.c src/ui.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = $(wildcard include/*.h)
TESTS = tests/test_main.c tests/test_hashtable.c tests/test_heap.c \
	tests/test_avl.c tests/test_structures.c tests/test_engine.c
TESTS += tests/test_storage.c

$(OBJECTS) src/main.o: $(HEADERS)

.PHONY: all run test clean

all: railease

railease: $(OBJECTS) src/main.o
	$(CC) $(LDFLAGS) -o $@ $^

run: railease
	./railease

test: $(OBJECTS) $(TESTS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o test_railease $(OBJECTS) $(TESTS)
	./test_railease

clean:
	$(RM) $(OBJECTS) src/main.o railease railease.exe test_railease test_railease.exe
