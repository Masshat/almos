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

ifeq ($(MODE),DEBUG)
EXTRACFLAGS = -g
else
EXTRACFLAGS = -fomit-frame-pointer
endif

LFLAGS = -T$(OBJDIR)/kldscript $(CPULFLAGS)

INCLUDES=	$(foreach DIR,$(DIRS),-I$(DIR)) -I$(OBJDIR)/kern/

CFLAGS= 	$(INCLUDES) -O2 -fno-builtin -static -Wall -Werror -MMD
CFLAGS+=	$(CPUCFLAGS) $(EXTRACFLAGS)

OBJDIR = .build

CFILES=	$(foreach DIR,$(DIRS),$(wildcard $(DIR)/*.c))
SFILES=	$(wildcard cpu/$(CPU)/*.S)

OBJS=	$(addprefix $(OBJDIR)/, $(CFILES:.c=.o) $(SFILES:.S=.o))
DEPS=	$(OBJS:.o=.d)

# Kernel bin file name
ifeq ($(KERNEL_REV),current)
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)
else
TARGET=$(OBJDIR)/almix-$(ARCH)-$(CPU)-$(KERNEL_REV)
endif


.PHONY: clean realclean obj


$(TARGET): obj $(OBJDIR)/kldscript $(OBJDIR)/kern/almOS-date.h $(OBJS) 
	@echo '   [  LD  ]        '$@.elf
	@$(LD) -o $@.elf $(OBJS) $(LFLAGS)
	@echo '   [  NM  ]        '$@.elf.nm 
	@$(NM) $@.elf > $@.elf.nm
	@echo '   [ OCPY ]        '$@.bin
	@$(OCPY) -O binary  $@.elf  $@.bin
	@echo -e '\n   [ DONE ]        \n'


$(OBJDIR)/kern/almOS-date.h:
	$(shell echo '#define ALMOS_DATE "'$(shell date +"%d %B %Y - %T")'"' > $@

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
	@$(RM) $(OBJDIR)/$(OBJS) $(OBJDIR)/$(DEPS)
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

-include $(DEPS)
