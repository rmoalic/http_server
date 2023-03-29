CC=gcc
WARNINGS=-Wall -Wextra -Wmissing-prototypes -Wshadow -Wno-unused-parameter
CFLAGS=-g -O2 -MMD -MP -pedantic $(WARNINGS)

.PHONY: all bin clean

all: bin

bin: http_server

http_server: main.c http.c threadpool.c http_status_code.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f *.o *.d http_server

-include $(wildcard *.d)

