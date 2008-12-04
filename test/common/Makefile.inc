$(test)-src-y := $(dir)$(test).c
$(test)-dts-y := $(wildcard $(dir)*.dts)

$(test)-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $($(test)-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)

$(test)-obj := $($(test)-obj:%=bin/%.o)

$(test)-hv-dtb := $($(test)-dts-y:$(dir)%.dts=bin/$(test)/%.dtb)

bin/$(test)/$(test): $($(test)-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(testdir)common/tests.lds -o $@ $^ -lgcc

.PHONY: $(test)
$(test): bin/$(test)/$(test) $($(test)-hv-dtb)

# We cannot use $(dir) here because rule evaluation is defered.
$(test)-run: $(test)
	$(SIMICS) $(testdir)$</run.simics

$(test)-run-%: $(test)
	$(SIMICS) $(SIMICS_FLAGS) $(testdir)$</run-$*.simics

OBJS += $(test)-obj
