/*
 * sys_sem.c: interface to access signal service
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
#include <thread.h>
#include <task.h>
#include <signal.h>


int sys_signal(uint_t sig, sa_handler_t *handler)
{  
	register struct thread_s *this;
	int retval;

	this = current_thread;

	if((sig == 0) || (sig >= SIG_NR) || (sig == SIGKILL) || (sig == SIGSTOP))
	{
		this->info.errno = EINVAL;
		return SIG_ERROR;
	}

	retval = (int) this->task->sig_mgr.sigactions[sig];
	this->task->sig_mgr.sigactions[sig] = handler;

	printk(INFO,"INFO: %s: handler @%x has been registred for signal %d\n", 
	       __FUNCTION__, 
	       handler, 
	       sig);

	return retval;
}


int sys_sigreturn_setup(void *sigreturn_func)
{
	struct thread_s *this;

	this = current_thread;
	this->info.attr.sigreturn_func = sigreturn_func;
	cpu_context_set_sigreturn(&this->pws, sigreturn_func);
	return 0;
}


int sys_kill(int pid, uint_t sig)
{
	register error_t err;
	struct task_s *task;
    
	if((sig == 0)  || (sig >= SIG_NR))
	{
		err = EINVAL;
		goto SYS_KILL_ERR;
	}
 
	if(pid < 0)
	{
		err = ENOSYS;
		goto SYS_KILL_ERR;
	}

	if((pid == 0) || (pid == 1))
	{
		err = EPERM;
		goto SYS_KILL_ERR;
	}

	if((task = task_lookup(pid)) == NULL)
	{
		err = ESRCH;
		goto SYS_KILL_ERR;
	}

	if((err = signal_rise(task, sig)))
	{
		err = EPERM;
		goto SYS_KILL_ERR;
	}

	return 0;

SYS_KILL_ERR:
	current_thread->info.errno = err;
	return -1;
}
