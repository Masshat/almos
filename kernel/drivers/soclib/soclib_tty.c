/*
 * soclib_tty.c - soclib tty driver
 *
 * Copyright (c) 2007,2008,2009,2010,2011,2012 Ghassan Almaless
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

#include <config.h>
#include <system.h>
#include <cpu.h>
#include <vfs.h>
#include <device.h>
#include <driver.h>
#include <drvdb.h>
#include <kmem.h>
#include <libk.h>
#include <soclib_tty.h>
#include <rwlock.h>
#include <cpu-trace.h>
#include <thread.h>
#include <mwmr.h>

/* This is a basic terminal mangement */

/* TODO: remove the usage of mwmr buffer and 
 * introduce in the kernel a specific terminal 
 * pool of buffers */

struct tty_context_s
{
	struct rwlock_s in_rwlock;
	struct rwlock_s out_rwlock;
	struct task_s *rd_owner;
	struct task_s *wr_owner;
	uint_t id;
	unsigned int eol;		/* End Of Line */
	dev_request_t *pending_rq;
	struct wait_queue_s wait_queue;
	struct fifomwmr_s *tty_buffer;
};

sint_t tty_open(struct device_s *tty, dev_request_t *rq)
{
	return 0;
}

/* TODO: introduce the notion of control-terminal */
sint_t tty_read(struct device_s *tty, dev_request_t *rq)
{
	struct tty_context_s *ctx; 
	register size_t size;
	uint_t irq_state;
	register char *dst;
	register size_t count,err;
	register uint32_t eol = 0;
   
	cpu_trace_write(current_cpu, tty_read);

	ctx = (struct tty_context_s*)tty->data;

	size  = 0;
	count = rq->count;
	dst   = rq->dst;
   
	if(rq->flags & DEV_RQ_NOBLOCK)
	{
		if((err=rwlock_trywrlock(&ctx->in_rwlock)) != 0)
			return -EAGAIN;
	}
	else
		rwlock_wrlock(&ctx->in_rwlock);

	spinlock_lock_noirq(&tty->lock, &irq_state);

	size = mwmr_read(ctx->tty_buffer, dst, count);
	eol = ctx->eol;
   
	if((eol) || (size == count) || (rq->flags & DEV_RQ_NOBLOCK))
	{
		ctx->eol = 0;
		spinlock_unlock_noirq(&tty->lock, irq_state);
		rwlock_unlock(&ctx->in_rwlock);
		return size;
	}

	rq->count -= size;
	rq->dst    = dst + size;

	ctx->pending_rq = rq;
	wait_on(&ctx->wait_queue, WAIT_LAST);

	spinlock_unlock_noirq(&tty->lock, irq_state);
	sched_sleep(current_thread);
	rwlock_unlock(&ctx->in_rwlock);

	return count - rq->count;
}

sint_t tty_write(struct device_s *tty, dev_request_t *rq)
{
	struct tty_context_s *ctx; 
	register unsigned int i;
	register size_t size = rq->count;
	volatile uint32_t *base = tty->base;
  
	cpu_trace_write(current_cpu, tty_write);
  
	ctx = (struct tty_context_s*)tty->data;
	rwlock_wrlock(&ctx->out_rwlock);

	for (i = 0; i < size; i++)
		base[TTY_WRITE_REG] = *((char *)rq->src + i);

	rwlock_unlock(&ctx->out_rwlock);
	return size;
}

sint_t tty_get_params(struct device_s *tty, dev_params_t *params)
{
	memset(params, 0, sizeof(*params));
	return 0;
}

char tty_get_status(struct device_s *tty)
{
	volatile uint32_t *base = tty->base;

	return base[TTY_STATUS_REG];
}

void tty_irq_handler(struct irq_action_s *action)
{
	char ch;
	struct device_s *tty;
	struct tty_context_s *ctx; 
	register char *dst;
	volatile uint32_t *base;
   
	cpu_trace_write(current_cpu, tty_irq_handler);

	tty  = action->dev;
	base = tty->base;
	ctx  = (struct tty_context_s*)tty->data;

	cpu_spinlock_lock(&tty->lock.val);

	while(tty_get_status(action->dev))
	{
		ch = base[TTY_READ_REG];
		if(ctx->pending_rq != NULL)
		{
			dst = ctx->pending_rq->dst;
			*(dst ++) = ch;
			ctx->pending_rq->dst = dst;
			ctx->pending_rq->count --;

			if((ch == '\n') || (ctx->pending_rq->count == 0))
			{
				wakeup_one(&ctx->wait_queue, WAIT_FIRST);
				ctx->pending_rq = NULL;
			}
		}
		else
		{
			mwmr_write(ctx->tty_buffer, &ch, 1);
			if(ch == '\n') ctx->eol = 1;
		}

#if CONFIG_TTY_ECHO_MODE
		switch(ch)
		{
		case '\b':
		case 0x7F:
			base[TTY_WRITE_REG] = '\b';
		base[TTY_WRITE_REG] = ' ';
		base[TTY_WRITE_REG] = '\b';
		break;
		default:
			base[TTY_WRITE_REG] = ch;
		}
#endif
	}

	cpu_spinlock_unlock(&tty->lock.val);
}

struct device_s *ttys_tbl[TTY_DEV_NR] = {NULL};
static uint_t tty_count = 0;

error_t soclib_tty_init(struct device_s *tty, void *base, uint_t size, uint_t irq)
{
	kmem_req_t req;
	struct tty_context_s *ctx;

	if(tty_count + 1 >= TTY_DEV_NR)
		return ERANGE;

	ttys_tbl[tty_count] = tty;

	spinlock_init(&tty->lock, "DevTTY");
	tty->base               = base;
	tty->irq                = irq;
	tty->type               = DEV_CHR;
	tty->action.dev         = tty;
	tty->action.irq_handler = &tty_irq_handler;
	tty->action.data        = NULL;
	tty->op.dev.open        = NULL;
	tty->op.dev.read        = &tty_read;
	tty->op.dev.write       = &tty_write;
	tty->op.dev.close       = NULL;
	tty->op.dev.lseek       = NULL;
	tty->op.dev.mmap        = NULL;
	tty->op.dev.munmap      = NULL;
	tty->op.dev.set_params  = NULL;
	tty->op.dev.get_params  = &tty_get_params;
	tty->op.drvid           = SOCLIB_TTY_ID;

	req.type  = KMEM_GENERIC;
	req.size  = sizeof(*ctx);
	req.flags = AF_BOOT | AF_ZERO;
  
	if((ctx = kmem_alloc(&req)) == NULL)
		return ENOMEM;
  
	ctx->id = tty_count;
	ctx->tty_buffer = mwmr_init(1,TTY_BUFFER_DEPTH, 0);
	rwlock_init(&ctx->in_rwlock);
	rwlock_init(&ctx->out_rwlock);
	ctx->eol  = 0;
	tty->data = (void*) ctx;
	sprintk(tty->name,
 
#if CONFIG_ROOTFS_IS_VFAT
		"TTY%d", 
#else
		"tty%d", 
#endif
		tty_count);

	metafs_init(&tty->node, tty->name);
	wait_queue_init(&ctx->wait_queue, tty->name);
	tty_count ++;
	return 0;
}

driver_t soclib_tty_driver = { .init = &soclib_tty_init };
