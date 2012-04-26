
ARCH?=	tsar

ifeq ($(ARCH), tsar)
CPU= 	mipsel
CPUCFLAGS= -mips32 -EL -G0
CPULFLAGS= -G0 --hash-style=sysv

DRVRS=	soclib

CCPREFIX= $(CPU)-unknown-elf-
endif


ifeq ($(ARCH), ibmpc)
CPU=		i386
CPUCFLAGS= 	-march=i386

CCPREFIX=
endif


CC=	$(CCPREFIX)cc
LD=	$(CCPREFIX)ld
NM=	$(CCPREFIX)nm
STRIP=	$(CCPREFIX)strip
OCPY=	$(CCPREFIX)objcopy
