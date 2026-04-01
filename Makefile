CC = clang
CFLAGS = -std=c99 -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wformat=2 -fstack-protector-strong -Werror -x c
LDFLAGS = -lpthread
MACFLAGS = -framework CoreFoundation -framework IOKit

BIN_DIR = bin
PUBLIC_DIR = public
TARGET = $(BIN_DIR)/server

.PHONY: all dev prod run clean
all: dev run

dev:
	@mkdir -p $(BIN_DIR)
	@cp -R $(PUBLIC_DIR) $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(MACFLAGS) -g -O0 server.c -o $(TARGET)

prod:
	@mkdir -p $(BIN_DIR)
	@cp -R $(PUBLIC_DIR) $(BIN_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(MACFLAGS) -O3 server.c -o $(TARGET)

	@# https://stackoverflow.com/a/71011994/16638833
	$(CC) $(CFLAGS) -O3 -c http.h -DHTTP_IMPLEMENTATION -o $(BIN_DIR)/http.o
	ar rcs $(BIN_DIR)/http.a $(BIN_DIR)/http.o
	@rm $(BIN_DIR)/http.o

	$(CC) $(CFLAGS) -O3 -c sys.h -DSYS_IMPLEMENTATION -o $(BIN_DIR)/sys.o
	ar rcs $(BIN_DIR)/sys.a $(BIN_DIR)/sys.o
	@rm $(BIN_DIR)/sys.o

run:
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)
