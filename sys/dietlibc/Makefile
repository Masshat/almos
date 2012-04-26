
LIB=	c

VPATH=	cpu/$(CPU)

SRCS=	$(wildcard *.c)
SRCS+=	__cpu_jmp.S  cpu_syscall.c

INCFLAGS= -I. -Icpu/$(CPU) -I../libpthread -I../libgomp -I../libm

include ../lib.mk
