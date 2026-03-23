CC = clang
COMMON_FLAGS = -std=c99 -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wformat=2 -fstack-protector-strong -Werror

SRC = server.c
BIN_DIR = bin
TARGET = $(BIN_DIR)/server

all: dev run

dev: $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(COMMON_FLAGS) -g -O0 $(SRC) -o $(TARGET)

prod: $(SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(COMMON_FLAGS) -O3 $(SRC) -o $(TARGET)

run:
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all dev prod run clean
