CC ?= gcc
CFLAGS = -Wall -Wextra -Werror

.PHONY: all clean default

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket
