/*
 * kern/mcs_sync.c - ticket-based barriers and locks synchronization
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

#include <config.h>
#include <types.h>
#include <mcs_sync.h>
#include <thread.h>
#include <cpu.h>
#include <kdmsg.h>

void mcs_barrier_init(mcs_sync_t *ptr, char *name, uint_t count)
{
	ptr->val     = count;
	ptr->name    = name;
	ptr->phase   = 0;
	ptr->cntr    = count;
	ptr->ticket  = 0;
	ptr->ticket2 = 0;
}


void mcs_barrier_wait(mcs_sync_t *ptr)
{
	register uint_t phase;
	register uint_t order;
	uint_t *current;
	uint_t *next;
  
	phase   = ptr->phase;
	current = (phase == 0) ? &ptr->ticket : &ptr->ticket2;
	order   = cpu_atomic_add((void*)&ptr->cntr, -1);

	if(order == 1)
	{
		phase      = ~(phase) & 0x1;
		next       =  (phase == 0) ? &ptr->ticket : &ptr->ticket2;
		ptr->phase = phase;
		ptr->cntr  = ptr->val;
		*next      = 0;
		*current   = 1;
		cpu_wbflush();
		return;
	}

	while(cpu_load_word(current) == 0)
		;
}


void mcs_lock_init(mcs_sync_t *ptr, char *name)
{
	ptr->cntr   = 0;
	ptr->name   = name;
	ptr->ticket = 0;
}


void mcs_lock(mcs_sync_t *ptr, uint_t *irq_state)
{
	uint_t ticket;

	cpu_disable_all_irq(irq_state);
	ticket = cpu_atomic_add(&ptr->ticket, 1);

	while(ticket != cpu_load_word(&ptr->cntr))
		;

	current_thread->locks_count ++;
}

void mcs_unlock(mcs_sync_t *ptr, uint_t irq_state)
{
	ptr->cntr ++;
	cpu_wbflush();
	current_thread->locks_count --;
	cpu_restore_irq(irq_state);
}
