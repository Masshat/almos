/*
 * kern/sys_thread_migrate.c - calls the scheduler to yield current CPU
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
#include <task.h>
#include <scheduler.h>

int sys_thread_migrate()
{
	struct thread_s *this;
	error_t err;

	this = current_thread;

	printk(INFO, "%s: pid %d, tid %d (%x), cpu %d [%u]\n", 
	       __FUNCTION__,
	       this->task->pid,
	       this->info.order,
	       this,
	       cpu_get_id(),
	       cpu_time_stamp());

	err = thread_migrate(this);

	printk(INFO, "%s: pid %d, tid %d (%x), cpu %d, err %d, done [%u]\n", 
	       __FUNCTION__,
	       this->task->pid,
	       this->info.order,
	       current_thread,
	       cpu_get_id(),
	       err,
	       cpu_time_stamp());

	this->info.errno = err;
	return (err) ? -1 : 0;
}
