/*
 * kern/thread_migrate.c - thread migration logic
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
#include <dqdt.h>
#include <page.h>

typedef struct
{
	volatile bool_t     isDone;
	volatile error_t    err;
	struct thread_s     *thread;
	struct task_s       *task;
	struct cpu_s        *cpu;
	struct event_s      event;
}th_migrate_info_t;

error_t do_migrate(th_migrate_info_t *info);

EVENT_HANDLER(migrate_event_handler)
{
	struct cpu_s *cpu;
	th_migrate_info_t *rinfo;
	th_migrate_info_t linfo;
	error_t err;
	uint_t tm_start;
	uint_t tm_end;
	uint_t tid;
	uint_t pid;
  
	tm_start = cpu_time_stamp();

	rinfo = event_get_argument(event);
  
	linfo.thread  = rinfo->thread;
	linfo.task    = rinfo->task;
	linfo.cpu     = rinfo->cpu;

	tid = linfo.thread->info.order;
	pid = linfo.task->pid;

	cpu_wbflush();

	err = do_migrate(&linfo);

	tm_end = cpu_time_stamp();

	rinfo->err = err;
	cpu_wbflush();

	rinfo->isDone = true;
	cpu_wbflush();

	cpu = current_cpu;

	printk(INFO, "%s: pid %d, tid %d, [from] clstr %d, cpu %d --> [to] clstr %d, cpu %d, done [%u - %u]\n", 
	       __FUNCTION__,
	       pid,
	       tid,
	       linfo.cpu->cluster->id,
	       linfo.cpu->lid,
	       cpu->cluster->id,
	       cpu->lid,
	       tm_end,
	       tm_end - tm_start);

	return 0;
}

/* TODO: rework this function */
error_t thread_migrate(struct thread_s *thread)
{
	th_migrate_info_t info;
	struct dqdt_attr_s attr;
	struct thread_s *this;
	struct task_s *task;
	struct cpu_s *cpu;
	uint_t tm_bRemote;
	uint_t tm_aRemote;
	uint_t tid,pid;
	error_t err;
  
	this = current_thread;
	task = thread->task;
	cpu  = thread_current_cpu(this);
	tid  = this->info.order;
	pid  = task->pid;

	if(cpu->owner == this)
		cpu_fpu_context_save(&this->uzone);

	err = cpu_context_save(&this->info.pss);

	if(err != 0) return 0;

	info.thread  = this;
	info.task    = task;
	info.cpu     = cpu;

	event_set_argument(&info.event, &info);
	event_set_handler(&info.event, migrate_event_handler);
	event_set_priority(&info.event, E_MIGRATE);

	err = dqdt_thread_migrate(cpu->cluster->levels_tbl[0], &attr);

	if(err) return EAGAIN;

	event_send(&info.event, &attr.cpu->re_listner);
 
	cpu_wbflush();
  
	tm_bRemote = cpu_time_stamp();

	while(info.isDone == false)
		sched_yield(this);

	err = info.err;
	tm_aRemote = cpu_time_stamp();

	printk(INFO, "%s: pid %d, tid %d, cpu %d, done, err %d, e:%u, t:%u\n",
	       __FUNCTION__,
	       pid,
	       tid,
	       cpu_get_id(),
	       err,
	       tm_aRemote,
	       tm_aRemote - tm_bRemote);

	if(err) return err;

	sched_remove(this);
	return 0;	
}

error_t do_migrate(th_migrate_info_t *info)
{
	kmem_req_t req;
	struct page_s *page;
	struct cpu_s *cpu;
	struct thread_s *new;
	struct task_s *task;
	struct thread_s *thread;
	error_t err;

	cpu       = current_cpu;
	task      = info->task;
	thread    = info->thread;
	req.type  = KMEM_PAGE;
	req.size  = 0;
	req.flags = AF_KERNEL;
	page      = kmem_alloc(&req);

	if(page == NULL) return ENOMEM;

	new = ppm_page2addr(page);
  
	err = sched_register(new);
	/* FIXME: Redo registeration or/and reask 
	 * for a target core either directly (dqdt)
	 * or by returning EAGIN so caller can redo
	 * the whole action.*/
	assert(err == 0);
  
	/* TODO: Review locking */
	spinlock_lock(&task->th_lock);
	spinlock_lock(&thread->lock);

	memcpy(new, thread, PMM_PAGE_SIZE);

	thread_set_origin_cpu(new,info->cpu);
	thread_set_current_cpu(new,cpu);
	sched_setpolicy(new, new->info.attr.sched_policy);

	list_add_last(&task->th_root, &new->rope);
	list_unlink(&thread->rope);
	task->th_tbl[new->info.order] = new;

	spinlock_unlock(&thread->lock);
	spinlock_unlock(&task->th_lock);
  
	new->info.attr.cid     = cpu->cluster->id;
	new->info.attr.cpu_lid = cpu->lid;
	new->info.attr.cpu_gid = cpu->gid;
	new->info.page         = page;

	cpu_context_set_tid(&new->info.pss, (reg_t)new);
	cpu_context_dup_finlize(&new->pws, &new->info.pss);

	/* TODO: MIGRATE PAGES */

	sched_add(new);
	return 0;
}

