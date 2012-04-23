#
# This file is part of AlmOS.
#
# AlmOS is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# AlmOS is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with AlmOS; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
#
# UPMC / LIP6 / SOC (c) 2007, 2008, 2009
# Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
#

# Default plateform architecture and default CPU
#------------------------------------------------------------------------------
ARCH        = $(ALMOS_ARCH)
CPU         = $(ALMOS_CPU)
ARCH_CLASS  = $(ALMOS_ARCH_CLASS)

# CC tools and parameters
#------------------------------------------------------------------------------
DIR_INC  = $(ALMOS_TOP)/include
DIR_LIB  = $(ALMOS_TOP)/lib
GCC_LIB  = $(CCTOOLS)/lib
CC       = $(CCTOOLS)/bin/$(CPU)-unknown-elf-gcc		
AR       = $(CCTOOLS)/bin/$(CPU)-unknown-elf-ar
AS       = $(CCTOOLS)/bin/$(CPU)-unknown-elf-as
OD       = $(CCTOOLS)/bin/$(CPU)-unknown-elf-objdump
OCPY	 = $(CCTOOLS)/bin/$(CPU)-unknown-elf-objcopy
LD       = $(CCTOOLS)/bin/$(CPU)-unknown-elf-ld
NM	 = $(CCTOOLS)/bin/$(CPU)-unknown-elf-nm

ifneq ($(ADD-LDSCRIPT),)
LDSCRIPT=$(ADD-LDSCRIPT)
else
LDSCRIPT=uldscript
endif

ifeq ($(CPU), mipsel)
CPU-CFLAGS = -G0
endif

ifeq ($(CPU), i386)
CPU-CFLAGS = -g
endif

CFLAGS = -c -fomit-frame-pointer $(ADD-CFLAGS)
LIBS =  -L$(DIR_LIB) -L$(GCC_LIB) $(CPU-LFLAGS) -T$(LDSCRIPT) $(OBJ) $(ADD-LIBS) \
	-lc -lpthread $(ADD-LIBS) -lc -lgcc --hash-style=sysv

# Sources and targets files name
#-------------------------------------------------------------------------------
OBJ=$(addsuffix .o,$(FILES))
SRC=$(addsuffix .c,$(FILES))
BIN ?= soft.bin

ifndef TARGET
RULE=usage
endif

ifeq ($(TARGET), TSAR)
RULE=almos
CFLAGS += $(CPU-CFLAGS) -I$(DIR_INC) -O3 -D_ALMOS_
endif

ifeq ($(TARGET), linux)
RULE=linux
CFLAGS += -g
CC=gcc
endif

ifeq ($(TARGET), ibmpc)
RULE=almos
CFLAGS += $(CPU-CFLAGS) -I$(DIR_INC) -O3 -D_ALMOS_
endif

ifndef RULE
RULE=usage
endif

# Building rules
#------------------------------------------------------------------------------
.PHONY : usage linux soclib clean realclean

all: $(RULE) $(ADD-RULE)

usage:
	@echo "AlmOS Application Compiler";\
	echo "> make TARGET=tsar     : targets the MIPS TSAR simulator";\
	echo "> make TARGET=ibmpc    : targets the Intel X86 for IBM-PC compatible plateform";\
	echo "> make TARGET=linux    : targets the GNU/Linux plateform";\
	echo ""

linux: $(OBJ)
	@echo '   [  CC  ]        '$^ 
	@$(CC) -o $(BIN) $^ -lpthread $(ADD-LIBS)

almos : $(BIN)

$(BIN) : $(OBJ)
	@echo '   [  LD  ]        '$@
	@$(LD) -o $@ $(LIBS)

%.o : %.c
	@echo '   [  CC  ]        '$<
	@$(CC) $(CFLAGS) $< -o $@

clean :
	@echo '   [  RM  ]        *~ *.dump *.nm '"$(BOJ)"
	@$(RM) $(OBJ) *~ *.dump *.nm 2> /dev/null||true 

realclean : clean
	@echo '   [  RM  ]        '$(BIN) tty* vcitty* 
	@$(RM) $(BIN) tty* vcitty*

