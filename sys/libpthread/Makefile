
LIB=	pthread

SRCS=	pthread_attr.c pthread_barrier.c pthread.c pthread_condition.c \
	pthread_keys.c pthread_mutex.c pthread_rwlock.c pthread_spinlock.c \
	semaphore.c

INCFLAGS= -I$(SRCDIR) -I$(SRCDIR)../dietlibc -I$(SRCDIR)../dietlibc/cpu/$(CPU)

include $(SRCDIR)../lib.mk
