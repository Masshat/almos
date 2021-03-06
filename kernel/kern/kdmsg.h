/*
 * kern/kdmsg.h - printk like functions and trace messages
 * 
 * Copyright (c) 2008,2009,2010,2011,2012 Ghassan Almaless
 * Copyright (c) 2011,2012 UPMC Sorbonne Universites
 *  
 * This file is part of ALMOS-kernel.
 *
 * ALMOS-kernel is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.0 of the License.
 *
 * ALMOS-kernel is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ALMOS-kernel; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _KDMSG_H_
#define _KDMSG_H_

#include <types.h>
#include <spinlock.h>
#include <mcs_sync.h>
#include <stdarg.h>

#define ERROR     DMSG_ERROR
#define WARNING   DMSG_WARNING
#define INFO      DMSG_INFO
#define DEBUG     DMSG_DEBUG
#define BOOT      DMSG_BOOT
#define ASSERT    DMSG_ASSERT

#define LEVEL        CONFIG_DMSG_LEVEL

extern spinlock_t printk_lock;
extern spinlock_t isr_lock;
extern spinlock_t exception_lock;
//extern spinlock_t boot_lock;
extern mcs_lock_t boot_lock;

typedef struct kdmsg_channel_s
{
	union
	{
		global_t val;
		uint_t id;
	};
}kdmsg_channel_t;

extern kdmsg_channel_t klog_tty;
extern kdmsg_channel_t kisr_tty;
extern kdmsg_channel_t kexcept_tty;

#define printk(level, args...)						\
	do{								\
		if((level) <= LEVEL)					\
			__fprintk(klog_tty.id, &printk_lock, args);	\
	}while(0)


#define isr_dmsg(level, args...)					\
	do{								\
		if((level) <= LEVEL)					\
			__fprintk(kisr_tty.id, &isr_lock, args);	\
	}while(0)

#define cpu_log_msg(...)					\
	do{							\
		__fprintk(cpu_get_id(), NULL, __VA_ARGS__);	\
	}while(0)


#if (ERROR <= LEVEL)
#define except_dmsg(args...) do { __fprintk(kexcept_tty.id, NULL, args); } while(0)
#else
#define except_dmsg(args...)
#endif

#if (BOOT <= LEVEL)
#define boot_dmsg(args...)   do { __arch_boot_dmsg(args); } while(0)
#else
#define boot_dmsg(args...)
#endif

void kdmsg_init();
void bdump(uint8_t *buff, size_t count);

int __perror (int fatal, const char *fmt, ...);
int __fprintk (int tty, spinlock_t *lock, const char *fmt, ...);
int __arch_boot_dmsg (const char *fmt, ...);

#define PANIC(MSG, args...)						\
	do{								\
		if(ERROR <= LEVEL)					\
			__perror(1,"PANIC at line: %d, file: %s , MSG: " MSG"\n", __LINE__,__FILE__, args); \
	} while(0)

#if (ASSERT <= LEVEL)
#define assert(expr) (void) ((expr) ? 0 : __perror(0,"cpu %d: Assert "#expr" faild, line %d, file %s [%u]\n", cpu_get_id(),__LINE__,__FILE__, cpu_time_stamp()))
#define bassert(expr) (void) ((expr) ? 0 : __arch_boot_dmsg("Assert "#expr" faild, line %d, file %s [%u]\n", __LINE__,__FILE__, cpu_time_stamp()))
#define assert2(_th,expr) (void) ((expr) ? 0 : __perror(0,"[ %x ] Assert "#expr" faild, line %d, file %s, Thread [%x] on CPU [%d], Current Thread [%x] on CPU [%d], [%u]\n", cpu_time_stamp(), __LINE__,__FILE__, _th, thread_current_cpu((_th))->gid, current_thread, cpu_get_id(), cpu_time_stamp()))
#else
#define assert(expr)
#define assert2(_th,expr)
#define bassert(expr)
#endif

/////////////////////////////////////////////////////////////////////////
//      per subsystem printk wrapper upon its debug configuration      //
/////////////////////////////////////////////////////////////////////////

#define dmsg(level, config, args...)		\
	do					\
	{					\
		if((level) <= (config))		\
			printk(DEBUG, args);	\
	} while(0)

#if CONFIG_VFAT_DEBUG
#define vfat_dmsg(level, args...)		\
	dmsg(level, CONFIG_VFAT_DEBUG, args)
#else
#define vfat_dmsg(args...)
#endif

#if CONFIG_EXT2_DEBUG
#define ext2_dmsg(level, args...)		\
	dmsg(level, CONFIG_EXT2_DEBUG, args)
#else
#define ext2_dmsg(args...)
#endif

#if CONFIG_DEVFS_DEBUG
#define devfs_dmsg(level, args...)		\
	dmsg(level, CONFIG_DEVFS_DEBUG, args)
#else
#define devfs_dmsg(args...)
#endif

#if CONFIG_SYSFS_DEBUG
#define sysfs_dmsg(level, args...)		\
	dmsg(level, CONFIG_SYSFS_DEBUG, args)
#else
#define sysfs_dmsg(args...)
#endif

#if CONFIG_VFS_DEBUG
#define vfs_dmsg(level, args...)		\
	dmsg(level, CONFIG_VFS_DEBUG, args)
#else
#define vfs_dmsg(args...)
#endif

#if CONFIG_MAPPER_DEBUG
#define mapper_dmsg(level, args...)		\
	dmsg(level, CONFIG_MAPPER_DEBUG, args)
#else
#define mapper_dmsg(args...)
#endif

#if CONFIG_KMEM_DEBUG
#define kmem_dmsg(args...) boot_dmsg(args)//printk(DEBUG, args)
#else
#define kmem_dmsg(args...)
#endif

#if CONFIG_KHM_DEBUG
#define khm_dmsg(args...) boot_dmsg(args)//printk(DEBUG, args)
#else
#define khm_dmsg(args...)
#endif

#if CONFIG_KCM_DEBUG
#define kcm_dmsg(args...) printk(DEBUG, args)
#else
#define kcm_dmsg(args...)
#endif

#if CONFIG_VMM_DEBUG
#define vmm_dmsg(level, args...)		\
	dmsg(level, CONFIG_VMM_DEBUG, args)
#else
#define vmm_dmsg(args...)
#endif

#if CONFIG_VMM_REGION_DEBUG
#define vmm_reg_dmsg(level, args...)			\
	dmsg(level, CONFIG_VMM_REGION_DEBUG, args)
#else
#define vmm_reg_dmsg(args...)
#endif

#if CONFIG_DQDT_DEBUG
#define dqdt_dmsg(level, args...)		\
	dmsg(level, CONFIG_DQDT_DEBUG, args)
#else
#define dqdt_dmsg(args...)
#endif

#if CONFIG_KFIFO_DEBUG
#define lffb_dmsg(level, args...)		\
	dmsg(level, CONFIG_KFIFO_DEBUG, args)
#else
#define lffb_dmsg(args...)
#endif

#if CONFIG_FORK_DEBUG
#define fork_dmsg(level, args...)		\
	dmsg(level, CONFIG_FORK_DEBUG, args)
#else
#define fork_dmsg(args...)
#endif


#if CONFIG_ELF_DEBUG
#define elf_dmsg(level, args...)		\
	dmsg(level, CONFIG_ELF_DEBUG, args)
#else
#define elf_dmsg(args...)
#endif


/////////////////////////////////////////////////////////////

#endif // _KDMSG_H_
