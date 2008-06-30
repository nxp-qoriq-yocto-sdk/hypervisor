CROSS_COMPILE=powerpc-e500mc-linux-gnu-
LIBFDT_DIR := ../../dtc/libfdt
LIBOS_DIR := ../lib
LIBOS_INC := ../include
LIBOS_BIN := bin/libos
LIBOS_CLIENT_H := src/libos-client.h

CC=gcc
CC_OPTS=-g -std=gnu99

CC_OPTS_C= -Wall \
  -Wundef \
  -Wstrict-prototypes \
  -Wno-trigraphs \
  -fno-strict-aliasing \
  -fno-common \
  -O2 \

all: mux_server

mux_server: mux_server.c
	$(CC) $(CC_OPTS) $(CC_OPTS_C) -lpthread -o $@ $<

clean:
	rm -f mux_server
