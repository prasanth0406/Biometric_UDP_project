CC ?= gcc
ifeq ($(wildcard C:/mingw64/bin/gcc.exe),C:/mingw64/bin/gcc.exe)
  CC = C:/mingw64/bin/gcc.exe
endif

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Wno-unused-but-set-variable -O2 -Isrc
LDFLAGS ?= 
PCAP_LIBS ?= -lpcap

ifeq ($(OS),Windows_NT)
  LDFLAGS += -lws2_32
endif

SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

COMMON_SRCS = $(SRC_DIR)/crc32.c $(SRC_DIR)/sha256.c $(SRC_DIR)/net_util.c $(SRC_DIR)/protocol.c $(SRC_DIR)/log.c
COMMON_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(COMMON_SRCS))

HAVE_PCAP := $(shell printf '\#include <pcap.h>\nint main(){return 0;}\n' | $(CC) -x c - -o /dev/null $(PCAP_LIBS) >/dev/null 2>&1 && echo yes || echo no)

ifeq ($(HAVE_PCAP),yes)
TARGETS = $(BIN_DIR)/sender $(BIN_DIR)/receiver $(BIN_DIR)/proxy $(BIN_DIR)/capture $(BIN_DIR)/test_runner
else
TARGETS = $(BIN_DIR)/sender $(BIN_DIR)/receiver $(BIN_DIR)/proxy $(BIN_DIR)/test_runner
endif

.PHONY: all debug test bench clean distclean

all: $(TARGETS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/sender: $(BUILD_DIR)/sender.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/receiver: $(BUILD_DIR)/receiver.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/proxy: $(BUILD_DIR)/proxy.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/capture: $(BUILD_DIR)/capture.o | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(PCAP_LIBS)

$(BIN_DIR)/test_runner: $(BUILD_DIR)/test_runner.o $(COMMON_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_runner.o: $(TEST_DIR)/test_runner.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

debug: CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -g -fsanitize=address,undefined -Isrc
debug:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	$(MAKE) all

test: all
	@echo "Running C Unit Tests..."
	./$(BIN_DIR)/test_runner
	@echo "Running End-to-End Localhost Tests..."
	bash scripts/test_e2e.sh

bench: all
	@echo "Running Benchmark Suite..."
	bash scripts/bench.sh

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) received_files/*

distclean: clean
	rm -rf logs/*.log logs/*.csv logs/*.pcap results/*.csv results/*.png
