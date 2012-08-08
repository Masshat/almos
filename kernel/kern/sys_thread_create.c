/*
 * kern/sys_thread_create.c - creates new user thread
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

#include <errno.h>
#include <config.h>
#include <cpu.h>
#include <cluster.h>
#include <event.h>
#include <list.h>
#include <thread.h>
#include <scheduler.h>
#include <kmem.h>
#include <task.h>
#include <rwlock.h>
#include <dqdt.h>

#define MAX_CPU_NR (CONFIG_MAX_CLUSTER_NR * CONFIG_MAX_CPU_PER_CLUSTER_NR)

typedef struct
{
	volatile bool_t   isDone;
	volatile error_t  err;
	bool_t            isPinned;
	struct thread_s   *new_thread;
	void              *sched_listner;
	struct event_s    event;
	pthread_t         key; 
	pthread_attr_t    *attr;
	struct task_s     *task;
}thread_info_t;

error_t do_thread_create(thread_info_t *info);

EVENT_HANDLER(thread_create_event_handler)
{
	pthread_attr_t attr;
	thread_info_t  linfo;
	thread_info_t *rinfo;
	error_t err;

	rinfo = event_get_argument(event);
	memcpy(&attr, rinfo->attr, sizeof(attr));

	linfo.isPinned = rinfo->isPinned;
	linfo.key      = rinfo->key;
	linfo.attr     = &attr;
	linfo.task     = rinfo->task;

	err = do_thread_create(&linfo);
  
	rinfo->sched_listner = linfo.sched_listner;
	rinfo->new_thread    = linfo.new_thread;
	rinfo->err           = err;

	cpu_wbflush();
	rinfo->isDone = true;
	cpu_wbflush();
	return 0;
}

int sys_thread_create (pthread_t *tid, pthread_attr_t *thread_attr)
{
	thread_info_t info;
	pthread_attr_t attr;
	struct thread_s *this;
	register struct task_s *task;
	register error_t err;
	sint_t order;
	pthread_t new_key; 
	uint_t sched_event;
	uint_t tm_start;
	uint_t tm_bRemote;
	uint_t tm_aRemote;
  
	tm_start = cpu_time_stamp();
	this     = current_thread;
	task     = this->task;

	if((tid == NULL) || (thread_attr == NULL))
	{
		err = EINVAL;
		goto fail_inval;
	}

	if(((uint_t)thread_attr >= CONFIG_KERNEL_OFFSET) || 
	   (((uint_t)tid + 8) >= CONFIG_KERNEL_OFFSET))
	{
		err = EPERM;
		goto fail_access;
	}
  
	if((err = cpu_uspace_copy(&attr, thread_attr, sizeof(attr)))) 
		goto fail_ucopy_attr;

	if((err = cpu_uspace_copy(&new_key, tid, sizeof(pthread_t)))) 
		goto fail_ucopy_tid;

	if(((attr.stack_size != 0) && (attr.stack_size & PMM_PAGE_MASK))    ||
	   ((attr.stack_addr != NULL) && (attr.stack_size < PMM_PAGE_SIZE)) ||
	   ((attr.cpu_gid > 0) && (attr.cpu_gid >= MAX_CPU_NR)))
	{
		err = EINVAL;
		goto fail_attr_inval;
	}

	if(attr.stack_size == 0)
		attr.stack_size = CONFIG_PTHREAD_STACK_SIZE;

	if(task->threads_nr == task->threads_limit) 
	{
		err = EAGAIN;
		goto fail_threads_limit;
	}

	spinlock_lock(&task->th_lock);
  
	order = bitmap_ffs2(task->bitmap, task->next_order, sizeof(task->bitmap));
  
	if(order != -1)
	{
		bitmap_clear(task->bitmap, order);
		task->next_order = order + 1;
		task->threads_nr ++;
	}

	spinlock_unlock(&task->th_lock);
 
	if(order == -1)
	{ 
		err = EAGAIN;
		goto fail_thread_order;
	}

	if(attr.cpu_gid < 0)
	{
		struct dqdt_attr_s dqdt_attr;

		info.isPinned = false;
		dqdt_attr_init(&dqdt_attr, NULL);
    
		err = dqdt_thread_placement(current_cluster->levels_tbl[0], &dqdt_attr);

		if(err == 0)
		{
			attr.cpu_gid = dqdt_attr_getcpu(&dqdt_attr)->gid;
			attr.cpu_lid = dqdt_attr_getcpu(&dqdt_attr)->lid;
			attr.cid     = dqdt_attr_getcluster(&dqdt_attr)->id;
		}
	}
	else
	{
		attr.cpu_gid  = attr.cpu_gid % arch_onln_cpu_nr();
		attr.cpu_lid  = arch_cpu_lid(attr.cpu_gid, current_cluster->cpu_nr);
		attr.cid      = arch_cpu_cid(attr.cpu_gid, current_cluster->cpu_nr);
		info.isPinned = true; 
	}

	if(attr.cpu_gid < 0)
	{
		attr.cpu_gid = task->threads_nr % arch_onln_cpu_nr();
		attr.cpu_lid = arch_cpu_lid(attr.cpu_gid, current_cluster->cpu_nr);
		attr.cid     = arch_cpu_cid(attr.cpu_gid, current_cluster->cpu_nr);
	}

	info.isDone = false;
	info.err    = 0;
	info.key    = order;
	info.attr   = &attr;
	info.task   = task;

#if CONFIG_REMOTE_THREAD_CREATE
  
	event_set_argument(&info.event, &info);
	event_set_handler(&info.event, thread_create_event_handler);
	event_set_priority(&info.event, E_CREATE);

	struct cluster_s *cluster;
	struct cpu_s *cpu;

	cluster = clusters_tbl[attr.cid].cluster;
	cpu     = &cluster->cpu_tbl[attr.cpu_lid];

	event_send(&info.event, &cpu->re_listner);

	tm_bRemote = cpu_time_stamp();
	cpu_wbflush();

	/* TODO: replace this active wait be a passive one */
	while(info.isDone == false)
	{
		if(thread_sched_isActivated((volatile struct thread_s*)this))
			sched_yield(this);
	}

	err        = info.err;
	tm_aRemote = cpu_time_stamp();

#else
	tm_bRemote = cpu_time_stamp();
	err        = do_thread_create(&info);
	tm_aRemote = cpu_time_stamp();
#endif	/* CONFIG_REMOTE_THREAD_CREATE */

	if(err) goto fail_remote;

	err = cpu_uspace_copy(tid, &order, sizeof(pthread_t));
  
	if(err) goto fail_tid;
  
	sched_event = sched_event_make(info.new_thread, SCHED_OP_ADD_CREATED);
	sched_event_send(info.sched_listner, sched_event);
	return 0;
  
fail_tid:
	info.new_thread->state = S_DEAD;
	thread_destroy(info.new_thread);
  
fail_remote:
fail_thread_order:
fail_threads_limit:
fail_attr_inval:
fail_ucopy_tid:
fail_ucopy_attr:
fail_access:
fail_inval:

	current_thread->info.errno = err;
	return err;
}
  

error_t do_thread_create(thread_info_t *info)
{
	register uint_t online_clusters;
	struct thread_s *new_thread;
	struct task_s *task;
	pthread_attr_t *attr;
	error_t err;
	uint_t tm_start;
	uint_t tm_end;
	uint_t tm_astep1;
	uint_t tm_astep2;
	uint_t tm_astep3;
	uint_t tm_astep4;

	tm_start = cpu_time_stamp();

	task = info->task;
	attr = info->attr;

	if(attr->stack_addr == NULL)
	{
		attr->stack_size = (attr->stack_size < CONFIG_PTHREAD_STACK_MIN) ? 
			CONFIG_PTHREAD_STACK_MIN : attr->stack_size;

		attr->stack_addr = vmm_mmap(task, NULL, 
					    NULL, attr->stack_size, 
					    VM_REG_RD | VM_REG_WR, 
					    VM_REG_PRIVATE | VM_REG_ANON | VM_REG_STACK, 0);

		if(attr->stack_addr == VM_FAILED)
		{
			err = ENOMEM;
			goto fail_nomem;
		}

		attr->stack_size   -= SIG_DEFAULT_STACK_SIZE;
		attr->sigstack_addr = (void*)((uint_t)attr->stack_addr + attr->stack_size);
		attr->sigstack_size = SIG_DEFAULT_STACK_SIZE;
	}

	tm_astep1 = cpu_time_stamp();

	// Determinate New Thread Attributes (default values)
	attr->sched_policy = SCHED_RR;
    
	if((err = thread_create(task, attr, &new_thread)))
		goto fail_create;

        // Set migration intial state
	if(info->isPinned)
		thread_migration_disabled(new_thread);
	else
		thread_migration_enabled(new_thread);

	tm_astep2       = cpu_time_stamp();
	online_clusters = arch_onln_cluster_nr();

#if CONFIG_AUTO_NEXT_TOUCH
	if((online_clusters != 1) && (task->threads_count == 1))
		vmm_set_auto_migrate(&task->vmm, task->vmm.data_start);
#endif

	tm_astep3 = cpu_time_stamp();
  
	// Add the new thread to the set of created threads
	new_thread->info.order    = info->key;
	new_thread->info.attr.key = info->key;

	spinlock_lock(&task->th_lock);
	list_add_last(&task->th_root, &new_thread->rope);
	task->threads_count += 1;
	task->max_order = (info->key > task->max_order) ? info->key : task->max_order;
	task->th_tbl[new_thread->info.order] = new_thread;
	spinlock_unlock(&task->th_lock);

	tm_astep4 = cpu_time_stamp();
	
	// Register the new thread 
	err = sched_register(new_thread);
	assert(err == 0);
	new_thread->state   = S_CREATE;
	tm_create_compute(new_thread);
	info->sched_listner = sched_get_listner(new_thread,SCHED_OP_ADD_CREATED);
	info->new_thread    = new_thread;
	tm_end              = cpu_time_stamp();
  
	// m: mmap, 
	// c: create, 
	// a: autoNextTouch, 
	// l: add to task thread list, 
	// s: sched_add_create, 
	// e: total time
	printk(INFO, 
	       "INFO: %s: tid %x, pid %d, order %d, flags %x, cluster %d, "
	       "cpu %d [ m %d, c %d, a %d, l %d, s %d, e %d ][%u]\n",
	       __FUNCTION__,
	       new_thread,
	       task->pid,
	       info->key,
	       new_thread->flags,
	       attr->cid, 
	       attr->cpu_lid,
	       tm_astep1 - tm_start,
	       tm_astep2 - tm_astep1,
	       tm_astep3 - tm_astep2,
	       tm_astep4 - tm_astep3,
	       tm_end - tm_astep4,
	       tm_end - tm_start,
	       tm_end);
  
	return 0;

fail_create:
	spinlock_lock(&task->th_lock);
	task->threads_nr --;
	bitmap_set(task->bitmap, info->key);
	task->next_order = (info->key < task->next_order) ? info->key : task->next_order;
	spinlock_unlock(&task->th_lock);
	vmm_munmap(&task->vmm, (uint_t)attr->stack_addr, attr->stack_size + SIG_DEFAULT_STACK_SIZE);

fail_nomem:
	return err;
}
