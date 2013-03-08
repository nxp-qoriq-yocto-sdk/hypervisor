#
# Copyright 2012 Freescale Semiconductor, Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
#  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
#  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
#  NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
#  TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

import subprocess, os
import sys
import shlex

def setParentSignalGroup():
		"""Allow no signals from the parent process to go this new process"""
		os.setpgrp()

def find_program(program, alt_path):

	#first search the program in path
	paths = os.environ['PATH'].split(os.pathsep)
	for path in paths:
		if os.path.isdir(path):
			fullpath = os.path.join(path, program)
			if os.path.isfile(fullpath):
				return 1, program

	if alt_path == '':
		return 0, program + " not found in PATH or in " + alt_path

	#search the alternate path
	fullpath = os.path.join(alt_path, program)
	if os.path.isfile(fullpath):
		return 1, fullpath

	return 0, program + " not found in PATH or in " + alt_path


def run_mux_server():
	vipr_path = sys.prefix + "/../../../../../libexec/"

	portID = top.duart1.exts.getTIOListeningPort()
	host = 'localhost:{0}'.format(portID)

	ports = str(BASE_PORT)
	for i in range(1, TOTAL_PORTS):
		ports = ports + ' ' + str(BASE_PORT + i)

	found, mux_server_exec = find_program('mux_server', os.path.join(os.getcwd(),'../../tools/mux_server'))
	if found == 0:
		print mux_server_exec
		return

	mux_server = shlex.split(mux_server_exec + ' -exec "' + vipr_path + 'tio_console -hub ' + host + ' -ser ' + SERIAL + '" ' + ports)

	try:
		mux = subprocess.Popen(mux_server, preexec_fn=setParentSignalGroup)
	except:
		print 'ERROR: Could not run mux_server'
		print "		mux_server launch command: " + ' '.join(mux_server)
		traceback.print_exc(file=sys.stdout)

	try:
		for i in range(0, TOTAL_PORTS):
			cmd  = 'unset LD_LIBRARY_PATH; xterm -sl 5000 -title console' + str(i) + ' -e socat -,raw,echo=0 tcp:localhost:' + str(BASE_PORT + i)
			subprocess.Popen(cmd, shell=True, preexec_fn=setParentSignalGroup)
	except:
		print 'ERROR: Could not connect to mux_server'
		print "		Console launch command: " + cmd
		traceback.print_exc(file=sys.stdout)
