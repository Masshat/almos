
include ../../common.mk



CFLAGS=	-W -Wall -Wextra -Wchar-subscripts -Werror \
	-Wno-switch -Wno-unused -Wredundant-decls \
	-fno-strict-aliasing -fno-pic -static \
	-DUSR_LIB_COMPILER -D_ALMOS_ -DZ_PREFIX \
	$(CPUCFLAGS) $(INCFLAGS) 

OBJS= $(SRCS:.c=.o)

all: lib$(LIB).a

clean:
	rm -f lib$(LIB).a $(OBJS)

lib$(LIB).a:: $(OBJS)
	@echo '   [  AR  ]        '$@
	@rm -f lib$(LIB).a
	@$(AR) -r $@ $^
