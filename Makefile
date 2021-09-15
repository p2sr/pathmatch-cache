.PHONY: all clean

CC=gcc
CFLAGS=-m32 -shared -Wall -Werror -O2

-include config.mk

all: pathmatch_cache.so

pathmatch_cache.so: pathmatch_cache.c
	$(CC) $^ $(CFLAGS) -o $@
