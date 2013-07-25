/*
 * kern/sys_exec.c - executes a new program (main work is done by do_exec)
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
#include <cpu.h>
#include <vfs.h>
#include <cluster.h>
#include <task.h>
#include <thread.h>
#include <vmm.h>
#include <pmm.h>
#include <ppm.h>
#include <kdmsg.h>

int sys_exec(char *filename, char **argv, char **envp)
{
	kmem_req_t req;
	struct thread_s *main_thread;
	struct task_s *task;
	struct thread_s *this;
	struct page_s *page;
	struct vfs_file_s *bin;
	char *path;
	uint_t len;
	error_t err;
	uint_t isFatal;

	req.type  = KMEM_PAGE;
	req.size  = 0;
	req.flags = AF_USER;
	this      = current_thread;
	task      = this->task;
	err       = 0;
	len       = 0;
	isFatal   = true;

	if((filename == NULL) || (argv == NULL) || (envp == NULL))
	{
		err = EINVAL;
		goto fail_args;
	}

	if(task->threads_nr != 1)
	{
		printk(INFO, "INFO: %s: current task (pid %d) has more than 1 thread !\n", 
		       __FUNCTION__, 
		       task->pid);

		err = EACCES;
		goto fail_access;
	}

	err = cpu_uspace_strlen(filename, &len);

	if(err) goto fail_filename;
  
	len ++;

	if(len >= PMM_PAGE_SIZE)
	{
		err = E2BIG;
		goto fail_len;
	}

	page = kmem_alloc(&req);
  
	if(page == NULL)
	{
		err = ENOMEM;
		goto fail_mem;
	}

	path = ppm_page2addr(page);
	err  = cpu_uspace_copy(path, filename, len);

	if(err) goto fail_path;

	assert(task->bin != NULL);
	bin = task->bin;
	cpu_wbflush();
	task->state = TASK_CREATE;
	cpu_wbflush();

	signal_manager_destroy(task);
	signal_manager_init(task);
  
	err = do_exec(task, path, argv, envp, &isFatal, &main_thread);

	if(err == 0)
	{
		assert(main_thread != NULL && (main_thread->signature == THREAD_ID));
    
		err = sched_register(main_thread);
		assert(err == 0);

		task->state = TASK_READY;

#if CONFIG_ENABEL_TASK_TRACE
		main_thread->info.isTraced = true;
#endif
		sched_add_created(main_thread);

		thread_set_no_vmregion(this);
		sched_exit(this);
	}
  
	printk(WARNING, "%s: failed to do_exec new task [pid %d, tid %x, cpu %d, err %d]\n",
	       __FUNCTION__, 
	       task->pid, 
	       this, 
	       cpu_get_id(), 
	       err);

fail_path:
	req.ptr = page;
	kmem_free(&req);
    
	if(isFatal == true)
		sched_exit(this);

fail_mem:
fail_len:
fail_filename:
fail_access:
fail_args:
	this->info.errno = err;
	task->state = TASK_READY;
	return -1;
}

