
# System related source directories 
#------------------------------------------------------------------------------
SRC_CPU         = $(SRC_C)/cpu/$(CPU)#    CPU dependant sources (assembler)
SRC_ARCH        = $(SRC_C)/arch/$(ARCH)#  plateform dependant sources

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
LDSCRIPT        = $(SRC_MK)/$(ALMOS_ARCH)_usr_ldscript.h

# libraries and object files
#------------------------------------------------------------------------------
LIBS            = libc.a libpthread.a libgomp.a libm.a libz.a libcrypto.a libupc.a libphoenix.a
OBJ_C           = crt0.o


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

uldscript: $(LDSCRIPT) arch-config.h
	@echo '   [  CPP ]        '$<
	@cpp -I. $< | grep -v "#" | grep . > $@
