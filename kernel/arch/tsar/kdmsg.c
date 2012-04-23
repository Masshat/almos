/*
 * kdmsg.c - output kernel debug/trace/information to an available terminal
 * (see kern/kdmsg.h)
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

#include <types.h>
#include <cpu.h>
#include <device.h>
#include <soclib_tty.h>
#include <spinlock.h>
#include <mcs_sync.h>
#include <libk.h>
#include <kdmsg.h>

spinlock_t exception_lock;
spinlock_t printk_lock;
spinlock_t isr_lock;
//spinlock_t boot_lock;
mcs_sync_t boot_lock;
mcs_sync_t printk_sync;

kdmsg_channel_t klog_tty = {{.id = 0}};
kdmsg_channel_t kisr_tty = {{.id = 0}};
kdmsg_channel_t kexcept_tty = {{.id = 0}};
kdmsg_channel_t kboot_tty;
static bool_t boot_stage_done = false;

void kdmsg_init()
{
	uint_t tty_count;
  
	spinlock_init(&exception_lock, "Exception");
	spinlock_init(&printk_lock, "Printk");
	spinlock_init(&isr_lock, "ISR");
	mcs_lock_init(&printk_sync, "printk");
  
	tty_count = 0;
	while((tty_count < TTY_DEV_NR) && (ttys_tbl[tty_count++] != NULL));
  
	if(tty_count >= 3)
	{
		kboot_tty.id = (uint_t)ttys_tbl[0]->base;
		kisr_tty.id = 3;
		kexcept_tty.id = 3;
	}
	boot_stage_done = true;
	cpu_wbflush();
}

int __arch_boot_dmsg (const char *fmt, ...)
{
	va_list ap;
	int count;
	uint_t irq_state;
	char *base;
   
   
	base  = (char*)((uint_t*)kboot_tty.id + TTY_WRITE_REG);
	mcs_lock(&boot_lock, &irq_state);

	va_start (ap, fmt);

	count = iprintk (base, 0, (char *) fmt, ap);

	va_end (ap);
	mcs_unlock(&boot_lock, irq_state);
	return count;
}

int __fprintk (int tty, spinlock_t *lock, const char *fmt, ...)
{
	va_list ap;
	int count;
	uint_t irq_state;

	va_start (ap, fmt);

	if(lock)
	{
		if(lock == &printk_lock)
			mcs_lock(&printk_sync, &irq_state);
		else
			cpu_spinlock_lock(&ttys_tbl[tty]->lock.val);
	}
  
	count = iprintk ((char*)(ttys_tbl[tty]->base + TTY_WRITE_REG), 0, (char *) fmt, ap);
   
	if(lock)
	{
		if(lock == &printk_lock)
			mcs_unlock(&printk_sync, irq_state);
		else
			cpu_spinlock_unlock(&ttys_tbl[tty]->lock.val);
	}

	va_end (ap);
	return count;
}

#include <thread.h>

int __perror (int fatal, const char *fmt, ...)
{
	va_list ap;
	int count;

	va_start (ap, fmt);
	count = iprintk ((char*)(ttys_tbl[kexcept_tty.id]->base + TTY_WRITE_REG), 0, (char *) fmt, ap);

	va_end (ap);
   
	if(fatal) while(1);
	return count;
}

void bdump(uint8_t *buff, size_t count)
{
	uint8_t b1, b2;
	char *tab = "0123456789ABCDEF";

	spinlock_lock(&ttys_tbl[klog_tty.id]->lock);

	while(count--) 
	{
		b1 = tab[(*buff & 0xF0)>>4];
		b2 = tab[*buff & 0x0F];
		buff++;
		*(char*)(ttys_tbl[klog_tty.id]->base + TTY_WRITE_REG) = b1;
		*(char*)(ttys_tbl[klog_tty.id]->base + TTY_WRITE_REG) = b2;
	}
	*(char*)(ttys_tbl[klog_tty.id]->base + TTY_WRITE_REG) = '\n';
	spinlock_unlock (&ttys_tbl[klog_tty.id]->lock);
}
