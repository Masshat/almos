
ARCH?=	tsar

ifeq ($(ARCH), tsar)
CPU= 	mipsel
CPUCFLAGS= -mips32 -EL -G0
CPULFLAGS= -G0 --hash-style=sysv

DRVRS=	soclib
endif


ifeq ($(ARCH), i386)
CPUCFLAGS = -march=i386
endif
