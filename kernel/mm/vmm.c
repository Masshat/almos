/*
 * mm/vmm.c - virtual memory management related operations
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
#include <libk.h>
#include <bits.h>
#include <task.h>
#include <thread.h>
#include <cluster.h>
#include <scheduler.h>
#include <semaphore.h>
#include <vfs.h>
#include <mapper.h>
#include <page.h>
#include <kmem.h>
#include <vmm.h>

error_t vmm_init(struct vmm_s *vmm)
{  
	rwlock_init(&vmm->rwlock);

	list_root_init(&vmm->regions_root);

	vmm->pgfault_nr          = 0;
	vmm->spurious_pgfault_nr = 0;
	vmm->remote_pages_nr     = 0;
	vmm->pages_nr            = 0;
	vmm->locked_nr           = 0;
	vmm->u_err_nr            = 0;
	vmm->m_err_nr            = 0;

	return keysdb_init(&vmm->regions_db, CONFIG_VM_REGION_KEYWIDTH);
}

error_t vmm_destroy(struct vmm_s *vmm)
{
	register struct vm_region_s *region;

	rwlock_wrlock(&vmm->rwlock);

	while(!(list_empty(&vmm->regions_root)))
	{
		region = list_first(&vmm->regions_root, struct vm_region_s, vm_list);
		region->vm_flags |= VM_REG_LAZY;
		vm_region_detach(vmm, region);
	}
	vmm->last_region = NULL;
	keysdb_destroy(&vmm->regions_db);
	rwlock_unlock(&vmm->rwlock);
	rwlock_destroy(&vmm->rwlock); 
	return 0;
}

error_t vmm_dup(struct vmm_s *dst, struct vmm_s *src)
{
	kmem_req_t req;
	struct task_s *dst_task;
	struct task_s *ktask;
	struct vm_region_s *src_reg;
	struct vm_region_s *dst_reg;
	struct list_entry *iter;
	error_t err;
  
	dst_task  = vmm_get_task(dst);
	ktask     = dst_task->cluster->task;

	req.type  = KMEM_VM_REGION;
	req.size  = sizeof(*dst_reg);
	req.flags = AF_KERNEL | AF_REMOTE;
	req.ptr   = dst_task->cluster;

#if CONFIG_FORK_LOCAL_ALLOC
	req.ptr   = dst_task->current_clstr;
#endif

	memcpy(dst, src, sizeof(*dst));
  
	err = vmm_init(dst);
  
	if(err) return err;

#if CONFIG_FORK_LOCAL_ALLOC
	err = pmm_init(&dst->pmm, dst_task->current_clstr);
#else
	err = pmm_init(&dst->pmm, dst_task->cluster);
#endif

	if(err) return err;

	err = pmm_dup(&dst->pmm, &ktask->vmm.pmm);
  
	if(err) return err;

	dst_reg = NULL;
	rwlock_wrlock(&src->rwlock);
  
	list_foreach_forward(&src->regions_root, iter)
	{
		src_reg = list_element(iter, struct vm_region_s, vm_list);
    
		dst_reg = kmem_alloc(&req);

		if(dst_reg == NULL) goto VMM_DUP_ERR;

		dst_reg->vmm = dst;

		err = vm_region_dup(dst_reg, src_reg);
    
		if(err) goto VMM_DUP_ERR1;

		list_add_last(&dst->regions_root, &dst_reg->vm_list);

		//keys_nr = (dst_reg->vm_end - dst_reg->vm_begin) >> CONFIG_VM_REGION_KEYWIDTH;
		//key_start = dst_reg->vm_begin >> CONFIG_VM_REGION_KEYWIDTH;
		//err = keysdb_bind(&dst->regions_db, key_start, keys_nr, dst_reg);
		//if(err) goto VMM_DUP_ERR1;
	}

	rwlock_unlock(&src->rwlock);
	dst->last_region = dst_reg;
	return 0;

VMM_DUP_ERR1:
	req.ptr = dst_reg;
	kmem_free(&req);

VMM_DUP_ERR:
	rwlock_unlock(&src->rwlock);
	vmm_destroy(dst);
	return err;
}

/* TODO: remove the usage of this function */
inline error_t vmm_check_address(char *objname, struct task_s *task, void *addr, uint_t size)
{
	struct vmm_s *vmm = &task->vmm;
  
	if(((uint_t)addr < vmm->text_start) || (((uint_t)addr + size) >= vmm->limit_addr))
	{

		printk(INFO,"%s: pid %d, objname %s: Error addr (%x) text (%x), size (%d), limit (%x), cycles %u\n", 
		       __FUNCTION__, 
		       task->pid, objname, 
		       addr, 
		       vmm->text_start, 
		       size, 
		       vmm->limit_addr, 
		       cpu_time_stamp());

		return EINVAL;		/* must be EINVAL */
	}
	return 0;
}

error_t vmm_do_munmap(struct vmm_s *vmm, struct vm_region_s *region, uint_t addr, uint_t length)
{
	register uint_t addr_limit;

	addr_limit = addr + length;

	if((region->vm_start == addr) && (region->vm_limit == addr_limit))
		return vm_region_detach(vmm, region);

	if((addr >= region->vm_start) && (addr_limit < region->vm_limit))
		return vm_region_split(vmm, region, addr, length);

	if(addr_limit > region->vm_limit)
		return vm_region_resize(vmm, region, addr, region->vm_limit);

	return vm_region_resize(vmm, region, region->vm_start, addr_limit);
}

error_t vmm_munmap(struct vmm_s *vmm, uint_t vaddr, uint_t length)
{
	register error_t err;
	struct vm_region_s *region;

	err = 0;
	region = keysdb_lookup(&vmm->regions_db, vaddr >> CONFIG_VM_REGION_KEYWIDTH);

	if((region == NULL)           || 
	   (vaddr > region->vm_limit) || 
	   (vaddr < region->vm_start))
	{
		return EINVAL;
	}
  
	rwlock_wrlock(&vmm->rwlock);
  
	err = vmm_do_munmap(vmm, region, vaddr, length);

	rwlock_unlock(&vmm->rwlock);
	return err;
}

static const struct mapper_op_s vmm_anon_mapper_op = 
{
	.writepage      = mapper_default_write_page,
	.readpage       = mapper_default_read_page,
	.sync_page      = mapper_default_sync_page,
	.set_page_dirty = mapper_default_set_page_dirty,
	.releasepage    = mapper_default_release_page,
};

error_t vmm_do_shared_anon_mapping(struct vm_region_s *region)
{
	struct mapper_s *mapper;
	kmem_req_t req;
	error_t err;

	req.type  = KMEM_MAPPER;
	req.flags = AF_KERNEL;
	req.size  = sizeof(*mapper);
	mapper    = kmem_alloc(&req);
  
	if(mapper == NULL) 
		return ENOMEM;

	err = mapper_init(mapper, &vmm_anon_mapper_op, NULL, NULL);

	if(err)
		goto fail_mapper_init;

	region->vm_mapper = mapper;
	region->vm_file   = NULL;
	return 0;

fail_mapper_init:
	req.ptr = mapper;
	kmem_free(&req);
	return err;
}

void *vmm_mmap(struct task_s *task, 
	       struct vfs_file_s *file,
	       void *addr, 
	       uint_t length, 
	       uint_t proto, 
	       uint_t flags, 
	       uint_t offset)
{
	kmem_req_t req;
	struct vm_region_s *region;
	struct thread_s *this;
	struct vmm_s *vmm;
	uint_t asked_addr;
	uint_t keys_nr;
	uint_t key_start;
	error_t err;

	req.type  = KMEM_VM_REGION;
	req.size  = sizeof(*region);
	req.flags = AF_KERNEL;
  
	this       = current_thread;
	asked_addr = (uint_t)addr;
	vmm        = &task->vmm;

	vmm_dmsg(1, "%s: Cycle: %x, cpu %d, pid %d, tid %x, addr %x, len %d, proto %x, flags %x, file %x, offset %d\n",
		 __FUNCTION__, 
		 cpu_time_stamp(),
		 current_cpu->gid,
		 task->pid,
		 this,
		 addr, 
		 length,
		 proto, 
		 flags, 
		 file, 
		 offset);
   
	if((region = kmem_alloc(&req)) == NULL)
	{
		err = ENOMEM;
		goto MMAP_ERR;
	}

	flags |= VM_REG_INIT;

	if((err = vm_region_init(region, asked_addr, asked_addr + length, proto, offset, flags)))
		goto MMAP_ERR1;

	rwlock_wrlock(&vmm->rwlock);

	err = vm_region_attach(vmm, region);

	rwlock_unlock(&vmm->rwlock);

	if(err) goto MMAP_ERR1;

	region->vm_op = (struct vm_region_op_s*)&vm_region_default_op;

	if((flags & VM_REG_SHARED) && (flags & VM_REG_ANON))
	{
		if((err = vmm_do_shared_anon_mapping(region)))
			goto MMAP_ERR2;
	}
	else
	{
		if(file != NULL)
		{
			if((err = file->f_op->mmap(file,region)))
				goto MMAP_ERR2;
		}
		else
			if(flags & VM_REG_SHARED)
				goto MMAP_ERR2;
	}
  
	vmm_dmsg(1, "%s: pid %d, cpu %d, region [%x, %x] has been mapped\n", 
		 __FUNCTION__,
		 task->pid,
		 current_cpu->gid,
		 region->vm_start, 
		 region->vm_limit);
  
	region->vm_flags &= ~(VM_REG_INIT);
	cpu_wbflush();

	if(flags & VM_REG_HEAP)
		vmm->heap_region = region;

	keys_nr   = (region->vm_end - region->vm_begin) >> CONFIG_VM_REGION_KEYWIDTH;
	key_start = region->vm_begin >> CONFIG_VM_REGION_KEYWIDTH;
	(void)keysdb_bind(&vmm->regions_db, key_start, keys_nr, region);

	return (void*)region->vm_start;
    
MMAP_ERR2:			
	rwlock_wrlock(&task->vmm.rwlock);
	vm_region_detach(&task->vmm, region);
	rwlock_unlock(&task->vmm.rwlock);
	vmm_dmsg(1, "%s: pid %d, tid %x, faild err2\n", __FUNCTION__, task->pid, this);

MMAP_ERR1:
	req.ptr = region;
	kmem_free(&req);
	vmm_dmsg(1, "%s: pid %d, tid %x, faild err1\n", __FUNCTION__, task->pid, this);
  
MMAP_ERR:
	current_thread->info.errno = err;
	vmm_dmsg(1, "%s: pid %d, tid %x, faild err %d\n", __FUNCTION__, task->pid, this, err);
	return VM_FAILED;
}

error_t vmm_sbrk(struct vmm_s *vmm, uint_t current, uint_t size)
{	
	struct vm_region_s *heap;
	uint_t irq_sate;
	error_t err;

	if(current < vmm->heap_current)
		return EAGAIN;

	if((vmm->heap_current + size) > CONFIG_TASK_HEAP_MAX_SIZE)
		return ENOMEM;

	heap = vmm->heap_region;

	mcs_lock(&heap->vm_lock, &irq_sate);

	if((vmm->heap_current + size) > CONFIG_TASK_HEAP_MAX_SIZE)
		err = ENOMEM;
	else
	{
		heap->vm_limit += size;
		err = 0;
	}

	mcs_unlock(&heap->vm_lock, irq_sate);
	return err;
}


error_t vmm_madvise_migrate(struct vmm_s *vmm, uint_t start, uint_t len)
{
	register error_t err;
	register uint_t count;
	register uint_t vaddr;
	register struct pmm_s *pmm;
	pmm_page_info_t info;

	count   = ARROUND_UP(len, PMM_PAGE_SIZE);
	count >>= PMM_PAGE_SHIFT;
	vaddr   = start;
	pmm     = &vmm->pmm;

	while(count)
	{
		if((err = pmm_get_page(pmm, vaddr, &info)))
			goto NEXT;

		if(info.attr & PMM_PRESENT)
		{
			info.attr   &= ~(PMM_PRESENT);
			info.attr   |= PMM_MIGRATE;
			info.cluster = NULL;
			(void)pmm_set_page(pmm, vaddr, &info);
		}

	NEXT:
		vaddr += PMM_PAGE_SIZE;
		count --;
	}
  
	return 0;
}

/* TODO: we should be able to apply this strategy on VM_REG_SHARED */
error_t vmm_set_auto_migrate(struct vmm_s *vmm, uint_t start, uint_t flags)
{
	struct list_entry *entry;
	struct vm_region_s *region;

	region = vm_region_find(vmm, start);
  
	if((region == NULL) || (region->vm_start < start))
		return ESRCH;

	entry = &region->vm_list;

	while(entry != NULL)
	{
		region = list_element(entry, struct vm_region_s, vm_list);
        
		if(((region->vm_flags & VM_REG_STACK) && !(flags & MGRT_STACK))  ||
		   (region->vm_flags  & VM_REG_SHARED) || (region->vm_flags & VM_REG_DEV))
			goto skip_current_region;

		vmm_madvise_migrate(vmm, region->vm_start, region->vm_limit - region->vm_start);
  
	skip_current_region:
		entry = list_next(&vmm->regions_root, entry);
	}

	return 0;
}

error_t vmm_madvise_willneed(struct vmm_s *vmm, uint_t start, uint_t len)
{
	register error_t err;
	register uint_t count;
	register uint_t vaddr;
	register struct pmm_s *pmm;
	pmm_page_info_t info;
  
	return ENOSYS;

	count   = ARROUND_UP(len, PMM_PAGE_SIZE);
	count >>= PMM_PAGE_SHIFT;
	vaddr   = start;
	pmm     = &vmm->pmm;
  
	while(count)
	{
		if((err = pmm_get_page(pmm, vaddr, &info)))
			goto NEXT;

		if(!(info.attr & PMM_PRESENT))
		{
			vmm_dmsg(3, "%s: vaddr %x <-> attr %x, ppn %x\n", 
				 __FUNCTION__, 
				 vaddr, 
				 info.attr, 
				 info.ppn);

			info.attr &= ~(PMM_PRESENT);
			info.attr |= PMM_MIGRATE;
			info.cluster = NULL;
			pmm_set_page(pmm, vaddr, &info);
		}

	NEXT:
		vaddr += PMM_PAGE_SIZE;
		count --;
	}

	return 0;
}


static inline error_t vmm_do_migrate(struct vm_region_s *region, pmm_page_info_t *pinfo, uint_t vaddr)
{
	kmem_req_t req;
	pmm_page_info_t current;
	struct page_s *page;
	struct page_s *newpage;
	struct cluster_s *cluster;
	register uint_t count;
	error_t err;
 
	assert(pinfo->ppn != 0);

	cluster = current_cluster;
	page    = ppm_ppn2page(pmm_ppn2ppm(pinfo->ppn), pinfo->ppn);
	newpage = NULL;
  
	current.attr = 0;
	current.ppn  = 0;

	page_lock(page);
  
	err = pmm_get_page(&region->vmm->pmm, vaddr , &current);

	if(err) goto VMM_MIGRATE_ERR;

	if((current.ppn == pinfo->ppn) && (current.attr & PMM_MIGRATE))
	{
		if(cluster->id != page->cid)
		{
			req.type  = KMEM_PAGE;
			req.size  = 0;
			req.flags = AF_USR | AF_PRIO | AF_TTL_AVRG;

			newpage = kmem_alloc(&req);
      
			if(newpage != NULL)
			{
				newpage->mapper = NULL;
				page_copy(newpage, page);
     
				if(current.attr & PMM_COW)
				{
					current.attr |= PMM_WRITE;
					current.attr &= ~(PMM_COW);
	  
					if((page->mapper == NULL) && 
					   ((count=page_refcount_get(page)) != 1))
					{
						page_refcount_down(page);
					}
				}
				current.ppn = ppm_page2ppn(newpage);
			}
		}

		current.attr   |= PMM_PRESENT;
		current.attr   &= ~(PMM_MIGRATE);
		current.cluster = NULL;

		err = pmm_set_page(&region->vmm->pmm, vaddr, &current);

		if((newpage != NULL) && (newpage->cid != cluster->id))
		{
			current_thread->info.remote_pages_cntr ++;

#if CONFIG_SHOW_REMOTE_PGALLOC  
			printk(INFO, "%s: pid %d, tid %x, cpu %d, cid %d: got new remote page from cluster %d (vaddr %x)\n",
			       __FUNCTION__,
			       current_task->pid,
			       current_thread,
			       cpu_get_id(),
			       cluster->id,
			       newpage->cid,
			       vaddr);
#endif	     
		}

#if CONFIG_SHOW_VMMMGRT_MSG
		printk(INFO, "%s: pid %d, tid %d, cpu %d: Asked to migrate page (vaddr %x) from cluster %d to cluster %d, err %d\n",
		       __FUNCTION__,
		       current_task->pid,
		       current_thread->info.order,
		       cpu_get_id(),
		       vaddr,
		       page->cid,
		       cluster->id,
		       err);
#endif
	}
	else
		current_thread->info.spurious_pgfault_cntr ++;
 
VMM_MIGRATE_ERR:
  
	page_unlock(page);

/* TODO: we should differ the kmem_free call */
	if(err)
	{
		if(newpage != NULL)
		{
			req.ptr = newpage;
			kmem_free(&req);
		}
	}
	else
	{
		if(newpage != NULL)
		{
			req.ptr = page;
			kmem_free(&req);
		}
	}
 
	return err;
}

error_t vmm_do_cow(struct vm_region_s *region, struct page_s *page, pmm_page_info_t *pinfo, uint_t vaddr)
{
	register struct page_s *newpage;
	register struct thread_s *this;
	register error_t err;
	register uint_t count;
	register bool_t isFreeable;
	pmm_page_info_t info;
	kmem_req_t req;

	this       = current_thread;
	info.attr  = 0;
	newpage    = NULL;
	isFreeable = false;

	vmm_dmsg(2,"%s: pid %d, tid %d, cpu %d, vaddr %x\n",
		 __FUNCTION__,
		 this->task->pid,
		 this->info.order,
		 cpu_get_id(),
		 vaddr);

	page_lock(page);
  
	err = pmm_get_page(&region->vmm->pmm, vaddr, &info);
    
	if((err != 0) || !(info.attr & PMM_COW))
	{
#if CONFIG_SHOW_SPURIOUS_PGFAULT
		printk(INFO, "%s: pid %d, tid %d, cpu %d, nothing to do for vaddr %x\n", 
		       __FUNCTION__, 
		       this->task->pid, 
		       this->info.order, 
		       cpu_get_id(),
		       vaddr);
#endif

		this->info.spurious_pgfault_cntr ++;
		goto VMM_COW_END;
	}

	if(page->mapper == NULL)
	{
		count = page_refcount_get(page);
    
		if(count == 1)
		{
			//if((info.attr & PMM_MIGRATE) && (page->cid != current_cluster->id))
			if(page->cid != current_cluster->id)
				isFreeable = true;
			else
			{
				newpage = page;
	
				vmm_dmsg(2, "%s: pid %d, tid %d, cpu %d, reuse same page for vaddr %x, pg_addr %x\n", 
					 __FUNCTION__, 
					 this->task->pid, 
					 this->info.order, 
					 cpu_get_id(), 
					 vaddr, 
					 ppm_page2addr(page));
			}
		}
		else
			page_refcount_down(page);
	}

	if(newpage == NULL)
	{    
		req.type  = KMEM_PAGE;
		req.size  = 0;
		req.flags = AF_USER;
  
		if((newpage = kmem_alloc(&req)) == NULL)
		{
			err = ENOMEM;
			goto VMM_COW_END;
		}

		newpage->mapper = NULL;
		page_copy(newpage,page);

		vmm_dmsg(2, 
			 "%s: pid %d, tid %d, cpu %d, newpage for vaddr %x, pg_addr %x\n", 
			 __FUNCTION__, 
			 this->task->pid, 
			 this->info.order, 
			 cpu_get_id(), 
			 vaddr, 
			 ppm_page2addr(newpage));

		if(newpage->cid != current_cluster->id)
			this->info.remote_pages_cntr ++;
	}

	info.attr    = region->vm_pgprot | PMM_WRITE;
	info.attr   &= ~(PMM_COW | PMM_MIGRATE);
	info.ppn     = ppm_page2ppn(newpage);
	info.cluster = NULL;

	err = pmm_set_page(&region->vmm->pmm, vaddr, &info);
  
VMM_COW_END:
	page_unlock(page);
  
	if(isFreeable)
	{
		req.type  = KMEM_PAGE;
		req.size  = 0;
		req.flags = AF_USER;
		req.ptr   = page;

		kmem_free(&req);
	}

	vmm_dmsg(2, "%s, pid %d, tid %d, cpu %d, COW ended [vaddr %x]\n", 
		 __FUNCTION__, 
		 this->task->pid,
		 this->info.order,
		 cpu_get_id(),
		 vaddr);

	return err;
}


static inline error_t vmm_do_mapped(struct vm_region_s *region, uint_t vaddr, uint_t flags)
{
	struct page_s *page;
	uint_t index;
	error_t err;
	bool_t isDone;
	pmm_page_info_t info;
	pmm_page_info_t current;

	index = ((vaddr - region->vm_start) + region->vm_offset) >> PMM_PAGE_SHIFT;
    
	page = mapper_get_page(region->vm_mapper, 
			       index, 
			       MAPPER_SYNC_OP,
			       region->vm_file);

	if(page == NULL)
		return (region->vm_file == NULL) ? EIO : ENOMEM;
  
	info.attr    = region->vm_pgprot;
	info.ppn     = ppm_page2ppn(page);
	info.cluster = NULL;
  
	current.attr = 1;
	current.ppn  = 1;
	isDone       = false;
  
	page_lock(page);
  
	err = pmm_get_page(&region->vmm->pmm, vaddr, &current);

	if(err == 0)
	{
		if(current.attr == 0)
		{
			err = pmm_set_page(&region->vmm->pmm, vaddr, &info);
		}
		else
		{
			isDone = true;
			current_thread->info.spurious_pgfault_cntr ++;      

#if CONFIG_SHOW_SPURIOUS_PGFAULT
			printk(INFO, "%s: pid %d, tid %d, cpu %d, nothing to do for vaddr %x, attr %x, ppn %x, flags %x)\n", 
			       __FUNCTION__, 
			       current_task->pid,
			       current_thread->info.order,
			       cpu_get_id(), 
			       vaddr, 
			       current.attr, 
			       current.ppn, 
			       flags);
#endif
      
#if CONFIG_SHOW_SPURIOUS_PGFAULT
			struct page_s *old;

			old = ppm_ppn2page(pmm_ppn2ppm(current.ppn), current.ppn);
      
			if(old != page)
			{
				printk(WARNING, "WARNING: %s: spurious pgfault on vaddr %x, with old %x, page %x\n", 
				       __FUNCTION__, 
				       vaddr,
				       old, 
				       page);
			}
#endif
		}
	}

	page_unlock(page);
     
	return err;
}

static inline error_t vmm_do_aod(struct vm_region_s *region, uint_t vaddr)
{
	register error_t err;
	register struct page_s *page;
	register struct cluster_s *cluster;
	struct thread_s *this;
	pmm_page_info_t old;
	pmm_page_info_t new;
	kmem_req_t req;

	page      = NULL;
	old.attr  = 0;
	this      = current_thread;
  
	err = pmm_lock_page(&region->vmm->pmm, vaddr, &old);

	if(err) return err;

	if(old.isAtomic == false)
	{
		this->info.spurious_pgfault_cntr ++;
		pmm_tlb_flush_vaddr(vaddr, PMM_DATA);
		return 0;
	}

	req.type  = KMEM_PAGE;
	req.size  = 0;
	req.flags = AF_USER | AF_ZERO;

	if((page = kmem_alloc(&req)) == NULL)
	{
		(void)pmm_unlock_page(&region->vmm->pmm, vaddr, &old);
		return ENOMEM;
	}

	page->mapper = NULL;

	new.attr    = region->vm_pgprot;
	new.ppn     = ppm_page2ppn(page);
	new.cluster = NULL;

	err = pmm_set_page(&region->vmm->pmm, vaddr, &new);
	
	if(err) goto fail_set_pg;

	cluster = current_cluster;

	if(page->cid != cluster->id)
		this->info.remote_pages_cntr ++;

	return 0;

fail_set_pg:
	(void)pmm_unlock_page(&region->vmm->pmm, vaddr, &old);
	req.ptr = page;
	kmem_free(&req);

	vmm_dmsg(3, "%s: ended [ err %d ]\n", __FUNCTION__, err);
	return err;
}

VM_REGION_PAGE_FAULT(vmm_default_pagefault)
{
	register struct thread_s *this;
	register struct page_s *page;
	register error_t err;
	pmm_page_info_t info;

	if((err = pmm_get_page(&region->vmm->pmm, vaddr, &info)))
		return err;

	if((info.attr != 0) && (info.ppn != 0))
	{
		if((info.attr & PMM_COW) && pmm_except_isWrite(flags))
		{
			page = ppm_ppn2page(pmm_ppn2ppm(info.ppn), info.ppn);
			err = vmm_do_cow(region, page, &info, vaddr);
			return err;
		}

		if(info.attr & PMM_MIGRATE)
			return vmm_do_migrate(region, &info, vaddr);

		if(info.attr & PMM_PRESENT)
		{
			this = current_thread;

#if CONFIG_SHOW_SPURIOUS_PGFAULT
			printk(WARNING, "WARNING: %s: pid %d, tid %d, cpu %d, flags %x but vaddr is valid %x, attr %x, ppn %x\n",
			       __FUNCTION__,
			       this->task->pid,
			       this->info.order,
			       cpu_get_id(),
			       flags,
			       vaddr,
			       info.attr,
			       info.ppn);
#endif

			current_thread->info.spurious_pgfault_cntr ++;
			pmm_tlb_flush_vaddr(vaddr, PMM_UNKNOWN);
			return 0;
		}

#if CONFIG_SHOW_VMM_ERROR_MSG
		printk(ERROR, 
		       "ERROR: %s: pid %d, cpu %d, Unexpected page attributes configuration for vaddr %x, found: ppn %x, attr %x\n",
		       __FUNCTION__,
		       current_task->pid,
		       cpu_get_id(), 
		       vaddr, 
		       info.ppn,
		       info.attr);
#endif
    
		return EPERM;
	}

	if(region->vm_mapper != NULL)
		return vmm_do_mapped(region, vaddr, flags);

	return vmm_do_aod(region, vaddr);
}

const struct vm_region_op_s vm_region_default_op =
{
	.page_in     = NULL,
	.page_out    = NULL,
	.page_lookup = NULL,
	.page_fault  = vmm_default_pagefault
};


void vmm_keysdb_update(struct vmm_s *vmm, struct vm_region_s *region, uint_t vaddr)
{
	uint_t key;
	uint_t count;

	key   = region->vm_begin >> CONFIG_VM_REGION_KEYWIDTH;
	count = (region->vm_end - region->vm_begin) >> CONFIG_VM_REGION_KEYWIDTH;

	(void)keysdb_bind(&vmm->regions_db, key, count, region);
}

error_t vmm_fault_handler(uint_t bad_vaddr, uint_t flags)
{
	register struct task_s *task;
	register struct thread_s *this;
	register struct vmm_s *vmm;
	struct vm_region_s *region;
	thread_state_t old_state;
	error_t err;
	bool_t isMiss;

	this      = current_thread;
	old_state = this->state;
	isMiss    = 0;

	if(old_state == S_USR)
	{
		this->state = S_KERNEL;
		tm_usr_compute(this);
	}

	cpu_enable_all_irq(NULL);

	this->info.pgfault_cntr ++;

	vmm_dmsg(2, "INFO: %s: cycle %u, cpu %d, tid %x, pid %d, vaddr 0x%x, flags %x\n", 
		 __FUNCTION__, 
		 cpu_time_stamp(), 
		 cpu_get_id(), 
		 this, 
		 this->task->pid, 
		 bad_vaddr, 
		 flags);

	if(bad_vaddr >= CONFIG_KERNEL_OFFSET)
	{
#if CONFIG_SHOW_VMM_ERROR_MSG
		printk(INFO, "%s: cycle %u, cpu %d, pid %d, tid %x, vaddr 0x%x exceed user limit\n",
		       __FUNCTION__, 
		       cpu_time_stamp(), 
		       cpu_get_id(), 
		       this->task->pid, 
		       this, bad_vaddr);
#endif
		if(pmm_except_isInKernelMode(flags))
		{
			err = VMM_ECHECKUSPACE;
			goto FAULT_END;
		}

		goto FAULT_SEND_SIGBUS;
	}
  
	task = this->task;
	vmm = &task->vmm;

	if(this->type == KTHREAD)
	{
#if CONFIG_SHOW_VMM_ERROR_MSG
		printk(INFO, "%s: cpu %d, pid %d, tid %x is a kernel thread (vaddr 0x%x)\n",
		       __FUNCTION__,
		       cpu_get_id(),
		       this->task->pid, 
		       this, 
		       bad_vaddr);
#endif
		err = VMM_ECHECKUSPACE;
		goto FAULT_END;
	}
  
	//rwlock_rdlock(&task->vmm.rwlock);

	//region = vmm->last_region;
  
	//if((bad_vaddr >= region->vm_limit) || (bad_vaddr < region->vm_start))
	{
		//region = vm_region_find(vmm, bad_vaddr);

#if CONFIG_SHOW_VMM_LOOKUP_TM
		register uint_t tm_start;
		tm_start = cpu_time_stamp();
#endif

		//rwlock_rdlock(&task->vmm.rwlock);
		region = keysdb_lookup(&vmm->regions_db, bad_vaddr >> CONFIG_VM_REGION_KEYWIDTH);
  
		if(region == NULL)
		{
			rwlock_rdlock(&task->vmm.rwlock);
			region = vm_region_find(vmm, bad_vaddr);
			rwlock_unlock(&task->vmm.rwlock);

			isMiss = true;
		}

		if((region == NULL) || (bad_vaddr > region->vm_limit) || (bad_vaddr < region->vm_start))
		{
			//rwlock_unlock(&task->vmm.rwlock);

			/* vmm_region_per_thread_find() */

			if(pmm_except_isInKernelMode(flags))
			{
#if CONFIG_SHOW_VMM_ERROR_MSG
				printk(INFO, "%s: cpu %d, pid %d, tid %x, thread is in kernel mode (vaddr 0x%x)\n",
				       __FUNCTION__,
				       cpu_get_id(),
				       this->task->pid, 
				       this, 
				       bad_vaddr);
#endif
				err = VMM_ECHECKUSPACE;
				goto FAULT_END;
			}

			printk(INFO, "%s: cpu %d, pid %d, tid %d, region not found for vaddr 0x%x [%x]\n", 
			       __FUNCTION__, 
			       cpu_get_id(),
			       this->task->pid, 
			       this->info.order, 
			       bad_vaddr,
				region);
      
			goto FAULT_SEND_SIGBUS;
		}

		vmm_dmsg(2, "%s: pid %d, tid %d, region found for vaddr 0x%x\n",
			 __FUNCTION__, 
			 this->task->pid, 
			 this->info.order, 
			 bad_vaddr);
	}

	if(isMiss)
		vmm_keysdb_update(vmm, region, bad_vaddr);
    
	//atomic_add(&region->vm_refcount, 1);
	//rwlock_unlock(&task->vmm.rwlock);

#if CONFIG_SHOW_VMM_LOOKUP_TM
	printk(INFO, "%s: lookup time %d\n", __FUNCTION__, cpu_time_stamp() - tm_start);
#endif

	vmm_dmsg(2,"%s: cpu %d, pid %d, tid %x, vaddr 0x%x found region <b %x - %x e>\n",
		 __FUNCTION__, 
		 cpu_get_id(), 
		 this->task->pid, 
		 this, 
		 bad_vaddr, 
		 region->vm_begin,
		 region->vm_end);

	err = vm_region_update(region, bad_vaddr, flags);
	//atomic_add(&region->vm_refcount, -1);
  
	vmm_dmsg(1,"%s: cpu %d, pid %d, tid %x, vaddr 0x%x region updated with err %d\n",
		 __FUNCTION__,
		 cpu_get_id(),
		 this->task->pid,
		 this,
		 bad_vaddr,
		 err);
  
	if(err) goto FAULT_SEND_SIGSEGV;
  
	err = VMM_ERESOLVED;
	goto FAULT_END;
  
FAULT_SEND_SIGSEGV:
	err = VMM_ESIGSEGV;

FAULT_SEND_SIGBUS:
	err = VMM_ESIGBUS;

	printk(ERROR, "ERROR: %s: Pid %d, Thread %x, CPU %d, cycle %d\t [ KILLED ]\n", 
	       __FUNCTION__, 
	       this->task->pid, 
	       this, 
	       cpu_get_id(), 
	       cpu_time_stamp());
  
#if 0
	if(this->type == PTHREAD)
		sched_exit(this);
#endif
  
FAULT_END:
  
	cpu_disable_all_irq(NULL);

	if(old_state == S_USR)
	{
		if(thread_sched_isActivated(this))
		{
			thread_set_cap_migrate(this);
			sched_yield(this);
			thread_clear_cap_migrate(this);
			
			this = current_thread;
			cpu_wbflush();
		}

		tm_sys_compute(this);
		this->state = S_USR;
	}
  
	vmm_dmsg(2, "%s: pid %d, cpu %d: ended [bad_vaddr 0x%x]\n", 
		 __FUNCTION__, 
		 this->task->pid, 
		 cpu_get_id(), 
		 bad_vaddr);

	return err;
}
