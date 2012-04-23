/*
 * sys_read.c: read entries from an opened directory
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

#include <vfs.h>
#include <sys-vfs.h>
#include <task.h>
#include <thread.h>

/* FIXEME: reading dirent from user without any protection */

int sys_readdir (uint_t fd, struct vfs_dirent_s *dirent)
{
	error_t err;
	struct task_s *task;
	struct thread_s *this;
	struct vfs_file_s *file;
  
	task = current_task;
	this = current_thread;

	if((dirent == NULL)                || 
	   (fd >= CONFIG_TASK_FILE_MAX_NR) || 
	   (task_fd_lookup(task,fd) == NULL))
	{
		this->info.errno = EBADFD;
		return -1;
	}

	file = task_fd_lookup(task,fd);
   
	if((err = vfs_readdir(file, dirent)))
	{
		this->info.errno = (err < 0) ? -err : err; 
		return -1;
	}
   
	return 0;
}
