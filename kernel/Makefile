#=====================================================================
#                      ALMOS-kernel Makefile                         #
#=====================================================================

# CPU type
CPU ?= mipsel

# ARCH type
ARCH ?= tsar

# Used drivers
DRVRS ?= soclib

# Kernel revsion
KERNEL_REV ?=current

# CPU directory
CPU_DIR=cpu/$(CPU)

# ARCH directory
ARCH_DIR=arch/$(ARCH)

# Drivers directory
DRV_DIR=drivers/$(DRVRS)

# Kernel directories contain source files
DIRS =  cpu $(CPU_DIR)            \
	arch $(ARCH_DIR)          \
	drivers $(DRV_DIR)        \
	kern                      \
	mm                        \
	fs 			  \
	fs/devfs                  \
	fs/sysfs                  \
	fs/ext2                   \
	fs/fat32                  \
	vfs                       \
	libk                      \
	ksh

ifeq ($(MODE),DEBUG)
EXTRACFLAGS = -g
else
EXTRACFLAGS = -fomit-frame-pointer
endif

# CPU specific flags
ifeq ($(CPU), mipsel)
CPUCFLAGS = -mips32 -EL -G0
CPULFLAGS = -G0 --hash-style=sysv
endif

ifeq ($(CPU), i386)
CPUCFLAGS = -march=i386
endif

# LD flags
LFLAGS = -T$(OBJDIR)/kldscript $(CPULFLAGS)

INCLUDES=	$(foreach DIR,$(DIRS),-I$(DIR)) -I$(OBJDIR)/kern/

CFLAGS= 	$(INCLUDES) -O2 -fno-builtin -static -Wall -Werror -MMD
CFLAGS+=	$(CPUCFLAGS) $(EXTRACFLAGS)

GCCPREFIX = $(CPU)-unknown-elf-

# CC tools and parameters
#------------------------------------------------------------------------------
CC      = $(GCCPREFIX)gcc
LD      = $(GCCPREFIX)ld
NM      = $(GCCPREFIX)nm
STRIP   = $(GCCPREFIX)strip
OCPY	= $(GCCPREFIX)objcopy

# Where to store object and dependancy files
OBJDIR = .build

# Makes a list of the source (.c) files
SOURCE_C:= $(foreach DIR,$(DIRS),$(wildcard $(DIR)/*.c))
SOURCE_S:= $(wildcard $(CPU_DIR)/*.S)
SOURCES := $(SOURCE_C) $(SOURCE_S)

# Makes a list of the object files that will have to be created
OBJECTS_C := $(addprefix $(OBJDIR)/, $(SOURCE_C:.c=.o))
OBJECTS_S := $(addprefix $(OBJDIR)/, $(SOURCE_S:.S=.o))
OBJECTS   := $(OBJECTS_C) $(OBJECTS_S)
DEPENDS   := $(OBJECTS:.o=.d)

# Kernel bin file name
ifeq ($(KERNEL_REV),current)
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)
else
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)-$(KERNEL_REV)
endif


.PHONY: clean realclean dirs start


$(TARGET): obj $(OBJDIR)/kldscript $(OBJDIR)/kern/almOS-date.h $(OBJECTS) 
	@echo '   [  LD  ]        '$@.elf
	@$(LD) -o $@.elf $(OBJECTS) $(LFLAGS)
	@echo '   [  NM  ]        '$@.elf.nm 
	@$(NM) $@.elf > $@.elf.nm
	@echo '   [ OCPY ]        '$@.bin
	@$(OCPY) -O binary  $@.elf  $@.bin
	@echo -e '\n   [ DONE ]        \n'


$(OBJDIR)/kern/almOS-date.h:
	$(shell echo '#define ALMOS_DATE "'$(shell date +"%d %B %Y - %T")'"' > $@

$(OBJDIR)/kldscript : $(ARCH_DIR)/kldscript.h $(ARCH_DIR)/arch-config.h 
	@echo '   [  CPP ]        '$< 
	@cpp $< | grep -v "#" | grep . > $@

$(OBJDIR)/%.o: %.c
	@echo '   [  CC  ]        '$<
	@$(CC) $(CFLAGS) -c $< -o $@
ifneq   ($(MODE),DEBUG)
	@$(STRIP) -x -R .pdr -R .gnu.attributes -R .reginfo -R .comment $@
endif

$(OBJDIR)/%.o: %.S
	@echo '   [  AS  ]        '$<
	@$(CC) $(CFLAGS) -c $< -o $@
ifneq   ($(MODE),DEBUG)
	@$(STRIP) -x -R .pdr -R .gnu.attributes -R .reginfo -R .comment $@
endif


# Cleans up the objects, .d files, kldscript and executables
clean:
	@$(RM) $(OBJDIR)/$(OBJECTS) $(OBJDIR)/$(DEPENDS)
	@$(RM) $(OBJDIR)/kldscript
	@$(RM) $(TARGET).elf $(TARGET).bin $(TARGET).elf.nm

realclean: clean
	@$(RM) -r $(OBJDIR)

obj:
	@if [ ! -e $(OBJDIR) ]; then mkdir $(OBJDIR); fi;
	@$(foreach DIR,$(DIRS), 					\
		if [ ! -e $(OBJDIR)/$(DIR) ]; 			\
		then 							\
			echo '   [ MKDIR ]       '$(OBJDIR)/$(DIR);  \
			mkdir $(OBJDIR)/$(DIR); 			\
		fi;)

# Includes the .d files so it knows the exact dependencies for every source
-include $(DEPENDS)
