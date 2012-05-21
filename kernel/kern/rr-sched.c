/*
 * kern/rr-sched.c - Round-Robin scheduling policy
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
#include <types.h>
#include <list.h>
#include <spinlock.h>
#include <thread.h>
#include <task.h>
#include <kmem.h>
#include <scheduler.h>
#include <kdmsg.h>

#if CONFIG_SCHED_RR_CHECK
#define sched_assert(cond) assert((cond))
#define SCHED_SCOPE
#else
#define sched_assert(cond)
#define SCHED_SCOPE static
#endif

#undef SCHED_SCOPE
#define SCHED_SCOPE

#define RR_QUANTUM        4	/* in TICs number */
#define RR_QUEUE_NR       5
#define RR_MAX_PRIO       5

typedef struct rQueues_s
{
	uint_t clock_cntr;
	uint_t period;
	struct list_entry root;
	struct list_entry root_tbls[RR_QUEUE_NR];
} rQueues_t;


void __attribute__ ((noinline)) rr_sched_load_balance(struct thread_s *this,
						      struct sched_s *sched, 
						      rQueues_t *rQueues)
{
	struct thread_s *victim;
	struct thread_s *thread;
	struct list_entry *iter;
	struct cpu_s *cpu;
	register uint_t min;

	cpu = current_cpu;

#if CONFIG_SCHED_DEBUG
	isr_dmsg(INFO, "%s: started cpu %d, u_rannable %d, busy %d, usage %d\n", 
		 __FUNCTION__, 
		 cpu_get_id(),
		 cpu->scheduler.u_runnable,
		 cpu->busy_percent,
		 cpu->usage);
#endif

	this->boosted_prio >>= 1;
	victim             = NULL;
	min                = rQueues->period;

	list_foreach(&rQueues->root, iter)
	{
		thread = list_element(iter, struct thread_s, list);
		
		if((thread->type == PTHREAD)    && 
		   (thread->ticks_nr > 1)       && 
		   (thread->boosted_prio < min) &&
		   thread_isCapMigrate(thread))
		{
			victim = thread;
			min    = victim->boosted_prio;
		}


		thread->boosted_prio >>= 1;
	}

#if CONFIG_SCHED_DEBUG
	isr_dmsg(INFO, "%s: cpu %d ended, min %d, victim %x\n", 
		 __FUNCTION__, 
		 cpu_get_id(), 
		 min,
		 victim);
#endif

	if(min == 0)
		thread_migration_activate(victim);
}


SCHED_SCOPE void rr_clock(struct thread_s *this, uint_t ticks_nr)
{
	register struct sched_s *sched;
	register rQueues_t *rQueues;

	this->quantum -= 1;

	if((this->quantum == 0) && (this->type != TH_IDLE))
		thread_sched_activate(this);

	sched   = this->local_sched;
	rQueues = (rQueues_t*) sched->data;
	
	rQueues->clock_cntr ++;

	if(rQueues->clock_cntr == rQueues->period)
	{
		rQueues->clock_cntr = 0;
		rr_sched_load_balance(this, sched, rQueues);
	}
}

SCHED_SCOPE void rr_sched_strategy(struct sched_s *sched)
{
}

SCHED_SCOPE void rr_yield(struct thread_s *this)
{
	sched_assert(this->state == S_KERNEL && "Unexpected yield op");
}

SCHED_SCOPE void rr_remove(struct thread_s *this)
{
	sched_assert(this->type == PTHREAD && "Unexpected remove op");
  
	thread_current_cpu(this)->scheduler.total_nr --;
	thread_current_cpu(this)->scheduler.user_nr --;
	cpu_wbflush();

	sched_assert(this->state == S_KERNEL && "Unexpected remove op");
}


SCHED_SCOPE void rr_exit(struct thread_s *this)
{
	thread_current_cpu(this)->scheduler.total_nr --;

	if(this->type == PTHREAD)
		thread_current_cpu(this)->scheduler.user_nr --;

	sched_assert(this->state == S_KERNEL && "Unexpected exit op");
	this->state = S_DEAD;
}

SCHED_SCOPE void rr_sleep(struct thread_s *this)
{
	sched_assert((this->state == S_KERNEL) && "Unexpected sleep op");
	this->state = S_WAIT;	
}

SCHED_SCOPE void rr_wakeup (struct thread_s *thread)
{
	register struct sched_s *sched;
	register rQueues_t *rQueues;
	register thread_type_t type;
   
	if(thread->state == S_READY)
		return;
	
	sched_assert((thread->state == S_WAIT) && "Unexpected sleep wakeup op");

	thread->state = S_READY;

	sched   = thread->local_sched;
	type    = thread->type;
	rQueues = (rQueues_t*) sched->data;

	sched->count ++;

	if(type == KTHREAD)
	{
		thread_sched_activate(current_thread);
		thread_current_cpu(thread)->scheduler.k_runnable ++;
		list_add_first(&rQueues->root, &thread->list);
	}
	else
	{
		thread_current_cpu(thread)->scheduler.u_runnable ++;
		list_add_last(&rQueues->root, &thread->list);
	}
}

SCHED_SCOPE void rr_add_created(struct thread_s *thread)
{
	register struct sched_s *sched;
	register rQueues_t *rQueues;
	register thread_type_t type;

	sched = thread->local_sched;
	rQueues = (rQueues_t*) sched->data;
	type = thread->type;
	sched->count ++;
	thread_current_cpu(thread)->scheduler.total_nr ++;

	if(type == KTHREAD)
	{
		thread_current_cpu(thread)->scheduler.k_runnable ++;
		list_add_first(&rQueues->root, &thread->list);
	}
	else
	{
		thread_current_cpu(thread)->scheduler.user_nr ++;
		thread_current_cpu(thread)->scheduler.u_runnable ++;
		list_add_last(&rQueues->root, &thread->list);
	}
}

SCHED_SCOPE struct thread_s *rr_elect(struct sched_s *sched)
{
	register struct thread_s *elected;
	register rQueues_t *rQueues;
	register struct thread_s *this;
	register uint_t count;

	this = current_thread;
	thread_sched_deactivate(this);
	rQueues = (rQueues_t*) sched->data;
	elected = NULL;
  
	count = sched->count;

	if(this->type != TH_IDLE)
	{
		this->boosted_prio += this->quantum;

		if(this->state == S_KERNEL)
		{
			this->state = S_READY;
			
			list_add_last(&rQueues->root, &this->list);
			count ++;

			if(this->type == PTHREAD)
				thread_current_cpu(this)->scheduler.u_runnable ++;
			else
				thread_current_cpu(this)->scheduler.k_runnable ++;
		}
	}

	if(count > 0)
	{
		elected = list_first(&rQueues->root, struct thread_s, list);
		list_unlink(&elected->list);
		count --;
   
		if(elected->type == PTHREAD)
			thread_current_cpu(elected)->scheduler.u_runnable --;
		else
			thread_current_cpu(elected)->scheduler.k_runnable --;

		if((elected->state != S_CREATE) && (elected->state != S_READY))
			except_dmsg("cpu %d, tid %x, state %s\n",
				    cpu_get_id(), 
				    elected, 
				    thread_state_name[elected->state]);

		elected->quantum = RR_QUANTUM;
		thread_sched_deactivate(elected);
	}
   
	sched->count = count;
	return elected;
}


static const struct sched_ops_s rr_sched_op = 
{
	.exit   = &rr_exit,
	.elect  = &rr_elect,
	.yield  = &rr_yield,
	.sleep  = &rr_sleep,
	.wakeup = &rr_wakeup,
	.strategy = &rr_sched_strategy,
	.clock  = &rr_clock,
	.add_created = &rr_add_created,
	.add = &rr_add_created,
	.remove = &rr_remove,
};

error_t rr_sched_init(struct sched_s *sched)
{
	kmem_req_t req;
	register rQueues_t *rQueues;
	register uint_t i;

	req.type  = KMEM_GENERIC;
	req.size  = sizeof(rQueues_t);
	req.flags = AF_BOOT | AF_ZERO;
	sched->count = 0;

	if((rQueues = kmem_alloc(&req)) == NULL)
		PANIC("rr-sched.init: fatal error NOMEM\n", 0);

	rQueues->period = CONFIG_CPU_BALANCING_PERIOD * RR_QUANTUM;
  
	list_root_init(&rQueues->root);

	for(i=0; i < RR_QUEUE_NR; i++)
		list_root_init(&rQueues->root_tbls[i]);

	sched->data = rQueues;
	sched->op = rr_sched_op;
	return 0;
}
