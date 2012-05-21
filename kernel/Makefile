#=====================================================================
#                      ALMOS-kernel Makefile                         #
#=====================================================================

include ../common.mk

KERNEL_REV ?=current

DIRS=	cpu/$(CPU)           	  \
	arch/$(ARCH)         	  \
	drivers/$(DRVRS)	  \
	kern                      \
	mm                        \
	fs/devfs                  \
	fs/sysfs                  \
	fs/ext2                   \
	fs/fat32                  \
	vfs                       \
	libk                      \
	ksh

LFLAGS = -T$(OBJDIR)/kldscript $(CPULFLAGS)

INCLUDES=	$(foreach DIR,$(DIRS),-I$(DIR)) -I$(OBJDIR)/kern/

CFLAGS= 	$(INCLUDES) -O2 -fno-builtin -static -Wall -Werror -MMD
CFLAGS+=	$(CPUCFLAGS)

CFILES=	$(foreach DIR,$(DIRS),$(wildcard $(DIR)/*.c))
SFILES=	$(wildcard cpu/$(CPU)/*.S)

OBJS=	$(addprefix $(OBJDIR)/, $(CFILES:.c=.o) $(SFILES:.S=.o))
DEPS=	$(OBJS:.o=.d)

OBJDIRS= $(addprefix $(OBJDIR)/, $(DIRS))

# Kernel bin file name
ifeq ($(KERNEL_REV),current)
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)
else
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)-$(KERNEL_REV)
endif


.PHONY: clean timestamp


$(TARGET): $(OBJDIRS) $(OBJDIR)/kldscript timestamp $(OBJS) 
	@echo '   [  LD  ]        '$@.elf
	@$(LD) -o $@.elf $(OBJS) $(LFLAGS)
	@echo '   [  NM  ]        '$@.elf.nm 
	@$(NM) $@.elf > $@.elf.nm
	@echo '   [ OCPY ]        '$@.bin
	@$(OCPY) -O binary  $@.elf  $@.bin
	@echo -e '\n   [ DONE ]        \n'


timestamp:
	@echo '#define ALMOS_DATE "'$(shell date +"%d %B %Y - %T")'"' > $(OBJDIR)/kern/almOS-date.h 

$(OBJDIR)/kldscript: arch/$(ARCH)/kldscript.h arch/$(ARCH)/arch-config.h
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

clean:
	rm -rf $(OBJDIR)

$(OBJDIRS):
	@echo '   [ MKDIR ]       '$@
	@mkdir -p $@


-include $(DEPS)
