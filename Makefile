CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
CFLAGS  += -Iinclude $(shell sdl2-config --cflags)
LDFLAGS += $(shell sdl2-config --libs)

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)
BIN := gb-emu

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build/%.o: src/%.c $(wildcard include/*.h) | build
	$(CC) $(CFLAGS) -c $< -o $@

test: build/ppu_test build/rom_test
	./build/ppu_test

build/ppu_test: tests/ppu_test.c $(filter-out build/main.o,$(OBJ)) $(wildcard include/*.h) | build
	$(CC) $(CFLAGS) $(filter-out build/main.o,$(OBJ)) $< -o $@ $(LDFLAGS)

build/rom_test: tests/rom_test.c $(filter-out build/main.o,$(OBJ)) $(wildcard include/*.h) | build
	$(CC) $(CFLAGS) $(filter-out build/main.o,$(OBJ)) $< -o $@ $(LDFLAGS)

build:
	mkdir -p build

clean:
	rm -rf build $(BIN)

.PHONY: all clean test
