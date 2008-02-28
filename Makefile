export PROJECTVERSION=0.0
export srctree=$(CURDIR)

.PHONY: all
all:

.config include/config/auto.conf.cmd: ;

-include include/config/auto.conf.cmd

include/config/auto.conf: .config include/config/auto.conf.cmd
	@$(MAKE) -f $(srctree)/Makefile silentoldconfig

.PHONY: silentoldconfig menuconfig xconfig gconfig oldconfig config
silentoldconfig menuconfig xconfig gconfig oldconfig config:
	$(MAKE) -f kconfig/Makefile obj=bin srctree=$(CURDIR) src=kconfig $@

non-config := $(filter-out %config clean, $(MAKECMDGOALS))
ifeq ($(MAKECMDGOALS),)
	non-config := all
endif

$(non-config): include/config/auto.conf
	$(MAKE) -f Makefile.build $@

clean:
	rm -rf bin include/config
	rm -f dts/*.dtb
