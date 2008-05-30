export LIBOS_DIR := ../libos/lib
export LIBOS_INC := ../libos/include

$(shell ln -sfn $(LIBOS_DIR) libos)

export VERSION=0
export SUBVERSION=2
export EXTRAVERSION=-rc1
export LOCALVERSION := $(shell tools/setlocalversion)

export PROJECTVERSION=$(VERSION).$(SUBVERSION)$(EXTRAVERSION)$(LOCALVERSION)
export srctree=$(CURDIR)

.PHONY: all
all:

.config include/config/auto.conf.cmd: ;

-include include/config/auto.conf.cmd

include/config/auto.conf: .config include/config/auto.conf.cmd
	@$(MAKE) -f $(srctree)/Makefile silentoldconfig

.PHONY: FORCE
config %config: FORCE
	$(MAKE) -f kconfig/Makefile obj=bin srctree=$(CURDIR) src=kconfig $@

non-config := $(filter-out %config clean, $(MAKECMDGOALS))
ifeq ($(MAKECMDGOALS),)
	non-config := all
endif

$(non-config): include/config/auto.conf
	$(MAKE) -f Makefile.build $@

clean:
	find * -name bin | xargs rm -rf
	rm -rf include/config
	rm -f dts/*.dtb
	rm -f libos
