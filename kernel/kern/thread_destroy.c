/*
 * kern/thread_destroy.c - release all thread's ressources and destroy it
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
#include <list.h>
#include <thread.h>
#include <kmem.h>
#include <task.h>
#include <event.h>
#include <cluster.h>
#include <ppm.h>

static void do_thread_stat(struct thread_s *thread);

EVENT_HANDLER(thread_destroy_handler)
{
	struct thread_s *thread;

	thread = event_get_argument(event);
	thread_destroy(thread);
	return 0;
}

/* FIXME: unmap thread region, we need a fast lookup, use a cache of pgfault */
error_t thread_destroy(struct thread_s *thread)
{
	struct task_s *task;
	struct thread_s *this;
	struct cpu_s *cpu;
	uint_t pgfault_nr;
	uint_t spurious_pgfault_nr;
	uint_t remote_pages_nr;
	uint_t count;
	uint_t stack_addr;
	uint_t pid;
	uint_t tid;
	uint_t tm_start;
	uint_t tm_end;
	uint_t u_err_nr;
	uint_t m_err_nr;
	bool_t isUserThread;
	kmem_req_t req; 

	tm_start            = cpu_time_stamp();
	pgfault_nr          = thread->info.pgfault_cntr;
	spurious_pgfault_nr = thread->info.spurious_pgfault_cntr;
	remote_pages_nr     = thread->info.remote_pages_cntr;
	u_err_nr            = thread->info.u_err_nr;
	m_err_nr            = thread->info.m_err_nr;
	stack_addr          = (uint_t)thread->info.attr.stack_addr;
	task                = thread->task;
	this                = current_thread;
	pid                 = task->pid;
	tid                 = thread->info.order;
	cpu                 = thread_current_cpu(thread);

	assert(thread->state == S_DEAD && "Thread must be dead before destroying it !!");
    
	if(thread_isJoinable(thread))
	{
		assert(wait_queue_isEmpty(&thread->info.wait_queue) && 
		       "Queue must be empty, joiner Thread has been found");
	}

	thread->signature = 0;
	isUserThread = (thread->type == PTHREAD) ? true : false;

	if(isUserThread)
	{
		spinlock_lock(&task->th_lock);
		task->th_tbl[thread->info.order] = NULL;
		list_unlink(&thread->rope);
		do_thread_stat(thread); 
		count = task->threads_nr --;
		bitmap_set(task->bitmap, thread->info.order);
  
		if(thread->info.order < task->next_order)
			task->next_order = thread->info.order;
    
		task->vmm.pgfault_nr          += pgfault_nr;
		task->vmm.spurious_pgfault_nr += spurious_pgfault_nr;
		task->vmm.remote_pages_nr     += remote_pages_nr;
		task->vmm.u_err_nr            += u_err_nr;
		task->vmm.m_err_nr            += m_err_nr;

		spinlock_unlock(&task->th_lock);
 
#if 0
		region = keysdb_lookup(&task->vmm.regions_db, 
				       stack_addr >> CONFIG_VM_REGION_KEYWIDTH);
  
		if(thread_has_vmregion(thread))
		{
			rwlock_wrlock(&task->vmm.rwlock);
			vm_region_detach(&task->vmm, region);
			rwlock_unlock(&task->vmm.rwlock);
		}
#endif
	}

	spinlock_destroy(&thread->lock);
	cpu_context_destroy(&thread->pws);

	thread->task = NULL;
	req.type     = KMEM_PAGE; 
	req.ptr      = thread->info.page;
	kmem_free(&req);

	if(isUserThread && (count == 1))
		task_destroy(task);

	tm_end = cpu_time_stamp();

#if CONFIG_SHOW_THREAD_DESTROY_MSG
	printk(INFO, "INFO: %s: pid %d, tid %d (%x), cluster %d, cpu %d, tm %u [ %d, %d, %d, %d, %d ][%u]\n",
	       __FUNCTION__,
	       pid,
	       tid,
	       thread,
	       cpu->cluster->id,
	       cpu->lid,
	       tm_end - tm_start,
	       pgfault_nr,
	       spurious_pgfault_nr,
	       remote_pages_nr,
	       u_err_nr,
	       m_err_nr,
	       tm_end);
#endif


	return 0;
}


static void do_thread_stat(struct thread_s *thread)
{
}
