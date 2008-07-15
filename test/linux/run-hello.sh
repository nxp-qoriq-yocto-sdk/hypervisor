#!/bin/sh

../../tools/mux_server/mux_server localhost:9124 9000 &
xterm -e "telnet localhost 9000" &
#simics test/linux/hv-hello.simics -e
