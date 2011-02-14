#
# Copyright (C) 2008-2011 Freescale Semiconductor, Inc.
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

test := simtst
dir := $(testdir)$(test)/

$(test)-scr := $(wildcard $(dir)*.scr)
$(test)-ubs := $($(test)-scr:$(dir)%.scr=bin/$(test)/%.ubs)

.PHONY: tunctl

$(test): $($(test)-ubs)

tunctl:
	ls /sys/class/net/*-f0e0* > /dev/null 2>&1 || \
		$(testdir)../../sim/fm.sh -s -a up

simtst-run-1p: simtst tunctl
	$(MAKE) -C $(testdir)../../sim
	setsid $(testdir)../tools/xtel.sh 9000 2
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run-1p.simics

simtst-run-2p: simtst tunctl
	$(MAKE) -C $(testdir)../../sim
	setsid $(testdir)../tools/xtel.sh 9000 4
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run-2p.simics

simtst-run-4p: simtst tunctl
	$(MAKE) -C $(testdir)../../sim
	setsid $(testdir)../tools/xtel.sh 9000 5
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run-4p.simics
