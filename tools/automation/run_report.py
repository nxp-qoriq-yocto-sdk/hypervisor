#!/usr/bin/env python

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

res_list = glob.glob("*_results.txt")
print "\n\nUsing files: %s\n\n" % " ".join(res_list)


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

