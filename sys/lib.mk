#
# lib.mk - Common rules to build libraries
#
include $(SRCDIR)../../common.mk
ifneq ($(SRCDIR), )


CFLAGS=	-W -Wall -Wextra -Wchar-subscripts -Werror \
	-Wno-switch -Wno-unused -Wredundant-decls \
	-fno-strict-aliasing -fno-pic -static \
	-DUSR_LIB_COMPILER -D_ALMOS_ -DZ_PREFIX \
	$(CPUCFLAGS) $(INCFLAGS) 

VPATH?=	$(SRCDIR)
OBJS=	$(filter %.o, $(SRCS:.c=.o) $(SRCS:.S=.o))



all: lib$(LIB).a

lib$(LIB).a:: $(OBJS)
	@rm -f $@
	@$(AR) -r $@ $^

clean:
	rm -f lib$(LIB).a $(OBJS)


# Set of rules to for a multi-architecture build.
#
# See http://make.paulandlesley.org/multi-arch.html for more information
else

MAKEFLAGS+= --no-print-directory

$(OBJDIR):
	@[ -d $@ ] || mkdir -p $@
	@cd $@ && $(MAKE) -f $(CURDIR)/Makefile SRCDIR=$(CURDIR)/ $(MAKECMDGOALS)

Makefile : ;

% :: $(OBJDIR) ;

clean:
	@rmdir $(OBJDIR)

.PHONY:	$(OBJDIR) clean
endif
