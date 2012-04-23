/*
 * kern/sys_ps.c - show kernel active processes and threads
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
#include <task.h>
#include <thread.h>
#include <vmm.h>
#include <errno.h>

extern error_t ps_func(void *param);

static error_t sys_ps_check_thread(uint_t pid, uint_t tid, struct thread_s **th_ptr)
{
	struct task_s *task;
	struct thread_s *thread;

	if(pid == 0)
		return EINVAL;

	task = task_lookup(pid);

	if((task == NULL) || (tid > task->max_order))
		return EINVAL;
  
	thread = task->th_tbl[tid];
      
	if((thread == NULL)                 ||
	   (thread->signature != THREAD_ID) || 
	   (thread->info.attr.key != tid))
	{
		return ESRCH;
	}

	*th_ptr = thread;
	return 0;
}

int sys_ps(uint_t cmd, uint_t pid, uint_t tid)
{
	error_t err = 0;
	struct thread_s *thread;
 
	switch(cmd)
	{
	case TASK_PS_TRACE_OFF:
		err = sys_ps_check_thread(pid, tid, &thread);
		if(err) goto fail_trace_off;
		thread->info.isTraced = false;
		cpu_wbflush();
		printk(INFO,"INFO: pid %d, tid %x, tracing is turned [OFF]\n", pid, tid);
		break;

	case TASK_PS_TRACE_ON:
		err = sys_ps_check_thread(pid, tid, &thread);
		if(err) goto fail_trace_on;
		thread->info.isTraced = true;
		cpu_wbflush();
		printk(INFO,"INFO: pid %d, tid %x, tracing is turned [ON]\n", pid, tid);
		break;

	case TASK_PS_SHOW:
		err = ps_func(NULL); 
		break;
	}

fail_trace_on:
fail_trace_off:
	current_thread->info.errno = err;
	return err;
}
