#!/bin/sh

../../tools/mux_server/mux_server localhost:9124 9000 9001 9002 9003 9004 9005 9006 9007 &
xterm -e "${CROSS_COMPILE}gdb -x gdb0" &
xterm -e "${CROSS_COMPILE}gdb -x gdb1" &
xterm -e "${CROSS_COMPILE}gdb -x gdb2" &
xterm -e "${CROSS_COMPILE}gdb -x gdb3" &
