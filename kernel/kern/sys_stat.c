/*
 * kern/sys_stat.c - stats a file or directory
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
#include <thread.h>
#include <vfs.h>
#include <sys-vfs.h>
#include <task.h>
#include <spinlock.h>
#include <cpu-trace.h>


int sys_stat(char *pathname, struct vfs_stat_s *buff, int fd)
{
	struct thread_s *this;
	register error_t err = 0;
	struct vfs_file_s *file;
	struct vfs_node_s *node;
	struct task_s *task;

	this = current_thread;
	task = current_task;

	if((buff == NULL) || ((pathname == NULL) && (fd == -1)))
	{
		this->info.errno = EINVAL;
		return -1;
	}

	if((uint_t)buff >= CONFIG_KERNEL_OFFSET)
	{
		this->info.errno = EPERM;
		return -1;
	}

	if(pathname == NULL)
	{
		if((fd >= CONFIG_TASK_FILE_MAX_NR) || (task_fd_lookup(task,fd) == NULL))
			return EBADFD;
 
		file = task_fd_lookup(task,fd);
		node = file->f_node;
		err = vfs_stat(task->vfs_cwd, NULL, &node);
	}
	else
	{
		node = NULL;
		rwlock_rdlock(&task->cwd_lock);
		err = vfs_stat(task->vfs_cwd, pathname, &node);
		rwlock_unlock(&task->cwd_lock);
	}
 
	if(err) goto SYS_STAT_ERR;
  
	err = cpu_uspace_copy(buff, &node->n_stat, sizeof(node->n_stat));
  
	if(err == 0)
		return 0;

SYS_STAT_ERR:
  
	if(pathname == NULL)
		vfs_node_down_atomic(node);

	this->info.errno = err;
	return -1;
}
