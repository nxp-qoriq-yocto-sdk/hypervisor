
HOSTCC=gcc
HOSTCC_OPTS=-g -std=gnu99

HOSTCC_OPTS_C= -Wall \
  -Wundef \
  -Wstrict-prototypes \
  -Wno-trigraphs \
  -fno-strict-aliasing \
  -fno-common \
  -O2 \

all: mux_server

mux_server: mux_server.c
	$(HOSTCC) $(HOSTCC_OPTS) $(HOSTCC_OPTS_C) -lpthread -o $@ $<

clean:
	rm -f mux_server
