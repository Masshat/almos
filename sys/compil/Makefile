#==============================================================================
# AlmOS User APIs compilation Makefile
#==============================================================================
#==============================================================================

ARCH            ?= $(ALMOS_ARCH)
CPU             ?= $(ALMOS_CPU)

# target directories
#------------------------------------------------------------------------------
TGT_LIB         ?= $(ALMOS_TOP)/lib
TGT_INCLUDE     ?= $(ALMOS_TOP)/include
SRC_DIR         ?= $(ALMOS_TOP)/src

THIS_DIR        = $(shell pwd)

# User source directories 
#------------------------------------------------------------------------------
SRC_C           = $(SRC_DIR)/dietlibc#       C librairies
SRC_MATH        = $(SRC_DIR)/libm#           MATH librairies
SRC_PTHREAD     = $(SRC_DIR)/libpthread#     pthread
SRC_GOMP        = $(SRC_DIR)/libgomp#        parallel programming model for shared memory multiprocessor architectures
SRC_UPC         = $(SRC_DIR)/libupc#         Unified Parallel C
SRC_PHOENIX     = $(SRC_DIR)/phoenix#        Standford implementation of Map&Reduce parallel system
SRC_ZLIB        = $(SRC_DIR)/zlib#           Data compression library
SRC_CRYPTO      = $(SRC_DIR)/libcrypto#      Openssl crypto library
SRC_MK          = $(SRC_DIR)/mk#             generic applicatiopn makefiles

# System related source directories 
#------------------------------------------------------------------------------
SRC_CPU         = $(SRC_DIR)/cpu/$(CPU)#    CPU dependant sources (assembler)
SRC_ARCH        = $(SRC_DIR)/arch/$(ARCH)#  plateform dependant sources

# Include files
#------------------------------------------------------------------------------
MK              = appli.mk
SYS-INC         = stat.h time.h syscall.h cpu-jmp.h mman.h types.h
SSL-INC         = $(shell cd $(SRC_CRYPTO); echo *.h)
UPC-INC         = $(shell cd $(SRC_UPC); echo *.h)
PNX-INC         = $(shell cd $(SRC_PHOENIX); echo *.h)
CPU-INC         = cpu-syscall.h cpu-jmp.h arch-config.h
USR-INC         = $(shell cd $(SRC_MATH); echo *.h)    \
		  $(shell cd $(SRC_C); echo *.h)       \
		  $(shell cd $(SRC_PTHREAD); echo *.h) \
		  $(shell cd $(SRC_GOMP); echo *.h)    \
	          $(shell cd $(SRC_UPC); echo *.h)     \
	          $(shell cd $(SRC_PHOENIX); echo *.h)     \
	          $(shell cd $(SRC_CRYPTO); echo *.h)  \
		  zconf.h zlib.h 

INCLUDES        = $(MK) $(USR-INC) $(CPU-INC)

EXCLUDES        = dietfeatures.h dietstdio.h dietwarning.h dietlocale.h \
		  dietstring.h dietlibm.h $(CPU-INC) $(SSL-INC) $(UPC-INC) $(PNX-INC)

INCLUDES	:= $(patsubst %, $(THIS_DIR)/%, $(INCLUDES))
EXCLUDES	:= $(patsubst %, $(TGT_INCLUDE)/%, $(EXCLUDES))

ALMOS_DATE      = almOS-date.h
LDSCRIPT        = $(SRC_MK)/$(ALMOS_ARCH_CLASS)_usr_ldscript.h

# libraries and object files
#------------------------------------------------------------------------------
LIBS            = libc.a libpthread.a libgomp.a libm.a libz.a libcrypto.a libupc.a libphoenix.a
OBJ_C           = crt0.o

# Object files of librairies
#------------------------------------------------------------------------------
OBJ_CPU        = __cpu_jmp.o cpu_syscall.o
OBJ_LIBC       = $(patsubst %.c,%.o,$(shell cd $(SRC_C); echo *.c)) $(OBJ_CPU)
OBJ_LIBM       = $(patsubst %.c,%.o,$(shell cd $(SRC_MATH); echo *.c))
OBJ_LIBPTHREAD = $(patsubst %.c,%.o,$(shell cd $(SRC_PTHREAD); echo *.c))
OBJ_LIBGOMP    = $(patsubst %.c,%.o,$(shell cd $(SRC_GOMP); echo *.c))
OBJ_LIBUPC     = $(patsubst %.c,%.o,$(shell cd $(SRC_UPC); echo *.c))
OBJ_LIBPNX     = $(patsubst %.c,%.o,$(shell cd $(SRC_PHOENIX); echo *.c))
OBJ_LIBZ       = $(patsubst %.c,%.o,$(shell cd $(SRC_ZLIB); echo *.c))
OBJ_CRYPTO     = $(patsubst %.c,%.o,$(shell cd $(SRC_CRYPTO); echo *.c))

#    +--------------------------------------------------------------------+
# ===| This Part doesn't depend on source files, it should not be changed |====
#    +--------------------------------------------------------------------+

# CC tools and parameters
#------------------------------------------------------------------------------
CC              = $(CCTOOLS)/bin/$(CPU)-unknown-elf-gcc
AR              = $(CCTOOLS)/bin/$(CPU)-unknown-elf-ar
AS              = $(CCTOOLS)/bin/$(CPU)-unknown-elf-as
OD              = $(CCTOOLS)/bin/$(CPU)-unknown-elf-objdump
LD              = $(CCTOOLS)/bin/$(CPU)-unknown-elf-ld
NM              = $(CCTOOLS)/bin/$(CPU)-unknown-elf-nm
STRIP           = $(CCTOOLS)/bin/$(CPU)-unknown-elf-strip 

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

ifeq ($(findstring libgomp.a, $(LIBS)), libgomp.a)
ADD-CFLAGS += -DHAVE_GOMP
$(info "  [ INFO ]  Compiling with libgomp")
endif

CFLAGS        = -I. -c -W -Wall -Wextra -Wchar-subscripts \
		-Wno-switch -Wno-unused -Wredundant-decls -fno-strict-aliasing \
		-fno-pic -static -Werror -DUSR_LIB_COMPILER -D_ALMOS_ $(CPUCFLAGS) $(ADD-CFLAGS) -DZ_PREFIX

AFLAGS          = -I.
TRASH           = /dev/null||true 

# rules
#------------------------------------------------------------------------------
ulibs : $(INCLUDES) $(LIBS) $(OBJ_C) uldscript
	@cp $(SYS-INC) $(TGT_INCLUDE)/sys
	@cp $(SSL-INC) $(TGT_INCLUDE)/openssl
	@cp $(UPC-INC) $(TGT_INCLUDE)/upc
	@cp $(PNX-INC) $(TGT_INCLUDE)/phoenix
	@cp $(INCLUDES)  $(TGT_INCLUDE)
	@$(RM) $(EXCLUDES)
	@cp $(LIBS) $(OBJ_C) uldscript $(TGT_LIB)
	make -C $(SRC_DIR)/TinyGL TGT_LIB=$(TGT_LIB) TGT_INCLUDE=$(TGT_INCLUDE) CFLAGS="$(CFLAGS)" CC=$(CC) AR=$(AR)
	@echo '   [ DONE ]        '
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uldscript: $(LDSCRIPT) arch-config.h
	@echo '   [  CPP ]        '$<
	@cpp -I. $< | grep -v "#" | grep . > $@
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
$(THIS_DIR)/%.h   : $(SRC_DIR)/[^c]??*/%.h   ; @echo '   [  CP  ]        '$< ; cp $< $@
$(THIS_DIR)/%.mk  : $(SRC_MK)/%.mk     ; @echo '   [  CP  ]        '$< ; cp $< $@ 
$(THIS_DIR)/%.h   : $(SRC_ARCH)/%.h    ; @echo '   [  CP  ]        '$< ; cp $< $@
$(THIS_DIR)/%.h   : $(SRC_CPU)/%.h     ; @echo '   [  CP  ]        '$< ; cp $< $@
$(THIS_DIR)/%.h   : $(SRC_SYS)/%.h     ; @echo '   [  CP  ]        '$< ; cp $< $@

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
libc.a       : $(OBJ_LIBC)      ; @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
libm.a       : $(OBJ_LIBM)      ; @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
libpthread.a : $(OBJ_LIBPTHREAD); @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
libgomp.a    : $(OBJ_LIBGOMP)   ; @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
libupc.a     : $(OBJ_LIBUPC)    ; @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
libphoenix.a : $(OBJ_LIBPNX)    ; @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
libz.a       : $(OBJ_LIBZ)      ; @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
libcrypto.a  : $(OBJ_CRYPTO)    ; @echo '   [  AR  ]        '$@ ; $(AR) -r $@ $^
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
%.o : $(SRC_DIR)/*/%.c
		@echo '   [  CC  ]        '$< 
		@$(CC) $(CFLAGS) $< -o $@
		@echo '   [ STRP ]        '$@ 
		@$(STRIP)-x -R .comment -R .note $@

%.o : $(SRC_CPU)/%.c
		@echo '   [  CC  ]        '$< 
		@$(CC) $(CFLAGS) $< -o $@
		@echo '   [ STRP ]        '$@ 
		@$(STRIP)-x -R .comment -R .note $@

%.o : $(SRC_CPU)/%.S
		@echo '   [  CC  ]        '$< 
		@$(CC) $(CFLAGS) $< -o $@
		@echo '   [ STRP ]        '$@ 
		@$(STRIP)-x -R .comment -R .note $@
#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

.PHONY: clean realclean ulibs start

cleantgl:
	make -C $(SRC_DIR)/TinyGL clean TGT_LIB=$(TGT_LIB) TGT_INCLUDE=$(TGT_INCLUDE) CFLAGS="$(CFLAGS)" CC=$(CC) AR=$(AR)

clean:  cleantgl
	@echo '   [  RM  ]        *.s *.a *.o *.h *.mk *~ uldscript *.dump'
	@$(RM) *.s *.a *.o *.h *.mk *~ uldscript *.dump 2> $(TRASH)

realclean: clean
	@echo '   [  RM  ]        '"$(RM) $(TGT_LIB)/* $(TGT_INCLUDE)/* $(TGT_INCLUDE)/*/*.h"
	@$(RM) $(TGT_LIB)/* $(TGT_INCLUDE)/* $(TGT_INCLUDE)/*/*.h 2> $(TRASH)
