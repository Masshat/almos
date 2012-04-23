/*
 * kern/sys_getcwd.c - get process current work directory
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

#include <libk.h>
#include <vfs.h>
#include <sys-vfs.h>
#include <task.h>
#include <kmem.h>
#include <ppm.h>
#include <thread.h>

/* TODO: user page need to be locked as long as its region */

int sys_getcwd (char *buff, size_t size)
{
	kmem_req_t req;
	struct page_s *page;
	register struct thread_s *this;
	register struct task_s *task;
	register struct vfs_node_s *current;
	register ssize_t count, len;
	register error_t err;
	char *tmp_path;
  
	req.type  = KMEM_PAGE;
	req.flags = AF_USER;
	req.size  = 0;
	this      = current_thread;
	task      = current_task;
	size      = MIN(size, PMM_PAGE_SIZE);

	if((size < VFS_MAX_NAME_LENGTH) || (!buff)) 
	{
		err = ERANGE;
		goto SYS_GETCWD_ERROR;
	}

	if(vmm_check_address("usr cwd buffer", task, buff, size))
	{
		err = EFAULT;
		goto SYS_GETCWD_ERROR;
	}

	if((page = kmem_alloc(&req)) == NULL)
	{
		err = ENOMEM;
		goto SYS_GETCWD_ERROR;
	}

	tmp_path = ppm_page2addr(page);
	count    = size - 1;
	len      = 0;
	tmp_path[count] = 0;

	rwlock_rdlock(&task->cwd_lock);
	current = task->vfs_cwd;
   
	while((current->n_parent != NULL) && (count > 0))
	{
		len    = strlen(current->n_name);
		count -= len;
		if(count < 1) break;

		memcpy(&tmp_path[count], current->n_name, len);
		tmp_path[--count] = '/';
		current = current->n_parent;
	}

	rwlock_unlock(&task->cwd_lock);

	if(current->n_parent != NULL)
	{
		err     = ERANGE;
		req.ptr = page;
		kmem_free(&req);
		goto SYS_GETCWD_ERROR;
	}
    
	if(len == 0)
		err = cpu_uspace_copy(buff, "/", 2);
	else
		err = cpu_uspace_copy(buff, &tmp_path[count], size - count);
   
	req.ptr = page;
	kmem_free(&req);
	if(err) goto SYS_GETCWD_ERROR;
   
	return (int)buff;

SYS_GETCWD_ERROR:
	this->info.errno = err;
	return 0;
}
