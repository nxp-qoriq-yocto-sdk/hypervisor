#!/usr/bin/env python
# Copyright (C) 2010 Freescale Semiconductor, Inc.
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

import sys
import pexpect
import time
import imp
import subprocess
import os
import signal
import common
import csv
import glob
import re


if len(sys.argv) > 1:
	redirect = sys.argv[1]
	outFile = open(redirect,'w')
	sys.stdout = outFile

res_list = glob.glob("*_results.txt")
#print "\n\nUsing files: %s\n\n" % " ".join(res_list)


full_res = {}
target_lst = []

for filename in res_list:
	filecsv = open(filename,'rb')
	dictionar = csv.DictReader(filecsv)
#for row in dictionar:
#	print row['Test Name'].ljust(30), row['Result'].ljust(40)

	target = re.sub("_results.txt",'',filename)
	target_lst.append(target)
	#print target


	for row in dictionar:
		key = row['Test Name']
		if key not in full_res:
			full_res[key] = {}
		full_res[key][target]=row['Result']

keys = full_res.keys()
keys.sort() 	
#print full_res

targetprint = ''
for target in target_lst:
	targetprint += target.ljust(20)
title = "Test Name".ljust(30)+targetprint
print title
print len(title)*"="
for key in keys:
	result = ''
	for target in target_lst:
		try:
			result += full_res[key][target].ljust(20)
		except KeyError:
			result += ''.ljust(20)
	#row['Result'].ljust(40)
	print key.ljust(30),result

