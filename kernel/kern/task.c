/*
 * kern/task.c - task related management
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
#include <bits.h>
#include <kmem.h>
#include <page.h>
#include <vmm.h>
#include <kdmsg.h>
#include <vfs.h>
#include <cpu.h>
#include <thread.h>
#include <list.h>
#include <scheduler.h>
#include <spinlock.h>
#include <dqdt.h>
#include <cluster.h>
#include <pmm.h>
#include <boot-info.h>
#include <task.h>

#define TASK_PID_BUSY        0x2
#define TASK_PID_BUSY_BITS   0x2

static struct task_s task0;

/* TODO: use atomic counter instead of spinlock */
struct tasks_manager_s
{
	atomic_t tm_next_clstr;
	atomic_t tm_next_cpu;
	spinlock_t tm_lock;
	uint_t tm_last_pid;
	struct task_s *tm_tbl[CONFIG_TASK_MAX_NR];
};

static struct tasks_manager_s tasks_mgr = 
{
	.tm_next_clstr = ATOMIC_INITIALIZER,
	.tm_next_cpu = ATOMIC_INITIALIZER,
	.tm_tbl[0] = &task0
};

void task_manager_init(void)
{
	spinlock_init(&tasks_mgr.tm_lock, "Tasks Mgr");
	tasks_mgr.tm_last_pid = 1;
	memset(&tasks_mgr.tm_tbl[1], 
	       0, 
	       sizeof(struct task_s*) * (CONFIG_TASK_MAX_NR - 1));
}

void task_default_placement(struct dqdt_attr_s *attr)
{
	uint_t cid;
	uint_t cpu_lid;
  
	cid           = atomic_inc(&tasks_mgr.tm_next_clstr);
	cpu_lid       = atomic_inc(&tasks_mgr.tm_next_cpu);
	cid          %= arch_onln_cluster_nr();  
	cpu_lid      %= current_cluster->cpu_nr;
	attr->cluster = clusters_tbl[cid].cluster;
	attr->cpu     = &attr->cluster->cpu_tbl[cpu_lid];
}

struct task_s* task_lookup(uint_t pid)
{
	struct task_s *task;

	if(pid >= CONFIG_TASK_MAX_NR)
		return NULL;

	task = tasks_mgr.tm_tbl[pid];

	return ((uint_t)task == TASK_PID_BUSY) ? NULL : task;
}

error_t task_pid_alloc(uint_t *new_pid)
{
	register uint_t pid;
	register uint_t overlap;
	error_t err;

	err     = EAGAIN;
	overlap = false;

	spinlock_lock(&tasks_mgr.tm_lock);
	pid = tasks_mgr.tm_last_pid;
  
	while(tasks_mgr.tm_tbl[pid] != NULL)
	{    
		if((++pid) == CONFIG_TASK_MAX_NR)
		{
			pid = 0;
			if(overlap == true)
				break;
			overlap = true;
		}
	}
  
	if(tasks_mgr.tm_tbl[pid] == NULL)
	{
		tasks_mgr.tm_tbl[pid] = (void*) TASK_PID_BUSY;
		err                   = 0;
		*new_pid              = pid;
	}

	tasks_mgr.tm_last_pid = pid;
	spinlock_unlock(&tasks_mgr.tm_lock);
	return err;
}

inline void* task_vaddr2paddr(struct task_s* task, void *vma)
{
	uint_t paddr;
	error_t err;
	pmm_page_info_t info;

	err = pmm_get_page(&task->vmm.pmm, (vma_t)vma, &info);
  
	if((err) || ((info.attr & PMM_PRESENT) == 0))
	{
		printk(ERROR, "ERROR: %s: task (%x) vma (%x), thread (%x), CPU (%d), Err %d, attr %x, ppn %x\n", 
		       __FUNCTION__, 
		       task, 
		       vma, 
		       current_thread, 
		       cpu_get_id(), 
		       err, 
		       info.attr, 
		       info.ppn);

		return NULL;
	}

	paddr = (info.ppn << PMM_PAGE_SHIFT) | ((vma_t)vma & PMM_PAGE_MASK);

	return (void*) paddr;
}

static void task_ctor(struct kcm_s *kcm, void *ptr);

static error_t task_bootstrap_dup(struct task_s *dst, struct task_s *src)
{
	error_t err;

	task_ctor(NULL, dst);

	err = vmm_init(&dst->vmm);
  
	if(err) return err;

	err = pmm_init(&dst->vmm.pmm, current_cluster);
  
	if(err) return err;

	dst->vmm.limit_addr  = src->vmm.limit_addr;
	dst->vmm.devreg_addr = src->vmm.devreg_addr;
	dst->vmm.text_start  = src->vmm.text_start;

	err = pmm_dup(&dst->vmm.pmm, &src->vmm.pmm);

	if(err) return err;

	dst->vfs_root      = NULL;
	dst->vfs_cwd       = NULL;
	dst->fd_info       = NULL;
	dst->bin           = NULL;
	dst->threads_count = 0;
	dst->threads_nr    = 0;
	dst->threads_limit = CONFIG_PTHREAD_THREADS_MAX;
	dst->pid           = 0;
	atomic_init(&dst->childs_nr, 0);
	dst->childs_limit  = CONFIG_TASK_CHILDS_MAX_NR;
	return 0;
}

error_t task_replicate_do_first_stage(struct task_s *task, struct task_s *src)
{
	extern uint_t __ktext_start;
	extern uint_t __ktext_end;
	struct page_s *page;
	register uint_t vaddr;
	register uint_t start_ppn;
	register uint_t i;
	uint_t ktext_start;
	uint_t ktext_end;
	uint_t ktext_attr;
	uint_t kdata_attr;
	pmm_page_info_t info;
	kmem_req_t req;
	error_t err;

	ktext_start = (uint_t) &__ktext_start;
	ktext_end   = (uint_t) &__ktext_end;

	err = pmm_get_page(&task->vmm.pmm, ktext_start, &info);

	if(err) return err;

	start_ppn = info.ppn;
	assert(info.attr & PMM_HUGE);
  
	info.ppn     = 0;
	info.attr    = PMM_HUGE | PMM_CLEAR;
	info.cluster = NULL;

	err = pmm_set_page(&task->vmm.pmm, ktext_start, &info);

	if(err) return err;

	ktext_attr = PMM_PRESENT  | 
		PMM_READ     | 
		PMM_EXECUTE  | 
		PMM_ACCESSED | 
		PMM_CACHED   | 
		PMM_GLOBAL;
  
	kdata_attr = PMM_PRESENT  | 
		PMM_READ     | 
		PMM_WRITE    |
		PMM_ACCESSED |
		PMM_DIRTY    |
		PMM_CACHED   | 
		PMM_GLOBAL;

	req.type  = KMEM_PAGE;
	req.size  = 0;
	req.flags = AF_BOOT;

	for(i = 0; i < (PMM_HUGE_PAGE_SIZE / PMM_PAGE_SIZE); i++)
	{
		vaddr = ktext_start + (i*PMM_PAGE_SIZE);
   
		if(vaddr < ktext_end)
		{
			page = kmem_alloc(&req);

			if(page == NULL)
				return ENOMEM;

			memcpy(ppm_page2addr(page), (void*)vaddr, PMM_PAGE_SIZE);
			info.ppn  = ppm_page2ppn(page);
			info.attr = ktext_attr;
		}
		else
		{
			info.ppn  = start_ppn + i;
			info.attr = kdata_attr;
		}

		err = pmm_set_page(&task->vmm.pmm, vaddr, &info);
    
		if(err) return err;
	}

	return 0;
}

error_t task_replicate_do_next_stage(struct task_s *task, struct task_s *src)
{
	extern uint_t __ktext_start;
	extern uint_t __ktext_end;
	struct page_s *page;
	register uint_t vaddr;
	register uint_t start_ppn;
	register uint_t i;
	uint_t ktext_start;
	uint_t ktext_end;
	pmm_page_info_t info;
	kmem_req_t req;
	error_t err;

	ktext_start = (uint_t) &__ktext_start;
	ktext_end   = (uint_t) &__ktext_end;

	err = pmm_get_page(&task->vmm.pmm, ktext_start, &info);

	if(err) return err;

	start_ppn = info.ppn;
	assert((info.attr & PMM_HUGE) == 0);
  
	if(info.attr & PMM_HUGE)
		return EINVAL;

	info.attr    = PMM_HUGE | PMM_CLEAR;
	info.cluster = NULL;

	err = pmm_set_page(&task->vmm.pmm, ktext_start, &info);

	if(err) return err;

	req.type  = KMEM_PAGE;
	req.size  = 0;
	req.flags = AF_BOOT;

	for(i = 0; i < (PMM_HUGE_PAGE_SIZE / PMM_PAGE_SIZE); i++)
	{
		vaddr = ktext_start + (i*PMM_PAGE_SIZE);
		err   = pmm_get_page(&src->vmm.pmm, vaddr, &info);
    
		if(err) return err;

		if(vaddr < ktext_end)
		{  
			page = kmem_alloc(&req);

			if(page == NULL)
				return ENOMEM;

			page_copy(page, ppm_ppn2page(pmm_ppn2ppm(info.ppn), info.ppn));
			info.ppn  = ppm_page2ppn(page);
		}
    
		err = pmm_set_page(&task->vmm.pmm, vaddr, &info);
    
		if(err) return err;
	}

	return 0;
}


error_t task_bootstrap_replicate(struct boot_info_s *binfo)
{
	struct task_s *task;
	struct task_s *src;
	struct cluster_s *cluster;
	struct dqdt_cluster_s *logical;
	uint_t flags;
	error_t err;

	cluster = current_cluster;
	logical = cluster->levels_tbl[1];

#if !(CONFIG_KERNEL_REPLICATE)
	cluster->task = &task0;
	return ENOSYS;
#endif

	if(cluster->id == binfo->boot_cluster_id)
	{
		cluster->task = &task0;

		if(logical != NULL)
		{
			logical->flags |= DQDT_CLUSTER_READY;
			cpu_wbflush();
		}

		return 0;
	}

	if(logical == NULL)
	{
		logical = cluster->levels_tbl[0]->parent;
		flags   = 0;

		while((flags & DQDT_CLUSTER_READY) == 0)
			flags = cpu_load_word(&logical->flags);

		src     = logical->home->task;
		logical = NULL;
	}
	else
		src = &task0;

	task = cluster->task;
	err  = task_bootstrap_dup(task,src);

	if(err)
	{
		printk(INFO, "%s: cpu %d, cid %d: Failed to dup bootstrap task [ %u ]\n", 
		       __FUNCTION__, 
		       cpu_get_id(),
		       cluster->id,
		       cpu_time_stamp());

		goto REPLICATE_ERR;
	}

	if(logical != NULL)
		err = task_replicate_do_first_stage(task,src);
	else
		err = task_replicate_do_next_stage(task,src);

REPLICATE_ERR:
  
	if(err)
	{
		cluster->task = &task0;
		cpu_wbflush();
	}

	if(logical != NULL)
	{ 
		logical->flags |= DQDT_CLUSTER_READY;
		cpu_wbflush();
	}

	return err;
}


error_t task_bootstrap_init(struct boot_info_s *info)
{
	task_ctor(NULL, &task0);
	memset(&task0.vmm, 0, sizeof(task0.vmm));
	task0.vmm.text_start  = CONFIG_KERNEL_OFFSET;
	task0.vmm.limit_addr  = CONFIG_KERNEL_OFFSET;
	task0.vmm.devreg_addr = CONFIG_DEVREGION_OFFSET;
	pmm_bootstrap_init(&task0.vmm.pmm, info->boot_pgdir);
	/* Nota: vmm is not intialized */
	task0.vfs_root        = NULL;
	task0.vfs_cwd         = NULL;
	task0.fd_info         = NULL;
	task0.bin             = NULL;
	task0.threads_count   = 0;
	task0.threads_nr      = 0;
	task0.threads_limit   = CONFIG_PTHREAD_THREADS_MAX;
	task0.pid             = 0;
	atomic_init(&task0.childs_nr, 0);
	task0.childs_limit    = CONFIG_TASK_CHILDS_MAX_NR;
	return 0;
}

error_t task_bootstrap_finalize(struct boot_info_s *info)
{
	register error_t err;
  
	err = vmm_init(&task0.vmm);
  
	if(err) return err;

	err =  pmm_init(&task0.vmm.pmm, current_cluster);

	if(err)
	{
		printk(ERROR,"ERROR: %s: Failed, err %d\n", __FUNCTION__, err);
		while(1);
	}

	return 0;
}

error_t task_create(struct task_s **new_task, struct dqdt_attr_s *attr, uint_t mode)
{
	struct task_s *task;
	kmem_req_t req;
	uint_t pid = 0;
	uint_t err;
  
	assert(mode != CPU_SYS_MODE);

	if((err=task_pid_alloc(&pid)))
	{
		printk(WARNING, "WARNING: %s: System Is Out of PIDs\n", __FUNCTION__);
		return err;
	}

	req.type  = KMEM_TASK;
	req.size  = sizeof(struct task_s);
	req.flags = AF_KERNEL | AF_REMOTE;
	req.ptr   = attr->cluster;
  
	if((task = kmem_alloc(&req)) == NULL)
	{
		err = ENOMEM;
		goto fail_task_desc;
	}

	if((err = signal_manager_init(task)) != 0)
		goto fail_signal_mgr;

	req.type = KMEM_PAGE;
	req.size = (CONFIG_PTHREAD_THREADS_MAX * sizeof(struct thread_s*)) / PMM_PAGE_SIZE;
	req.size = (req.size == 0) ? 0 : bits_log2(req.size);
  
	task->th_tbl_pg = kmem_alloc(&req);

	if(task->th_tbl_pg == NULL)
	{
		err = ENOMEM;
		goto fail_th_tbl;
	}

	task->th_tbl = ppm_page2addr(task->th_tbl_pg);
  
	req.type      = KMEM_FDINFO;
	req.size      = sizeof(*task->fd_info);
	task->fd_info = kmem_alloc(&req);

	if(task->fd_info == NULL)
	{
		err = ENOMEM;
		goto fail_fd_info;
	}

	memset(&task->vmm, 0, sizeof(task->vmm));
	memset(&task->fd_info->tbl[0], 0, sizeof(task->fd_info->tbl));
	task->cluster         = attr->cluster;
	task->cpu             = attr->cpu;
	task->bin             = NULL;
	task->threads_count   = 0;
	task->threads_nr      = 0;
	task->threads_limit   = CONFIG_PTHREAD_THREADS_MAX;
	bitmap_set_range(task->bitmap, 0, CONFIG_PTHREAD_THREADS_MAX);
	task->pid             = pid;
	task->state           = TASK_CREATE;
	atomic_init(&task->childs_nr, 0);
	task->childs_limit    = CONFIG_TASK_CHILDS_MAX_NR;
	*new_task             = task;
	tasks_mgr.tm_tbl[pid] = task;
	return 0;

fail_fd_info:
	req.type = KMEM_PAGE;
	req.ptr  = task->th_tbl_pg;
	kmem_free(&req);

fail_signal_mgr:
fail_th_tbl:
	req.type = KMEM_TASK;
	req.ptr  = task;
	kmem_free(&req);

fail_task_desc:
	tasks_mgr.tm_tbl[pid] = NULL;
	*new_task = NULL;
	return err;
}

error_t task_dup(struct task_s *dst, struct task_s *src)
{
	uint_t i;

	rwlock_wrlock(&src->cwd_lock);

	vfs_node_up_atomic(src->vfs_root);
	vfs_node_up_atomic(src->vfs_cwd);

	dst->vfs_root = src->vfs_root;
	dst->vfs_cwd  = src->vfs_cwd;

	rwlock_unlock(&src->cwd_lock);
  
	spinlock_lock(&src->fd_info->lock);

	for(i=0; i < (CONFIG_TASK_FILE_MAX_NR); i++)
	{
		if(src->fd_info->tbl[i] != NULL)
		{
			atomic_add(&src->fd_info->tbl[i]->f_count, 1);
			dst->fd_info->tbl[i] = src->fd_info->tbl[i];
		}
	}

	spinlock_unlock(&src->fd_info->lock);

	assert(src->bin != NULL);
	(void)atomic_add(&src->bin->f_count, 1);
	dst->bin = src->bin;
	return 0;
}

void task_destroy(struct task_s *task)
{
	kmem_req_t req;
	register uint_t i;
	register uint_t pid;
	uint_t pgfault_nr;
	uint_t spurious_pgfault_nr;
	uint_t remote_pages_nr;
	uint_t u_err_nr;
	uint_t m_err_nr;
	uint_t count;

	assert(task->threads_nr == 0 && 
	       "Unexpected task destruction, One or more Threads still active\n");

	pid = task->pid;

	signal_manager_destroy(task);

	tasks_mgr.tm_tbl[task->pid] = NULL;
	cpu_wbflush();

	for(i=0; i < (CONFIG_TASK_FILE_MAX_NR); i++)
	{
		if(task->fd_info->tbl[i] != NULL)
			vfs_close(task->fd_info->tbl[i], &count);
	}

	if(task->bin != NULL)
		vfs_close(task->bin, &count);

	vfs_node_down_atomic(task->vfs_root);
	vfs_node_down_atomic(task->vfs_cwd);

	pgfault_nr          = task->vmm.pgfault_nr;
	spurious_pgfault_nr = task->vmm.spurious_pgfault_nr;
	remote_pages_nr     = task->vmm.remote_pages_nr;
	u_err_nr            = task->vmm.u_err_nr;
	m_err_nr            = task->vmm.m_err_nr;

	vmm_destroy(&task->vmm);
	pmm_release(&task->vmm.pmm);
	pmm_destroy(&task->vmm.pmm);

	req.type = KMEM_PAGE;
	req.ptr  = task->th_tbl_pg;
	kmem_free(&req);

	req.type = KMEM_FDINFO;
	req.ptr  = task->fd_info;
	kmem_free(&req);
  
	req.type = KMEM_TASK;
	req.ptr  = task;
	kmem_free(&req);

	printk(INFO, "INFO: %s: pid %d [ %d, %d, %d, %d, %d ]\n",
	       __FUNCTION__, 
	       pid, 
	       pgfault_nr,
	       spurious_pgfault_nr,
	       remote_pages_nr,
	       u_err_nr,
	       m_err_nr);
}


static void task_ctor(struct kcm_s *kcm, void *ptr)
{
	struct task_s *task = ptr;
  
	mcs_lock_init(&task->block, "Task");
	spinlock_init(&task->lock, "Task");
	rwlock_init(&task->cwd_lock);
	spinlock_init(&task->th_lock, "Task threads");
	spinlock_init(&task->tm_lock, "Task Time");
	list_root_init(&task->children);
	list_root_init(&task->th_root);
}

KMEM_OBJATTR_INIT(task_kmem_init)
{
	attr->type   = KMEM_TASK;
	attr->name   = "KCM Task";
	attr->size   = sizeof(struct task_s);
	attr->aligne = 0;
	attr->min    = CONFIG_TASK_KCM_MIN;
	attr->max    = CONFIG_TASK_KCM_MAX;
	attr->ctor   = task_ctor;
	attr->dtor   = NULL;
	return 0;
}

static void fdinfo_ctor(struct kcm_s *kcm, void *ptr)
{
	struct fd_info_s *info = ptr;
	spinlock_init(&info->lock, "Task FDs");
}

KMEM_OBJATTR_INIT(task_fdinfo_kmem_init)
{
	attr->type   = KMEM_FDINFO;
	attr->name   = "KCM FDINFO";
	attr->size   = sizeof(struct fd_info_s);
	attr->aligne = 0;
	attr->min    = CONFIG_FDINFO_KCM_MIN;
	attr->max    = CONFIG_FDINFO_KCM_MAX;
	attr->ctor   = fdinfo_ctor;
	attr->dtor   = NULL;
	return 0;
}
