/*
 * kern/do_interrupt.c - kernel unified interrupt entry-point
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

#include <cpu.h>
#include <thread.h>
#include <cpu-trace.h>
#include <device.h>
#include <system.h>
#include <signal.h>

void do_interrupt(struct thread_s *this, uint_t irq_num)
{
	struct irq_action_s *action;
	struct cpu_s *cpu;
	register thread_state_t old_state;

	cpu_trace_write(thread_current_cpu(this), do_interrupt);

	cpu = thread_current_cpu(this);

	cpu->irq_nr ++;
	old_state = this->state;

	if(old_state == S_USR)
	{
		this->state = S_KERNEL;
		tm_usr_compute(this);
	}

	arch_cpu_get_irq_entry(cpu, irq_num, &action);
	action->irq_handler(action);
   
	if(old_state != S_USR)
	{
		if((this != cpu->event_mgr) && 
		   ((event_is_pending(&cpu->re_listner)) || 
		    (event_is_pending(&cpu->le_listner))))
		{
			(void)wakeup_one(&cpu->event_mgr->info.wait_queue, WAIT_ANY);
		}
     
		return;
	}

	if(event_is_pending(&cpu->re_listner))
		event_listner_notify(&cpu->re_listner);

	if(event_is_pending(&cpu->le_listner))
		event_listner_notify(&cpu->le_listner);

	if(thread_sched_isActivated(this))
	{
		thread_set_cap_migrate(this);
		sched_yield(this);
		thread_clear_cap_migrate(this);

		this = current_thread;
		cpu_wbflush();
	}

	tm_sys_compute(this);
	this->state = S_USR;
	signal_notify(this);
}
