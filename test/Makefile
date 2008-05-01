
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-

SUBDIRS = gdb-test hello-test hcalls-test linux  msgsnd-test ipi_doorbell-test  vmpic-test fit-test demo whoami-test

.PHONY: subdirs $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

.PHONY: clean

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
