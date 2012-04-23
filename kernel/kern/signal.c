/*
 * kern/signal.c - signal-management related operations
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
#include <signal.h>
#include <thread.h>
#include <task.h>
#include <cpu.h>

SIGNAL_HANDLER(default_sigaction)
{
	struct thread_s *this;
  
	this = current_thread;
  
	printk(INFO, "INFO: Recieved signal %d, pid %d, tid %x, cpu %d  [ KILLED ]\n",
	       sig,
	       this->task->pid,
	       this,
	       cpu_get_id());

	sys_thread_exit((void*)EINTR);
}

error_t signal_manager_init(struct task_s *task)
{
	memset(&task->sig_mgr, 0, sizeof(task->sig_mgr));
	task->sig_mgr.sigactions[SIGCHLD] = SIG_IGNORE;
	task->sig_mgr.sigactions[SIGURG]  = SIG_IGNORE;
	return 0;
}

error_t signal_init(struct thread_s *thread)
{
	thread->info.sig_state = 0;
	thread->info.sig_mask  = current_thread->info.sig_mask;
	return 0;
}

error_t signal_rise(struct task_s *task, uint_t sig)
{
	struct thread_s *thread;
  
	if (task->sig_mgr.handler == NULL)
	{
		spinlock_lock(&task->th_lock);
		thread = list_first(&task->th_root, struct thread_s, rope);
		spinlock_unlock(&task->th_lock);

		printk(INFO, "%s: pid %d, target thread is %x\n", 
		       __FUNCTION__, 
		       task->pid,
		       thread);
	}
	else
	{
		thread = task->sig_mgr.handler;
		printk(INFO, "%s: pid %d, handler thread is %x\n", 
		       __FUNCTION__, 
		       task->pid,
		       thread);
	}

	thread->info.sig_state |= (1 << sig);
  
	printk(INFO, "%s: pid %d, thread %x, signal %d, sig_state %x\n", 
	       __FUNCTION__,
	       task->pid,
	       thread, 
	       sig, 
	       thread->info.sig_state);

	return 0;
}

void signal_notify(struct thread_s *this)
{
	register uint_t sig_state;
	register uint_t sig;
	register struct sig_mgr_s *sig_mgr;

	sig_state = this->info.sig_state & this->info.sig_mask;
	sig = 0;
 
	while((sig_state != 0) && ((sig_state & 0x1) == 0) && (sig < SIG_NR))
	{
		sig ++; 
		sig_state >>= 1;
	}
  
	if(sig)
	{
		sig_mgr = &this->task->sig_mgr;

		if(sig_mgr->sigactions[sig] == SIG_DEFAULT)
		{
			default_sigaction(sig);
			return;
		}

		if(sig_mgr->sigactions[sig] == SIG_IGNORE)
			return;

		printk(INFO, "%s: going to delever signal %d\n", __FUNCTION__, sig);
		this->info.sig_state &= ~(1 << sig);
		cpu_signal_notify(this, sig_mgr->sigactions[sig], sig);
	}
}

