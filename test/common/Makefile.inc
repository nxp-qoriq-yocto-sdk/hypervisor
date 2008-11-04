$(test)-src-y := $(dir)$(test).c
$(test)-hv-dts-y := bin/$(test)/hv.dts
$(test)-part-dts-y := $(dir)p1.dts

$(test)-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $($(test)-src-y:$(dir)%=$(test)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)

$(test)-obj := $($(test)-obj:%=bin/%.o)

$(test)-hv-dtb := $($(test)-hv-dts-y:%.dts=%.dtb)
$(test)-part-dtb := $($(test)-part-dts-y:$(dir)%.dts=bin/$(test)/%.dtb)

$($(test)-hv-dts-y): $($(test)-part-dtb)

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
