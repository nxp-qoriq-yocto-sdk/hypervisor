#!/bin/sh

../../tools/mux_server/mux_server localhost:9124 9000 9001 9002 9003 &

xterm -e "${CROSS_COMPILE}gdb -x gdb0-C" &
xterm -e "${CROSS_COMPILE}gdb -x gdb1-C" &
xterm -e "${CROSS_COMPILE}gdb -x gdb2-C" &
xterm -e "${CROSS_COMPILE}gdb -x gdb3-C" &
