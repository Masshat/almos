/*
 * soclib_block.c - soclib block-device (hdd ctrl) driver
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

#include <device.h>
#include <driver.h>
#include <drvdb.h>
#include <cpu.h>
#include <thread.h>
#include <task.h>
#include <scheduler.h>
#include <kmem.h>
#include <errno.h>
#include <soclib_block.h>
#include <string.h>
#include <event.h>
#include <cpu-trace.h>

/* Block device mapped registers offset */
#define BLK_DEV_BUFFER_REG	0
#define BLK_DEV_LBA_REG	        1
#define BLK_DEV_COUNT_REG	2
#define BLK_DEV_OP_REG	        3
#define BLK_DEV_STATUS_REG	4
#define BLK_DEV_IRQ_ENABLE_REG	5
#define BLK_DEV_SIZE_REG	6
#define BLK_DEV_BLOCK_SIZE_REG	7

/* Block device operations types */
#define BLK_DEV_NOOP	        0
#define BLK_DEV_READ	        1
#define BLK_DEV_WRITE	        2

/* Block device status */
#define BLK_DEV_IDLE	        0
#define BLK_DEV_BUSY	        1
#define BLK_DEV_READ_SUCCESS	2
#define BLK_DEV_WRITE_SUCCESS	3
#define BLK_DEV_READ_ERROR	4
#define BLK_DEV_WRITE_ERROR	5
#define BLK_DEV_ERROR	        6

struct block_params_s
{
	uint32_t blk_count;
	uint32_t blk_size;
};

struct block_context_s
{
	struct list_entry request_queue;
	struct wait_queue_s pending;
	struct block_params_s params;
};

static void block_start_request(struct device_s *block, dev_request_t *rq, uint32_t type)
{
	volatile uint32_t *base = block->base;

	base[BLK_DEV_IRQ_ENABLE_REG] = 1;
	base[BLK_DEV_BUFFER_REG]     = (uint32_t)rq->dst;
	base[BLK_DEV_LBA_REG]        = (uint32_t)rq->src;
	base[BLK_DEV_COUNT_REG]      = rq->count;
	base[BLK_DEV_OP_REG]         = type;
}

void __attribute__ ((noinline)) block_irq_handler(struct irq_action_s *action)
{
	register struct block_context_s *ctx;
	register struct device_s *block;
	register dev_request_t *rq;
	register dev_request_t *new_rq;
	register uint32_t err;
	register struct thread_s *this;
	volatile uint32_t *base;
	struct thread_s *thread;

	cpu_trace_write(current_thread()->local_cpu, block_irq_handler);

	block = action->dev;
	base  = block->base;
	ctx   = (struct block_context_s*)block->data;

	cpu_spinlock_lock(&block->lock.val);

	err = base[BLK_DEV_STATUS_REG]; /* IRQ ACK */

	if(list_empty(&ctx->request_queue))
	{
		cpu_spinlock_unlock(&block->lock.val);
		isr_dmsg(WARNING, "WARNING: Recived irq on DevBlk but no request is pending [CPU %d]\n", 
			 cpu_get_id());
		return;
	}

	rq = list_first(&ctx->request_queue, dev_request_t, list);
	list_unlink(&rq->list);

	if(!(list_empty(&ctx->request_queue)))
	{
		new_rq = list_first(&ctx->request_queue, dev_request_t, list);
		block_start_request(block,new_rq,(uint32_t)new_rq->data);
	}

	if((err != BLK_DEV_READ_SUCCESS) && (err != BLK_DEV_WRITE_SUCCESS))
		rq->err = 1;
	else
		rq->err = 0;
  
	this = current_thread;

	if(!(rq->flags & DEV_RQ_NOBLOCK))
	{
		thread=wakeup_one(&ctx->pending, WAIT_FIRST);
		cpu_spinlock_unlock(&block->lock.val);
		return;
	}

	cpu_spinlock_unlock(&block->lock.val);

	event_set_error(&rq->event, rq->err);
	event_set_senderId(&rq->event, block);
	event_set_priority(&rq->event, E_BLK);
	event_send(&rq->event, &current_cpu->le_listner);
}


int32_t __attribute__ ((noinline)) block_request(struct device_s *blk_dev, dev_request_t *rq, uint32_t type)
{
	struct thread_s *this;
	struct block_context_s *ctx;
	uint_t irq_state;

	cpu_trace_write(current_cpu, block_request);
  
	rq->dst = task_vaddr2paddr(current_task, rq->dst);
	ctx     = (struct block_context_s*)blk_dev->data;
	this    = current_thread;
  
	if((rq->count + ((uint_t)rq->src)) > ctx->params.blk_count)
		return -1;

	rq->data = (void*)type;
	spinlock_lock_noirq(&blk_dev->lock, &irq_state); /* FIXME: should to be selective irq mask */ 

	if(list_empty(&ctx->request_queue))
	{
		list_add(&ctx->request_queue, &rq->list);
		block_start_request(blk_dev, rq, type);
	}
	else
		list_add_last(&ctx->request_queue, &rq->list);
  
	if(rq->flags & DEV_RQ_NOBLOCK)
	{
		spinlock_unlock_noirq(&blk_dev->lock, irq_state);
		return 0;
	}

	wait_on(&ctx->pending, WAIT_LAST);
	spinlock_unlock_noirq(&blk_dev->lock, irq_state);
	sched_sleep(this);
  
#if 0
	printk(DEBUG, "DEBUG: %s: cpu %d: Ended , err %d\n", 
	       __FUNCTION__, 
	       cpu_get_id(), 
	       rq->err);
#endif

	return rq->err;
}


static sint_t block_read(struct device_s *blk_dev, dev_request_t *rq)
{
	return block_request(blk_dev, rq, BLK_DEV_READ);
}


static sint_t block_write(struct device_s *blk_dev, dev_request_t *rq)
{
	return block_request(blk_dev, rq, BLK_DEV_WRITE);
}


static sint_t block_get_params(struct device_s *blk_dev, dev_params_t *params) 
{
	struct block_context_s *ctx;

	ctx = (struct block_context_s*) blk_dev->data;
 
	params->count       = ctx->params.blk_count;
	params->size        = params->count * ctx->params.blk_size;
	params->sector_size = ctx->params.blk_size;

	return 0;
}

static sint_t block_open(struct device_s *blk_dev, dev_request_t *rq)
{
	return EPERM;
}


struct device_s * __sys_blk;
static uint_t sda_count = 0;

error_t soclib_block_init(struct device_s *block, void *base, uint_t size, uint_t irq)
{
	kmem_req_t req;
	struct block_context_s *ctx;
  
	spinlock_init(&block->lock, "DevBlk (SoCLib)");
	block->base = base;
	block->irq  = irq;
	block->type = DEV_BLK;
  
	block->action.dev         = block;
	block->action.irq_handler = &block_irq_handler;
	block->action.data        = NULL;

	if(sda_count == 0)
		__sys_blk = block;

	sprintk(block->name, "sda%d", sda_count++);
	metafs_init(&block->node, block->name);
  
	block->op.dev.open       = &block_open;
	block->op.dev.read       = &block_read;
	block->op.dev.write      = &block_write;
	block->op.dev.close      = NULL;
	block->op.dev.lseek      = NULL;
	block->op.dev.mmap       = NULL;
	block->op.dev.munmap     = NULL;
	block->op.dev.set_params = NULL;
	block->op.dev.get_params = &block_get_params;
	block->op.drvid          = SOCLIB_BLKDEV_ID;

	req.type  = KMEM_GENERIC;
	req.size  = sizeof(*ctx);
	req.flags = AF_BOOT | AF_ZERO;

	if((ctx = kmem_alloc(&req)) == NULL)
		return ENOMEM;

	list_root_init(&ctx->request_queue);
	wait_queue_init(&ctx->pending, block->name);
  
	ctx->params.blk_size  = *(((uint32_t*)base) + BLK_DEV_BLOCK_SIZE_REG);
	ctx->params.blk_count = *(((uint32_t*)base) + BLK_DEV_SIZE_REG);
	block->data = (void *)ctx;
	return 0;
}

driver_t soclib_blkdev_driver = { .init = &soclib_block_init };
