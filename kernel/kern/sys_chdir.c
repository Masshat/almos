/*
 * sys_chdir: change process current work directory
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
#include <device.h>
#include <driver.h>
#include <rwlock.h>
#include <vfs.h>
#include <sys-vfs.h>
#include <thread.h>
#include <task.h>

int sys_chdir (char *pathname)
{
	register error_t err = 0;
	register struct thread_s *this;
	register struct task_s *task;

	this = current_thread;
	task = current_task;

	if(!pathname)
	{
		this->info.errno = EINVAL;
		return -1;
	}
      
	rwlock_wrlock(&task->cwd_lock);

	if((err = vfs_chdir(pathname, task->vfs_cwd, &task->vfs_cwd)))
	{
		rwlock_unlock(&task->cwd_lock);
		this->info.errno = (err < 0) ? -err : err;
		return -1;
	}
   
	rwlock_unlock(&task->cwd_lock);
	return 0;
}
