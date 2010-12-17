#!/usr/bin/env python

#constants used by automation scripts
RMT_SERVER = "ofnir.am.freescale.net" 
RMT_USER = "arailea1"
RMT_PASSWORD = "freescale"
RMT_TESTDIR = "arailea1/" #dir on server used by tftp
RMT_BOARD = "grinch"
LOG_PATH = "../../output/test32/log/" #console log location
BIN_PATH = "../../output/test32/"     #test binaries location


START_PORT = 23400
LINUX_TESTS = ["linux","simtst"]      #identify linux tests

#for test linux-e1000
LINUX_ETH0_IP = "192.168.170.2"       #ip addr for eth0
LINUX_ETH0_PING = "192.168.170.1"     #dest ip addr for ping test

#for test simtst
LINUX_FM1GB1_IP = "192.168.20.2"      #ip addr for fm1gb1
LINUX_FM1GB1_PING = "192.168.20.1"    #dest ip addr for ping test
