$(TEST)-src-y := $(dir)$(TEST).c
$(TEST)-hv-dts-y := bin/$(TEST)/hv.dts
$(TEST)-part-dts-y := $(dir)p1.dts

$(TEST)-obj := $(basename $(libos-src-first-y:%=libos/%) \
                          $(libos-src-early-y:%=libos/%) \
                          $(libos-src-y:%=libos/%) \
                          $($(TEST)-src-y:$(dir)%=$(TEST)/%) \
                          $(LIBFDT_SRCS:%=libfdt/%) \
                          common/init.o)

$(TEST)-obj := $($(TEST)-obj:%=bin/%.o)

$(TEST)-hv-dtb := $($(TEST)-hv-dts-y:%.dts=%.dtb)
$(TEST)-part-dtb := $($(TEST)-part-dts-y:$(dir)%.dts=bin/$(TEST)/%.dtb)

$($(TEST)-hv-dts-y): $($(TEST)-part-dtb)

bin/$(TEST)/$(TEST): $($(TEST)-obj)
	@$(MKDIR) $(@D)
	$(call show,link: $@)
	$(V)$(CC) $(LD_OPTS) -Wl,-T$(test)common/tests.lds -o $@ $^ -lgcc

.PHONY: $(TEST)
$(TEST): bin/$(TEST)/$(TEST) $($(TEST)-hv-dtb)

$(TEST)-run: $(TEST)
	$(SIMICS) $(dir)run.simics
