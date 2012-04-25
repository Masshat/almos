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

# Add directories to the include and library paths
INCPATH = . $(DIRS) $(OBJDIR)/kern

CFLAGS = -O2 -fno-builtin -static -Wall -Werror $(CPUCFLAGS) $(EXTRACFLAGS)
CFLAGS += -Wp,-MMD,$(OBJDIR)/$*.dd  $(foreach INC,$(INCPATH),-I$(INC))

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
SOURCE_S:= $(shell echo $(CPU_DIR)/*.S)
SOURCES := $(SOURCE_C) $(SOURCE_S)

# List of header files
HEADERS := $(foreach DIR,$(DIRS),$(wildcard $(DIR)/*.h))

# Makes a list of the object files that will have to be created
OBJECTS_C := $(addprefix $(OBJDIR)/, $(SOURCE_C:.c=.o))
OBJECTS_S := $(addprefix $(OBJDIR)/, $(patsubst %.S,%.o,$(SOURCE_S)))
OBJECTS   := $(OBJECTS_C) $(OBJECTS_S)

# Same for the .d (dependancy) files
DFILES_C := $(addprefix $(OBJDIR)/,$(SOURCE_C:.c=.d))
DFILES_S := $(addprefix $(OBJDIR)/,$(SOURCE_S:.S=.d))
DFILES   := $(DFILES_C) $(DFILES_S)

# Compilation date header
ALMOS_DATE=$(OBJDIR)/kern/almOS-date.h
ALMOS_DEP=$(OBJDIR)/kern/almOS.dep

# Kernel bin file name
ifeq ($(KERNEL_REV),current)
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)
else
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)-$(KERNEL_REV)
endif

# Rules that are not real files
.PHONY: clean realclean backup export dirs start

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
$(TARGET): start $(OBJECTS) $(OBJDIR)/kldscript
	@echo '   [  LD  ]        '$@.elf
	@$(LD) -o $@.elf $(OBJECTS) $(LFLAGS)
	@echo '   [  NM  ]        '$@.elf.nm 
	@$(NM) $@.elf > $@.elf.nm
	@echo '   [ OCPY ]        '$@.bin
	@$(OCPY) -O binary  $@.elf  $@.bin
	@echo -e '\n   [ DONE ]        \n'

start: dirs
	@echo  -e '\nALMIX Compilation Process ...\n'
	$(shell echo '#define ALMOS_DATE "'$(shell date +"%d %B %Y - %T")'"' > $(ALMOS_DATE))
#	$(shell echo '#define ALMOS_DATE "'$(shell date +"%d %B %Y")'"' > $(ALMOS_DATE))

$(OBJDIR)/kldscript : $(ARCH_DIR)/kldscript.h $(ARCH_DIR)/arch-config.h 
	@echo '   [  CPP ]        '$< 
	@cpp $< | grep -v "#" | grep . > $@


# Rule for creating object file and .d file, the sed magic is to add
# the object path at the start of the file because the files gcc
# outputs assume it will be in the same dir as the source file
$(OBJDIR)/%.o: %.c
	@echo '   [  CC  ]        '$<
	@$(CC) $(CFLAGS) -c $< -o $@
ifneq   ($(MODE),DEBUG)
	@$(STRIP) -x -R .pdr -R .gnu.attributes -R .reginfo -R .comment $@
endif
	@sed -e '1s/^\(.*\)$$/$(subst /,\/,$(dir $@))\1/' $(OBJDIR)/$*.dd > $(OBJDIR)/$*.d
	@$(RM) $(OBJDIR)/$*.dd

$(OBJDIR)/%.o: %.S
	@echo '   [  AS  ]        '$<
	@$(CC) $(CFLAGS) -c $< -o $@
ifneq   ($(MODE),DEBUG)
	@$(STRIP) -x -R .pdr -R .gnu.attributes -R .reginfo -R .comment $@
endif
	@sed -e '1s/^\(.*\)$$/$(subst /,\/,$(dir $@))\1/' $(OBJDIR)/$*.dd > $(OBJDIR)/$*.d
	@$(RM) $(OBJDIR)/$*.dd


# Empty rule to prevent problems when a header is deleted
%.h: ;

# Cleans up the objects, .d files, kldscript and executables
clean:
	@$(RM) $(foreach DIR,$(DIRS),$(OBJDIR)/$(DIR)/*.d $(OBJDIR)/$(DIR)/*.o)
	@$(RM) $(OBJDIR)/kldscript 
	@$(RM) $(TARGET).elf $(TARGET).bin $(TARGET).elf.nm

realclean: clean
	@$(RM) -r $(OBJDIR)

# Create compilation tree
dirs:
	@if [ ! -e $(OBJDIR) ]; then mkdir $(OBJDIR); fi;
	@$(foreach DIR,$(DIRS), 					\
		if [ ! -e $(OBJDIR)/$(DIR) ]; 			\
		then 							\
			echo '   [ MKDIR ]       '$(OBJDIR)/$(DIR);  \
			mkdir $(OBJDIR)/$(DIR); 			\
		fi;)

# Includes the .d files so it knows the exact dependencies for every source
-include $(DFILES)
