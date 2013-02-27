/*
 * vfs/vfs_file.c - vfs file related operations
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

#include <thread.h>
#include <cpu.h>
#include <string.h>
#include <kmem.h>
#include <vfs.h>
#include <page.h>
#include <ppm.h>
#include <pmm.h>
#include <vfs-private.h>
#include <spinlock.h>
#include <mapper.h>
#include <vm_region.h>

static void vfs_file_ctor(struct kcm_s *kcm, void *ptr)
{
	struct vfs_file_s *file;

	file = (struct vfs_file_s*) ptr;
  
	rwlock_init(&file->f_rwlock);
	list_root_init(&file->f_wait_queue);
}

KMEM_OBJATTR_INIT(vfs_kmem_file_init)
{
	attr->type   = KMEM_VFS_FILE;
	attr->name   = "KCM VFS File";
	attr->size   = sizeof(struct vfs_file_s);
	attr->aligne = 0;
	attr->min    = CONFIG_VFS_FILE_MIN;
	attr->max    = CONFIG_VFS_FILE_MAX;
	attr->ctor   = vfs_file_ctor;
	attr->dtor   = NULL;
	return 0;
}

struct vfs_file_s* vfs_file_get(struct vfs_node_s *node)
{
	struct vfs_file_s *file;
	kmem_req_t req;

	req.type  = KMEM_VFS_FILE;
	req.size  = sizeof(*file);
	req.flags = AF_KERNEL;

	if((file = kmem_alloc(&req)) == NULL)
		return NULL;

	atomic_init(&file->f_count, 1);
	file->f_offset  = 0;
	file->f_version = 0;
	file->f_node    = node;
	file->f_op      = node->n_ctx->ctx_file_op;
	file->f_pv      = NULL;
	return file;
}

VFS_MMAP_FILE(vfs_default_mmap_file)
{
	uint_t irq_state;

	region->vm_mapper = file->f_node->n_mapper;
	region->vm_file   = file;

	if(region->vm_flags & VM_REG_PRIVATE)
	{
		region->vm_pgprot &= ~(PMM_WRITE);
		region->vm_pgprot |= PMM_COW;
	}

#if CONFIG_USE_COA
	if((region->vm_flags & VM_REG_SHARED) && (region->vm_flags & VM_REG_INST))
	{
		region->vm_pgprot &= ~(PMM_PRESENT);
		region->vm_pgprot |= PMM_MIGRATE;
	}
#endif
	region->vm_flags |= VM_REG_FILE;

	if(region->vm_flags & VM_REG_SHARED)
	{
		mcs_lock(&region->vm_mapper->m_reg_lock, &irq_state);
		list_add_last(&region->vm_mapper->m_reg_root, &region->vm_mlist);
		mcs_unlock(&region->vm_mapper->m_reg_lock, irq_state);
		(void)atomic_add(&region->vm_mapper->m_refcount, 1);
	}

	printk(INFO,
	       "INFO: Region [%x,%x] has been mapped [%s, size %d]\n",
	       region->vm_start,
	       region->vm_limit,
	       file->f_node->n_name,
	       (uint32_t)file->f_node->n_size);

	return 0;
}

VFS_MUNMAP_FILE(vfs_default_munmap_file)
{
	uint_t irq_state;

	if(region->vm_flags & VM_REG_SHARED)
	{
		(void)atomic_add(&region->vm_mapper->m_refcount, -1);
		mcs_lock(&region->vm_mapper->m_reg_lock, &irq_state);
		list_unlink(&region->vm_mlist);
		mcs_unlock(&region->vm_mapper->m_reg_lock, irq_state);
	}

	return 0;
}


VFS_READ_FILE(vfs_default_read) 
{  
	struct vfs_node_s *node;
	struct mapper_s *mapper;
	struct page_s *page;
	uint8_t *pread;
	uint8_t *pbuff;
	size_t asked_size;
	uint_t current_offset;
	uint_t bytes_left;
 
	if(file->f_node->n_attr & VFS_FIFO)
		return -EINVAL;

	if(size == 0) return 0;

	node           = file->f_node;
	mapper         = node->n_mapper;
	asked_size     = size;
	pbuff          = buffer;
	current_offset = file->f_offset;

	while((size > 0) && (current_offset < node->n_size)) 
	{
		if ((page = mapper_get_page(mapper,
					    current_offset >> PMM_PAGE_SHIFT, 
					    MAPPER_SYNC_OP,
					    file)) == NULL)
			return -VFS_IO_ERR;

		pread  = (uint8_t*) ppm_page2addr(page);
		pread += current_offset % PMM_PAGE_SIZE;
		bytes_left = ((node->n_size - current_offset) > PMM_PAGE_SIZE) ? 
			PMM_PAGE_SIZE : (node->n_size - current_offset);
    
		if(size >= bytes_left)
		{
			memcpy(pbuff, pread, bytes_left);
			pbuff          += bytes_left;
			size           -= bytes_left;
			current_offset += bytes_left;
		} 
		else
		{
			memcpy(pbuff, pread, size);
			size = 0;
			current_offset += size;
		}

		if(thread_sched_isActivated(current_thread))
			sched_yield(current_thread);
	}

	return asked_size - size;
}


VFS_WRITE_FILE(vfs_default_write) 
{
	struct mapper_s *mapper;
	struct page_s *page;
	uint8_t *pwrite;
	uint8_t *pbuff;
	size_t asked_size;
	uint_t current_offset;
	uint_t index;
	error_t err;

	if(file->f_node->n_attr & VFS_FIFO)
		return -EINVAL;
  
	if(size == 0) return 0;

	mapper         = file->f_node->n_mapper;
	asked_size     = size;
	pbuff          = buffer;
	current_offset = file->f_offset;

	while(size > 0) 
	{
		index = current_offset >> PMM_PAGE_SHIFT;

		if ((page = mapper_get_page(mapper, 
					    index, 
					    MAPPER_SYNC_OP,
					    file)) == NULL)
			return -VFS_IO_ERR;
    
		pwrite  = (uint8_t*) ppm_page2addr(page);
		pwrite += current_offset % PMM_PAGE_SIZE;
    		page_lock(page);

		if((page->mapper != mapper) || (page->index != index))
			goto WRITE_PAGE_LOCK_FAILED;

		if(size >= PMM_PAGE_SIZE) 
		{
			err    = cpu_uspace_copy(pwrite, pbuff, PMM_PAGE_SIZE);
			pbuff += PMM_PAGE_SIZE;
			size  -= PMM_PAGE_SIZE;
			current_offset += PMM_PAGE_SIZE;
		}
		else 
		{
			err  = cpu_uspace_copy(pwrite, pbuff, size);
			size = 0;
		}
    
		if(err) goto WRITE_ERR;

		if (VFS_IS(file->f_flags, VFS_O_SYNC))
			mapper->m_ops->writepage(page, MAPPER_SYNC_OP, NULL);
		else
			mapper->m_ops->set_page_dirty(page);
    
	WRITE_PAGE_LOCK_FAILED:
		page_unlock(page);
	}

	vfs_dmsg(1,"%s Ended: written %d\n", __FUNCTION__, asked_size - size);
	return asked_size - size;

WRITE_ERR:
	mapper_remove_page(page);
	page_unlock(page);
	return -err;
}
