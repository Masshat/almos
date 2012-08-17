TGTDIR=almos-$(ARCH)-$(CPU)/almos

LIBDIR=$(DISTRIB)/$(TGTDIR)/lib
INCDIR=$(DISTRIB)/$(TGTDIR)/include

LDSCRIPT=$(ARCH)_uldscript

include $(SRCDIR)../../common.mk



all:
	cp $(ARCH)_uldscript $(LIBDIR)/uldscript
	cp include/* $(INCDIR)/.

clean:
	@echo ""


.PHONY: clean all