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

SIGNAL_HANDLER(kill_sigaction)
{
	struct thread_s *this;
  
	this = current_thread;
	this->state = S_KERNEL;

	printk(INFO, "INFO: Recieved signal %d, pid %d, tid %x, core %d  [ KILLED ]\n",
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

static error_t signal_rise_all(struct task_s *task, uint_t sig)
{
	struct list_entry *iter;
	struct thread_s *thread;
	uint_t count;

	count = 0;

	spinlock_lock(&task->th_lock);

	list_foreach(&task->th_root, iter)
	{
		thread = list_element(iter, struct thread_s, rope);

		spinlock_unlock(&thread->lock);
		thread->info.sig_state |= (1 << sig);
		spinlock_unlock(&thread->lock);

		count ++;
	}

	spinlock_unlock(&task->th_lock);

	printk(INFO, "INFO: pid %d, %d threads has been signaled\n", task->pid, count);

	return 0;
}

static error_t signal_rise_one(struct task_s *task, uint_t sig)
{
	struct thread_s *thread;

	spinlock_lock(&task->th_lock);

	if(task->sig_mgr.handler == NULL)
		thread = list_first(&task->th_root, struct thread_s, rope);
	else
		thread = task->sig_mgr.handler;

	spinlock_unlock(&thread->lock);
	thread->info.sig_state |= (1 << sig);
	spinlock_unlock(&thread->lock);

	spinlock_unlock(&task->th_lock);

	printk(INFO, "INFO: core %d: pid %d, tid %d, signal %d, sig_state %x\n",
	       thread_current_cpu(thread)->gid,
	       task->pid,
	       thread->info.order,
	       sig,
	       thread->info.sig_state);

	return 0;
}

error_t signal_rise(struct task_s *task, uint_t sig)
{
	error_t err;

	if((sig == SIGTERM) || (sig == SIGKILL))
		err = signal_rise_all(task, sig);
	else
		err = signal_rise_one(task, sig);

	return err;
}

void signal_notify(struct thread_s *this)
{
	register uint_t sig_state;
	register uint_t sig;
	register struct sig_mgr_s *sig_mgr;
	uint_t irq_state;

	sig_state = this->info.sig_state & this->info.sig_mask;
	sig       = 0;
 
	while((sig_state != 0) && ((sig_state & 0x1) == 0) && (sig < SIG_NR))
	{
		sig ++; 
		sig_state >>= 1;
	}
  
	if(sig)
	{
		cpu_disable_all_irq(&irq_state);

		if(thread_isSignaled(this))
		{
			cpu_restore_irq(irq_state);
			return;
		}

		thread_set_signaled(this);
		cpu_restore_irq(irq_state);

		spinlock_lock(&this->lock);
		this->info.sig_state &= ~(1 << sig);
		spinlock_unlock(&this->lock);

		sig_mgr = &this->task->sig_mgr;

		if(sig_mgr->sigactions[sig] == SIG_IGNORE)
			return;

		if(sig_mgr->sigactions[sig] == SIG_DEFAULT)
			kill_sigaction(sig);

		cpu_signal_notify(this, sig_mgr->sigactions[sig], sig);
	}
}
