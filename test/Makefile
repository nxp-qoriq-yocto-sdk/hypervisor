
# The env var $CROSS_COMPILE should be set to powerpc-unknown-linux-gnu-

SUBDIRS = gdb hello hcalls linux msgsnd ipi vmpic fit whoami byte-chan mmu dma

.PHONY: subdirs $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

.PHONY: clean

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
