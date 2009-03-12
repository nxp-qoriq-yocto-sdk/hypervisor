#
# Copyright (C) 2009 Freescale Semiconductor, Inc.
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

#This script parses the ccm_logs generated by the csd unit test. The unit test comprises of two partitions
#with the following configuration :
# Partition 1 :
# CPU cores - 0 - 3
# Memory - 1GB - 1.5GB
# Partition 2 :
# CPU cores - 4 - 7
# Memory - 1.5GB - 2GB
# Aim of the unit test is to ensure snooping for each partition core is restricted to the memory allocated
# to that partition i.e. cores 0 - 3 should snoop the bus for addresses ranging from 1GB - 1.5GB (0x40000000
# to 0x5fffffff)
# This can be verified from the ccm logs, each transaction captured by the ccm logs has the memory address
# and a corresponding acr (Address Coherency Response) entry. Here the first 8 "-" indicate the cores 0-7.
# A "z" in the position indicates the transaction was snooped by those cores. So, in our case we should see
# the following acr patterns :
# for addresses 0x040000000 - 0x05fffffff - acr:zzzz-------------z;
# for addresses 0x060000000 - 0x07fffffff - acr:----zzzz---------z;
# We exclude the "READ I" and "WCO" transactions as these are not snooped.

#!/bin/bash

ADDR=`cat ccm_log.txt | awk '{print $6}' | awk -F "0" '{print $3}'`
PORTS=`cat ccm_log.txt | awk '{print $11}'`
CACHE=`cat ccm_log.txt | awk '{print $3}'`
TRANS=`cat ccm_log.txt | awk '{print $2}'`

FAILED=0

declare -a CCM_ADDR[]
declare -a CCM_PORTS[]
declare -a CCM_CACHE[]
declare -a CCM_TRANS[]

ad=0
po=0
ca=0
tr=0

for i in $ADDR
do
	CCM_ADDR[ $ad ]=`echo $i`
	ad=`expr $ad + 1`
done

for j in $PORTS
do
	CCM_PORTS[ $po ]=`echo $j`
	po=`expr $po + 1`
done

for l in $CACHE
do
	CCM_CACHE[ $ca ]=`echo $l`
	ca=`expr $ca + 1`
done

for m in $TRANS
do
	CCM_TRANS[ $tr ]=`echo $m`
	tr=`expr $tr + 1`
done


for (( k = 0; k <= po; k++ ))
    do
	if [ "${CCM_TRANS[ $k ]}" == "WCO" ]
	then
		continue
	else
		if [ "${CCM_CACHE[ $k ]}" == "I" ]
		then
			continue
		else
			if [ "${CCM_ADDR[ $k ]}" == "4" ] || [ "${CCM_ADDR[ $k ]}" == "5" ]
			then
				if [ "${CCM_PORTS[ $k ]}" != "acr:zzzz-------------z;" ]
				then
					FAILED=1
					break
				fi
			fi

			if [ "${CCM_ADDR[ $k ]}" == "6" ] || [ "${CCM_ADDR[ $k ]}" == "7" ]
			then
				if [ "${CCM_PORTS[ $k ]}" != "acr:----zzzz---------z;" ]
				then
					FAILED=1
					break
				fi
			fi
		fi
	fi
    done

if [ $FAILED == "1" ]
then
	echo "Coherency Subdomain Test - FAILED"
else
	echo "Coherency Subdomain Test - PASSED"
fi
