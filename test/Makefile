
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-

SUBDIRS = gdb-test hello-test hcalls-test linux  msgsnd-test  vmpic-test

.PHONY: subdirs $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

.PHONY: clean

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
