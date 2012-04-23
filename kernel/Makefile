#=====================================================================
#                      ALMOS-kernel Makefile                         #
#=====================================================================

# CPU type
CPU ?=$(ALMOS_CPU)

# ARCH type
ARCH ?=$(ALMOS_ARCH)

# Used drivers
DRVRS ?=$(ALMOS_DRIVERS)

# Kernel bin directory
TARGET_DIR ?=$(ALMOS_TOP)/bin

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
EXTRACFLAGS = -g -O2
else
EXTRACFLAGS = -O2 -fomit-frame-pointer
endif

# CPU specific flags
ifeq ($(CPU), mipsel)
CPUCFLAGS = -mips32 -EL -G0
CPULFLAGS = -G0 --hash-style=sysv
endif

ifeq ($(CPU), i386)
CPUCFLAGS = -march=i386
endif

# Compilation flags
CFLAGS = -I. -c -fno-builtin -c -static -Wall -Werror $(CPUCFLAGS) $(EXTRACFLAGS)

# LD flags
LFLAGS = -T$(BUILD_DIR)/kldscript $(CPULFLAGS)

# Add directories to the include and library paths
INCPATH = $(DIRS) $(BUILD_DIR)/kern

# Which files to add to backups, apart from the source code
EXTRA_FILES = Makefile

# CC tools and parameters
#------------------------------------------------------------------------------
CC      ?= $(CCTOOLS)/bin/$(CPU)-unknown-elf-gcc
LD      ?= $(CCTOOLS)/bin/$(CPU)-unknown-elf-ld
NM      ?= $(CCTOOLS)/bin/$(CPU)-unknown-elf-nm
STRIP   ?= $(CCTOOLS)/bin/$(CPU)-unknown-elf-strip 
OCPY	?= $(CCTOOLS)/bin/$(CPU)-unknown-elf-objcopy

# Where to store object and dependancy files
BUILD_DIR = .build

# Makes a list of the source (.c) files
SOURCE_C:= $(foreach DIR,$(DIRS),$(wildcard $(DIR)/*.c))
SOURCE_S:= $(shell echo $(CPU_DIR)/*.S)
SOURCES := $(SOURCE_C) $(SOURCE_S)

# List of header files
HEADERS := $(foreach DIR,$(DIRS),$(wildcard $(DIR)/*.h))

# Makes a list of the object files that will have to be created
OBJECTS_C := $(addprefix $(BUILD_DIR)/, $(SOURCE_C:.c=.o))
OBJECTS_S := $(addprefix $(BUILD_DIR)/, $(patsubst %.S,%.o,$(SOURCE_S)))
OBJECTS   := $(OBJECTS_C) $(OBJECTS_S)

# Same for the .d (dependancy) files
DFILES_C := $(addprefix $(BUILD_DIR)/,$(SOURCE_C:.c=.d))
DFILES_S := $(addprefix $(BUILD_DIR)/,$(SOURCE_S:.S=.d))
DFILES   := $(DFILES_C) $(DFILES_S)

# Compilation date header
ALMOS_DATE=$(BUILD_DIR)/kern/almOS-date.h
ALMOS_DEP=$(BUILD_DIR)/kern/almOS.dep

# Kernel bin file name
ifeq ($(KERNEL_REV),current)
TARGET=$(TARGET_DIR)/almix-$(ARCH)-$(CPU)
else
TARGET=$(TARGET_DIR)/almix-$(ARCH)-$(CPU)-$(KERNEL_REV)
endif

# Rules that are not real files
.PHONY: clean realclean backup export dirs start

#- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
$(TARGET): start $(OBJECTS) $(BUILD_DIR)/kldscript
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

$(BUILD_DIR)/kldscript : $(ARCH_DIR)/kldscript.h $(ARCH_DIR)/arch-config.h 
	@echo '   [  CPP ]        '$< 
	@cpp $< | grep -v "#" | grep . > $@


# Rule for creating object file and .d file, the sed magic is to add
# the object path at the start of the file because the files gcc
# outputs assume it will be in the same dir as the source file
$(BUILD_DIR)/%.o: %.c
	@echo '   [  CC  ]        '$<
	@$(CC) -Wp,-MMD,$(BUILD_DIR)/$*.dd $(CFLAGS) $(foreach INC,$(INCPATH),-I$(INC)) -c $< -o $@
ifneq   ($(MODE),DEBUG)
	@$(STRIP) -x -R .pdr -R .gnu.attributes -R .reginfo -R .comment $@
endif
	@sed -e '1s/^\(.*\)$$/$(subst /,\/,$(dir $@))\1/' $(BUILD_DIR)/$*.dd > $(BUILD_DIR)/$*.d
	@$(RM) $(BUILD_DIR)/$*.dd

$(BUILD_DIR)/%.o: %.S
	@echo '   [  AS  ]        '$<
	@$(CC) -Wp,-MMD,$(BUILD_DIR)/$*.dd $(CFLAGS) $(foreach INC,$(INCPATH),-I$(INC)) -c $< -o $@
ifneq   ($(MODE),DEBUG)
	@$(STRIP) -x -R .pdr -R .gnu.attributes -R .reginfo -R .comment $@
endif
	@sed -e '1s/^\(.*\)$$/$(subst /,\/,$(dir $@))\1/' $(BUILD_DIR)/$*.dd > $(BUILD_DIR)/$*.d
	@$(RM) $(BUILD_DIR)/$*.dd


# Empty rule to prevent problems when a header is deleted
%.h: ;

# Cleans up the objects, .d files, kldscript and executables
clean:
	@$(RM) $(foreach DIR,$(DIRS),$(BUILD_DIR)/$(DIR)/*.d $(BUILD_DIR)/$(DIR)/*.o)
	@$(RM) $(BUILD_DIR)/kldscript 
	@$(RM) $(TARGET).elf $(TARGET).bin $(TARGET).elf.nm

realclean: clean
	@$(RM) -r $(BUILD_DIR)

# Backup the source files
backup:
	@if [ ! -e .backup ]; then mkdir .backup; fi;
	@tar jcvf .backup/backup_`date +%d-%m-%y_%H.%M`.tbz2 $(SOURCES) $(HEADERS) $(EXTRA_FILES)


# Create compilation tree
dirs:
	@if [ ! -e $(BUILD_DIR) ]; then mkdir $(BUILD_DIR); fi;
	@$(foreach DIR,$(DIRS), 					\
		if [ ! -e $(BUILD_DIR)/$(DIR) ]; 			\
		then 							\
			echo '   [ MKDIR ]       '$(BUILD_DIR)/$(DIR);  \
			mkdir $(BUILD_DIR)/$(DIR); 			\
		fi;)

# Includes the .d files so it knows the exact dependencies for every source
-include $(DFILES)
