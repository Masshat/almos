
SUBDIR= kernel sys tools/arch_info


default: all

$(SUBDIR)::
	@cd $@ && $(MAKE) $(MAKECMDGOALS)

all clean: $(SUBDIR)

.PHONY: all clean
