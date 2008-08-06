#!/bin/sh

../../tools/mux_server/mux_server localhost:9124 9000 9001 9002 9003 9004 9005 9006 9007 &
xterm -e "powerpc-unknown-linux-gnu-gdb-6.8.50.20080507 -x gdb0" &
xterm -e "powerpc-unknown-linux-gnu-gdb-6.8.50.20080507 -x gdb1" &
xterm -e "powerpc-unknown-linux-gnu-gdb-6.8.50.20080507 -x gdb2" &
xterm -e "powerpc-unknown-linux-gnu-gdb-6.8.50.20080507 -x gdb3" &
