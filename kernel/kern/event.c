/*
 * kern/event.c - Per-CPU Events-Manager
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
#include <cluster.h>
#include <device.h>
#include <event.h>
#include <cpu-trace.h>
#include <thread.h>


/** Initialize event */
error_t event_init(struct event_s *event)
{
	/* nothing to do in this version */
	return 0;
}

/** Destroy event */
void event_destroy(struct event_s *event)
{
	/* nothing to do in this version */
}

/** Destroy event listner */
void event_listner_destroy(struct event_listner_s *el)
{
	/* nothing to do in this version */
}

error_t event_listner_init(struct event_listner_s *el, uint_t type)
{
	error_t err;
	register uint_t i;
  
	el->type = type;
	el->flags = 0;
	el->prio = 0;
	err = 0;

	if(type == EL_LOCAL)
	{
		for(i=0; i < E_PRIO_NR; i++)
		{
			list_root_init(&el->tbl[i].list.root);
			el->tbl[i].list.count = 0;
		}

		return 0;
	}

	for(i=0; i < E_PRIO_NR; i++)
	{
		err = lffb_init(&el->tbl[i].lffb, CONFIG_REL_LFFB_SIZE, LFFB_MW);
    
		if(err) goto eli_lffb_err;
	}

	return 0;

eli_lffb_err:
	if(i != 0)
		lffb_destroy(&el->tbl[i].lffb);

	return err;
}


static void local_event_send(struct event_s *event, struct event_listner_s *el, bool_t isFIFO) 
{
	uint_t prio;

	prio = event_get_priority(event);

	if(isFIFO)
		list_add_last(&el->tbl[prio].list.root, &event->e_list);
	else
		list_add_first(&el->tbl[prio].list.root, &event->e_list);

	el->tbl[prio].list.count ++;
	el->prio = (!(el->flags & EVENT_PENDING) || (el->prio > prio)) ? prio : el->prio;
	el->flags |= EVENT_PENDING;

#if CONFIG_SHOW_LOCAL_EVENTS
	isr_dmsg(INFO, "%s: cpu %d, prio %d, el->prio %d [%d]\n", 
		 __FUNCTION__, 
		 cpu_get_id(), 
		 prio, 
		 el->prio,
		 cpu_time_stamp());
#endif
}

static EVENT_HANDLER(re_send_backoff_handler)
{
	struct event_listner_s *rel;
  
	rel = event_get_argument(event);
  
	event_restore(event);
  
	event_send(event,rel);

	printk(INFO, "INFO: %s, cpu %d, event %x sent to el %x [%d]\n",
	       __FUNCTION__,
	       cpu_get_id(),
	       event,
	       rel,
	       cpu_time_stamp());
  
	return 0;
}

static error_t __attribute__((noinline)) re_send_backoff(struct event_s *event, 
							 struct event_listner_s *el, 
							 uint_t tm_stamp)
{
	struct cpu_s *cpu;
	uint_t tm_now;
	uint_t irq_state;

	tm_now = cpu_time_stamp();
	cpu    = current_cpu;

	if((tm_now - tm_stamp) < cpu_get_ticks_period(cpu))
		return EAGAIN;

	event_backup(event);
  
	event_set_priority(event, E_CHR);
	event_set_handler(event, re_send_backoff_handler);
	event_set_argument(event, el);

	cpu_disable_all_irq(&irq_state);
	local_event_send(event, &cpu->le_listner, false);
	cpu_restore_irq(irq_state);

	printk(INFO, "INFO: %s, cpu %d, event %x sent to el %x [%d]\n",
	       __FUNCTION__,
	       cpu->gid,
	       event,
	       el,
	       tm_now);

	return 0;
}

static void remote_event_send(struct event_s *event, struct event_listner_s *el)
{ 
	error_t retry;
	uint_t tm_stamp;
	uint_t prio;
	struct cpu_s *cpu;

	tm_stamp = cpu_time_stamp();
	prio     = event_get_priority(event);

	while((retry = lffb_put(&el->tbl[prio].lffb, event)) != 0)
	{
		retry = re_send_backoff(event, el, tm_stamp);
    
		if(retry == 0)
			break;
	}

	el->flags |= EVENT_PENDING;
	cpu_wbflush();
  
	if(prio < E_FUNC)
	{
		cpu = event_listner_get_cpu(el,re_listner);
		(void)arch_cpu_send_ipi(cpu);
	}
}

/* must be called with all irq disabled */
void event_send(struct event_s *event, struct event_listner_s *el)
{
	switch(el->type)
	{
	case EL_LOCAL:
		local_event_send(event, el, true);
		return;
	case EL_REMOTE:
		remote_event_send(event, el);
		return;
	default:
		PANIC("Curropted event listner structure [cpu %d, type %d, cycle %u]\n", 
		      cpu_get_id(), 
		      el->type,
		      cpu_time_stamp());
	}
}


static void local_event_listner_notify(struct event_listner_s *el)
{
	register struct event_s *event;
	register uint_t count;
	register uint_t start_prio;
	register uint_t current_prio;

#if CONFIG_SHOW_LOCAL_EVENTS
	register uint_t cntr;
#endif
  
	count = 0;
	assert((el->flags & EVENT_PENDING) && "event_notify is called but no event is pending");

	start_prio   = el->prio ++;
	current_prio = start_prio;

	while(current_prio < E_PRIO_NR)
	{
		if(el->prio <= start_prio)
		{
			start_prio   = el->prio ++;
			current_prio = start_prio;
		}

		while(el->tbl[current_prio].list.count != 0)
		{
			assert(!(list_empty(&el->tbl[current_prio].list.root)) && "pending event queue is empty");

			event = list_first(&el->tbl[current_prio].list.root, struct event_s, e_list);
			list_unlink(&event->e_list);
      
			el->tbl[current_prio].list.count --;
      
			cpu_enable_all_irq(NULL);
			event_get_handler(event)(event);	 /* el->prio can be changed */
			count ++;

#if CONFIG_SHOW_LOCAL_EVENTS
			cntr ++;
#endif

			cpu_disable_all_irq(NULL);
		}

#if CONFIG_SHOW_LOCAL_EVENTS    
		cpu_enable_all_irq(NULL);
		if(cntr)
		{
			printk(INFO, "INFO: cpu %d, %d pending events of priority %d have been Delivered [ %u ]\n",
			       cpu_get_id(),
			       cntr, 
			       current_prio, 
			       cpu_time_stamp());
		}
		cntr = 0;
		cpu_disable_all_irq(NULL);
#endif
    
		current_prio ++;
	}
	el->flags &= ~EVENT_PENDING;
	el->count += count;
}


static void remote_event_listner_notify(struct event_listner_s *el)
{
	struct event_listner_s *local_el;
	struct event_s *event;
	event_prio_t current_prio;
	uint_t irq_state;
	uint_t count;
  
	local_el = &current_cpu->le_listner;
	count    = 0;

	do
	{
		el->flags &= ~EVENT_PENDING;
		cpu_disable_all_irq(&irq_state);

		for(current_prio = E_CLK; current_prio < E_PRIO_NR; current_prio++, count++)
		{
			while((event = lffb_get(&el->tbl[current_prio].lffb)) != NULL)
				local_event_send(event, local_el, false);
		}

		cpu_restore_irq(irq_state);
		el->count += count;

	}while(el->flags & EVENT_PENDING);
}

/* must be called with all irq disabled */
void event_listner_notify(struct event_listner_s *el)
{
	switch(el->type)
	{
	case EL_LOCAL:
		local_event_listner_notify(el);
		return;
	case EL_REMOTE:
		remote_event_listner_notify(el);
		return;
	default:
		PANIC("Curropted event listner structure [CPU %d, type %d]\n", cpu_get_id(), el->type);
	}
}


void* thread_event_manager(void *arg)
{
	struct thread_s *this;
	struct cpu_s *cpu;
	uint_t irq_state;

	cpu_enable_all_irq(NULL);
  
	this = current_thread;
	cpu  = current_cpu;

	thread_preempt_disable(this);

	while(1)
	{
#if 0
		printk(INFO, "INFO: Event Handler on CPU %d, event is le pending %d, re pending %d [%d,%d]\n",
		       cpu->gid,
		       event_is_pending(&cpu->le_listner),
		       event_is_pending(&cpu->re_listner),
		       cpu_time_stamp(),
		       cpu_get_ticks(cpu)); 
#endif
   
		if(event_is_pending(&cpu->re_listner))
			event_listner_notify(&cpu->re_listner);

		cpu_disable_all_irq(&irq_state);

		if(event_is_pending(&cpu->le_listner))
			event_listner_notify(&cpu->le_listner);
    
		wait_on(&this->info.wait_queue, WAIT_ANY);

		sched_sleep(this);

		cpu_restore_irq(irq_state);
	}

	return NULL;
}
