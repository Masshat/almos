/*
 * kern/thread_create.c - create new thread descriptor
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
#include <errno.h>
#include <kmem.h>
#include <kdmsg.h>
#include <kmagics.h>
#include <cpu.h>
#include <thread.h>
#include <task.h>
#include <list.h>
#include <scheduler.h>
#include <spinlock.h>
#include <cluster.h>
#include <ppm.h>
#include <pmm.h>
#include <page.h>

const char* const thread_state_name[THREAD_STATES_NR] = 
{
	"S_CREATE",
	"S_USR",
	"S_KERNEL",
	"S_READY",
	"S_WAIT",
	"S_DEAD"
};

const char* const thread_type_name[THREAD_TYPES_NR] =
{
	"USR",
	"KTHREAD",
	"IDLE"
};

error_t thread_create(struct task_s *task, pthread_attr_t *attr, struct thread_s **new_thread)
{
	kmem_req_t req;
	register struct thread_s *thread;
	struct page_s *page;

	// New Thread Ressources Allocation
	req.type  = KMEM_PAGE;
	req.size  = ARCH_THREAD_PAGE_ORDER;
	req.flags = AF_KERNEL | AF_ZERO | AF_REMOTE;
	req.ptr   = clusters_tbl[attr->cid].cluster;

#if CONFIG_THREAD_LOCAL_ALLOC
	req.ptr   = current_cluster;
#endif

	page      = kmem_alloc(&req);

	if(page == NULL) return EAGAIN;
  
	thread = (struct thread_s*) ppm_page2addr(page);
  
	// Initialize new thread
	spinlock_init(&thread->lock, "Thread");

	thread_set_current_cpu(thread, 
			       &clusters_tbl[attr->cid].cluster->cpu_tbl[attr->cpu_lid]);

	sched_setpolicy(thread, attr->sched_policy);
	thread->task = task;
	thread->type = PTHREAD;

	if(attr->isDetached)
		thread_clear_joinable(thread);
	else
		thread_set_joinable(thread);

	signal_init(thread);
	attr->tid = (uint_t)thread;
	attr->pid = task->pid;
	memcpy(&thread->info.attr, attr, sizeof(*attr));
	thread->info.kstack_addr = thread;
	thread->info.kstack_size = PMM_PAGE_SIZE << ARCH_THREAD_PAGE_ORDER;
	thread->info.page = page;

#if CONFIG_PPM_USE_INTERLEAVE
	thread->info.ppm_last_cid = attr->cid;
#endif

	thread->signature = THREAD_ID;
  
	wait_queue_init(&thread->info.wait_queue, "Join/Exit Sync");
	cpu_context_init(&thread->pws, thread); 

	// Set referenced thread pointer to new thread address
	*new_thread = thread;

	return 0;
}
