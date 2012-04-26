
include ../../common.mk

USE_HARD_FLOAT=yes

ifeq ($(CPU), mipsel)
ifeq ($(USE_HARD_FLOAT), yes)
CPUCFLAGS = -mhard-float
else
CPUCFLAGS = -msoft-float
endif
CPUCFLAGS += -mips32 -EL -G0 -fomit-frame-pointer -O3
endif

ifeq ($(CPU), i386)
#CPUCFLAGS = -msoft-float -fomit-frame-pointer -Os
CPUCFLAGS = -g --hash-style=sysv
endif


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
