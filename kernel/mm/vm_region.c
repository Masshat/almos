/*
 * mm/vm_region.c - virtual memory region related operations
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
#include <list.h>
#include <errno.h>
#include <libk.h>
#include <bits.h>
#include <thread.h>
#include <task.h>
#include <ppm.h>
#include <pmm.h>
#include <mapper.h>
#include <spinlock.h>
#include <vfs.h>
#include <page.h>
#include <vmm.h>
#include <kmem.h>
#include <vm_region.h>

static void vm_region_ctor(struct kcm_s *kcm, void *ptr)
{
	struct vm_region_s *region;
  
	region = (struct vm_region_s*)ptr;
	//spinlock_init(&region->vm_lock, "VM Region");
	mcs_lock_init(&region->vm_lock, "VM Region");
}

KMEM_OBJATTR_INIT(vm_region_kmem_init)
{
	attr->type   = KMEM_VM_REGION;
	attr->name   = "KCM VM REGION";
	attr->size   = sizeof(struct vm_region_s);
	attr->aligne = 0;
	attr->min    = CONFIG_VM_REGION_MIN;
	attr->max    = CONFIG_VM_REGION_MAX;
	attr->ctor   = &vm_region_ctor;
	attr->dtor   = NULL;
	return 0;
}

error_t vm_region_init(struct vm_region_s *region,
		       uint_t vma_start, 
		       uint_t vma_end, 
		       uint_t prot,
		       uint_t offset,
		       uint_t flags)
{
	register uint_t pgprot;
	register uint_t regsize;
  
	atomic_init(&region->vm_refcount, 1);
	regsize           = 1 << CONFIG_VM_REGION_KEYWIDTH;
	region->vm_begin  = ARROUND_DOWN(vma_start, regsize);
	region->vm_start  = vma_start;
	region->vm_limit  = vma_end;
	region->vm_flags  = flags;
	region->vm_offset = offset;
	region->vm_op     = NULL;
	region->vm_mapper = NULL;
	region->vm_file   = NULL;

	if(flags & VM_REG_HEAP)
		region->vm_end = ARROUND_UP(CONFIG_TASK_HEAP_MAX_SIZE, regsize);
	else
		region->vm_end = ARROUND_UP(vma_end, regsize);

	pgprot = PMM_USER | PMM_CACHED | PMM_ACCESSED | PMM_DIRTY;

	if(prot & VM_REG_WR)
	{
		prot   |= VM_REG_RD;
		pgprot |= PMM_WRITE;
	}

	if(prot & VM_REG_RD)
		pgprot |= PMM_READ;
  
	if(prot & VM_REG_EX)
		pgprot |= PMM_EXECUTE;

	if(prot != VM_REG_NON)
		pgprot |= PMM_PRESENT;
  
	if(flags & VM_REG_HUGETLB)
		pgprot |= PMM_HUGE;

	region->vm_prot   = prot;
	region->vm_pgprot = pgprot;

	return 0;
}

error_t vm_region_destroy(struct vm_region_s *region)
{
  
	return 0;
}

struct vm_region_s* vm_region_find(struct vmm_s *vmm, uint_t vaddr)
{
	struct vm_region_s *region;
	struct list_entry *iter;

	list_foreach_forward(&vmm->regions_root, iter)
	{
		region = list_element(iter, struct vm_region_s, vm_list);

		if(vaddr < region->vm_end)
		{
			vmm->last_region = region;
			return region;
		}
	}

	return NULL;
}

error_t vm_region_add(struct vmm_s *vmm, struct vm_region_s *region)
{
	register struct vm_region_s *current_reg;
	register struct vm_region_s *next_reg;
	register struct list_entry *iter;
	register struct list_entry *next;
	register uint_t effective_size;
  
	effective_size = region->vm_end; // + ((PMM_PAGE_SIZE) << 1);

	list_foreach_forward(&vmm->regions_root, iter)
	{
		next = list_next(&vmm->regions_root, iter);
		if(next == NULL) break;

		current_reg = list_element(iter, struct vm_region_s, vm_list);
		next_reg    = list_element(next, struct vm_region_s, vm_list);
    
		if((current_reg->vm_end + effective_size) <= next_reg->vm_begin)
		{
			list_add_next(iter, &region->vm_list);

			region->vm_start  = current_reg->vm_end;
			region->vm_limit += region->vm_start;
			region->vm_begin  = current_reg->vm_end;
			region->vm_end   += region->vm_begin;

			return 0;
		}
	}

	current_reg = list_last(&vmm->regions_root, struct vm_region_s, vm_list);
  
	if((current_reg->vm_end + effective_size) > vmm->limit_addr)
		return ENOMEM;

	list_add_last(&vmm->regions_root, &region->vm_list);
	region->vm_start  = current_reg->vm_end;
	region->vm_limit += region->vm_start;

	return 0;
}

error_t vm_region_attach(struct vmm_s *vmm, struct vm_region_s *region)
{
	struct vm_region_s *reg;
	error_t err;

	region->vmm = vmm;
  
	if(region->vm_flags & VM_REG_FIXED)
	{
		reg = vm_region_find(vmm, region->vm_begin);

		if(reg == NULL)
			list_add_last(&vmm->regions_root, &region->vm_list);
		else
		{
			if(reg->vm_begin < region->vm_end)
				return ERANGE;
     
			list_add_pred(&reg->vm_list, &region->vm_list);
		}
    
		return 0;
	}

	err = vm_region_add(vmm,region);

	return err;
}

/* TODO: compute LAZY flag */
error_t vm_region_unmap(struct vm_region_s *region)
{
	register uint_t vaddr;
	register uint_t count;
	register bool_t isLazy;
	struct pmm_s *pmm;
	struct page_s *page;
	struct ppm_s *ppm;
	pmm_page_info_t info;
	uint_t refcount;
	uint_t attr;
	error_t err;
  
	count  = (region->vm_limit - region->vm_start) >> PMM_PAGE_SHIFT;
	vaddr  = region->vm_start;
	pmm    = &region->vmm->pmm;
	isLazy = (region->vm_flags & VM_REG_LAZY) ? true : false;

	while(count)
	{
		if((err = pmm_get_page(pmm, vaddr, &info)))
			goto NEXT;

		if(info.attr & PMM_PRESENT)
		{
			ppm          = pmm_ppn2ppm(info.ppn);
			page         = ppm_ppn2page(ppm, info.ppn);
			attr         = info.attr;
			info.attr    = 0;
			info.ppn     = 0;
			info.cluster = NULL;
			refcount     = 0;

			page_lock(page);
      
			if(isLazy == false)
				pmm_set_page(pmm, vaddr, &info);

			if(page->mapper == NULL) 
				refcount = page_refcount_down(page);

			page_unlock(page);
      
			if(refcount == 1)
			{
				page_refcount_up(page);	/* adjust refcount */
				ppm_free_pages(page);
			}
		}

	NEXT:
		vaddr += PMM_PAGE_SIZE;
		count --;
	}

	if((region->vm_mapper != NULL) && (region->vm_flags & VM_REG_ANON))
	{
		count = atomic_add(&region->vm_mapper->m_refcount, -1);
      
		if(count == 1)
			mapper_destroy(region->vm_mapper, false);
	}
  
	return 0;
}


/* TODO: Informe the pmm layer about this event */
/* vmm lock must be taken (wrlock) before calling this function */
error_t vm_region_detach(struct vmm_s *vmm, struct vm_region_s *region)
{
	struct vfs_file_s *file;
	kmem_req_t req;
	uint_t regsize;
	uint_t key_start;
	uint_t count;

	list_unlink(&region->vm_list);

	if(!(region->vm_flags & VM_REG_LAZY))
	{
		regsize   = region->vm_end - region->vm_begin;
		key_start = region->vm_begin >> CONFIG_VM_REGION_KEYWIDTH;
		keysdb_unbind(&vmm->regions_db, key_start, regsize >> CONFIG_VM_REGION_KEYWIDTH);
	}
	rwlock_unlock(&vmm->rwlock);

	while((count = atomic_get(&region->vm_refcount)) > 1)
		sched_yield(current_thread);

	file = region->vm_file;
  
	if(file != NULL)
	{
		file->f_op->munmap(file, region);
		vfs_close(file, &count);
	}

	vm_region_unmap(region);

	req.type = KMEM_VM_REGION;
	req.ptr  = region;
	kmem_free(&req);
  
	rwlock_wrlock(&vmm->rwlock);
	return 0;
}

error_t vm_region_resize(struct vmm_s *vmm, struct vm_region_s *region, uint_t start, uint_t end)
{
	return 0;
}

/* TODO: use a marker of last active page in the region, purpose is to reduce time of duplication */
error_t vm_region_dup(struct vm_region_s *dst, struct vm_region_s *src)
{
	register struct pmm_s *src_pmm;
	register struct pmm_s *dst_pmm;
	register uint_t vaddr;
	register uint_t count;
	struct page_s *page;
	struct ppm_s *ppm;
	struct task_s *task;
	pmm_page_info_t info;
	error_t err;

	atomic_init(&dst->vm_refcount, 1);
	dst->vm_begin   = src->vm_begin;
	dst->vm_end     = src->vm_end;
	dst->vm_start   = src->vm_start;
	dst->vm_limit   = src->vm_limit;
	dst->vm_prot    = src->vm_prot;
	dst->vm_pgprot  = src->vm_pgprot;
	dst->vm_flags   = src->vm_flags;
	dst->vm_offset  = src->vm_offset;
	dst->vm_op      = src->vm_op;
	dst->vm_mapper  = src->vm_mapper;
	dst->vm_file    = src->vm_file;
	dst->vm_data    = src->vm_data;

	if(src->vm_file != NULL)
		atomic_add(&src->vm_file->f_count, 1);
  
	if(src->vm_flags & VM_REG_SHARED)
	{
		atomic_add(&src->vm_mapper->m_refcount, 1);
		return 0;
	}

	vaddr   = src->vm_start;
	src_pmm = &src->vmm->pmm;
	dst_pmm = &dst->vmm->pmm;
	count   = (src->vm_limit - src->vm_start) >> PMM_PAGE_SHIFT;
	task    = vmm_get_task(dst->vmm);

	while(count)
	{
		if((err = pmm_get_page(src_pmm, vaddr, &info)))
			goto REG_DUP_ERR;

		if(info.attr & PMM_PRESENT)	/* TODO: review this condition on swap */
		{
			ppm  = pmm_ppn2ppm(info.ppn);
			page = ppm_ppn2page(ppm, info.ppn);

			page_lock(page);
      
			info.attr |= PMM_COW;
			info.attr &= ~(PMM_WRITE);
			info.cluster = task->cluster;

#if CONFIG_FORK_LOCAL_ALLOC
			info.cluster = NULL;
#endif

			if((err = pmm_set_page(dst_pmm, vaddr, &info)))
				goto REG_DUP_ERR1;

			if(page->mapper == NULL)
			{
				if((err = pmm_set_page(src_pmm, vaddr, &info)))
					goto REG_DUP_ERR1;

				page_refcount_up(page);
			}

			page_unlock(page);
		}

		vaddr += PMM_PAGE_SIZE;
		count --;
	}

	return 0;

REG_DUP_ERR1:
	page_unlock(page);

REG_DUP_ERR:
	return err;
}

/* FIXME: review this function */
error_t vm_region_split(struct vmm_s *vmm, struct vm_region_s *region, uint_t start_addr, uint_t length)
{
	kmem_req_t req;
	struct vm_region_s *new_region;
	error_t err;

	return ENOSYS;

	req.type  = KMEM_VM_REGION;
	req.size  = sizeof(*region);
	req.flags = AF_KERNEL;

	if((new_region = kmem_alloc(&req)) == NULL)
		return ENOMEM;
  
	new_region->vmm = vmm;
  
	/* FIXME: review this call as the dup has changed */
	if((err = vm_region_dup(new_region, region)))
		return err;

	new_region->vm_limit = start_addr;
  
	return vm_region_resize(vmm, region, start_addr + length, region->vm_limit);
}

error_t vm_region_update(struct vm_region_s *region, uint_t vaddr, uint_t flags)
{
	error_t err;
	bool_t isTraced;
	struct thread_s *this;

	if(pmm_except_isRights(flags))
		goto fail_rights;

	if((pmm_except_isPresent(flags)) && !(region->vm_prot & VM_REG_RD))
		goto fail_read;

	if((pmm_except_isWrite(flags)) && !(region->vm_prot & VM_REG_WR))
		goto fail_write;

	if((pmm_except_isExecute(flags)) && !(region->vm_prot & VM_REG_EX))
		goto fail_execute;

	if(region->vm_flags & VM_REG_INIT)
		goto fail_region;

	this     = current_thread;
	isTraced = this->info.isTraced;
  
#if CONFIG_SHOW_PAGEFAULT
	isTraced = true;
#endif	/* CONFIG_SHOW_PAGEFAULT */

	if(isTraced)
	{
		printk(DEBUG, 
		       "DEBUG: %s: cpu %d, pid %d, vaddr %x, flags %x, region [%x,%x]\n",
		       __FUNCTION__, 
		       cpu_get_id(),
		       this->task->pid,
		       vaddr,
		       flags, 
		       region->vm_start, 
		       region->vm_limit);
	}
  
	err = region->vm_op->page_fault(region, vaddr, flags);
	goto update_end;

fail_region:
	err = -1005;
	goto update_end;

fail_execute:
	err = -1004;
	goto update_end;

fail_write:
	err = -1003;
	goto update_end;

fail_read:
	err = -1002;
	goto update_end;

fail_rights:
	err = -1001;
  
update_end:
	if(err) 
		printk(INFO, "INFO: %s: cpu %d, vaddr 0x%x flags 0x%x, prot 0x%x, ended with err %d\n", 
		       __FUNCTION__, 
		       cpu_get_id(),
		       vaddr,
		       flags,
		       region->vm_prot,
		       err);

	return err;
}

