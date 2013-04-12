/*
 * mm/mapper.c - mapping object used to map memory, file or device in
 * process virtual address space.
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

#include <mapper.h>
#include <radix.h>
#include <thread.h>
#include <cpu.h>
#include <task.h>
#include <kmem.h>
#include <kcm.h>
#include <page.h>
#include <blkio.h>
#include <cluster.h>
#include <vfs.h>


static void mapper_ctor(struct kcm_s *kcm, void *ptr) 
{
	struct mapper_s *mapper;

	mapper = (struct mapper_s *)ptr;
	radix_tree_init(&mapper->m_radix);
	mcs_lock_init(&mapper->m_lock, "Mapper Object");
}

KMEM_OBJATTR_INIT(mapper_kmem_init)
{
	attr->type   = KMEM_MAPPER;
	attr->name   = "KCM Mapper";
	attr->size   = sizeof(struct mapper_s);
	attr->aligne = 0;
	attr->min    = CONFIG_MAPPER_MIN;
	attr->max    = CONFIG_MAPPER_MAX;
	attr->ctor   = mapper_ctor;
	attr->dtor   = NULL;
	return 0;
}

error_t mapper_init(struct mapper_s *mapper, 
		    const struct mapper_op_s *ops, 
		    struct vfs_node_s *node,
		    void *data)
{
	atomic_init(&mapper->m_refcount, 1);
	mapper->m_ops  = ops;
	mapper->m_node = node;
	mapper->m_data = data;
	return 0;
}

/* FIXME: check dummy pages inserted by mapper_get */
struct page_s* mapper_find_page(struct mapper_s* mapper, uint_t index) 
{
	struct page_s *page;
	uint_t irq_state;

	mcs_lock(&mapper->m_lock, &irq_state);
	page = radix_tree_lookup(&(mapper->m_radix), index);
	mcs_unlock(&mapper->m_lock, irq_state);

	return page;
}

/* FIXME: check dummy pages inserted by mapper_get */
uint_t mapper_find_pages(struct mapper_s* mapper, 
			 uint_t start, 
			 uint_t nr_pages, 
			 struct page_s** pages)
{
	uint_t pages_nr;
	uint_t irq_state;

	mcs_lock(&mapper->m_lock, &irq_state);

	pages_nr = radix_tree_gang_lookup(&(mapper->m_radix),
					  (void**)pages,
					  start,
					  nr_pages);

	mcs_unlock(&mapper->m_lock, irq_state);
	return pages_nr;
}

/* FIXME: check dummy pages inserted by mapper_get */
uint_t mapper_find_pages_contig(struct mapper_s* mapper, 
				uint_t start, 
				uint_t nr_pages, 
				struct page_s** pages)
{
	uint_t pages_nr;
	uint_t irq_state;
	uint_t i;

	mcs_lock(&mapper->m_lock, &irq_state);
	pages_nr = radix_tree_gang_lookup(&(mapper->m_radix),
					  (void**)pages,
					  start,
					  nr_pages);
	mcs_unlock(&mapper->m_lock, irq_state);

	for(i = 0; i < pages_nr; i++) 
	{
		if(pages[i]->index != nr_pages+i)
			break;
	}

	pages[i] = NULL;
	return i;
}

/* FIXME: check dummy pages inserted by mapper_get */
uint_t mapper_find_pages_by_tag(struct mapper_s* mapper, 
				uint_t start, 
				uint_t tag, 
				uint_t nr_pages, 
				struct page_s** pages)
{
	uint_t pages_nr;
	uint_t irq_state;

	mcs_lock(&mapper->m_lock,&irq_state);

	pages_nr = radix_tree_gang_lookup_tag(&(mapper->m_radix),
					      (void**)pages,
					      start,
					      nr_pages,
					      tag);

	mcs_unlock(&mapper->m_lock, irq_state);
  
	return pages_nr;
}

error_t mapper_add_page(struct mapper_s *mapper, struct page_s* page, uint_t index)
{
	uint_t irq_state;
	error_t err;

	mcs_lock(&mapper->m_lock, &irq_state);
  
	err = radix_tree_insert(&(mapper->m_radix), index, page);
  
	if (err != 0) 
		goto ADD_PAGE_ERROR;

	page->mapper = mapper;
	page->index  = index;

ADD_PAGE_ERROR:
	mcs_unlock(&mapper->m_lock, irq_state);
	return err;
}

static inline void __mapper_remove_page(struct mapper_s *mapper, struct page_s *page)
{
	radix_tree_delete(&mapper->m_radix, page->index);
  
	PAGE_CLEAR(page, PG_DIRTY);

	if(PAGE_IS(page, PG_BUFFER))
		blkio_destroy(page);

	page->mapper = NULL;
}

void mapper_remove_page(struct page_s *page)
{
	struct mapper_s *mapper;
	kmem_req_t req;
	uint_t irq_state;

	mapper    = page->mapper;
	req.type  = KMEM_PAGE;
	req.ptr   = page;

	mcs_lock(&mapper->m_lock, &irq_state);
	__mapper_remove_page(mapper, page);
	mcs_unlock(&mapper->m_lock, irq_state);
	kmem_free(&req);
}

MAPPER_RELEASE_PAGE(mapper_default_release_page)
{
	struct mapper_s *mapper;
	uint_t irq_state;

	mapper = page->mapper;

	mcs_lock(&mapper->m_lock, &irq_state);
	__mapper_remove_page(mapper, page);
	mcs_unlock(&mapper->m_lock, irq_state);

	return 0;
}

struct page_s* mapper_get_page(struct mapper_s*	mapper, uint_t index, uint_t flags, void* data)
{
	kmem_req_t req;
	struct page_s dummy;
	struct page_s* page;
	radix_item_info_t info;
	uint_t irq_state;
	bool_t found;
	error_t err;
  
	req.type  = KMEM_PAGE;
	req.size  = 0;
	req.flags = AF_USER;

	while (1) 
	{
		mcs_lock(&mapper->m_lock, &irq_state);
		found = radix_item_info_lookup(&mapper->m_radix, index, &info);

		if ((found == false) || (info.item == NULL))   // page not in mapper, creating it 
		{
			page_init(&dummy, CLUSTER_NR);
			PAGE_CLEAR(&dummy, PG_INIT);
			PAGE_SET(&dummy, PG_INLOAD);
			page_refcount_up(&dummy);
			dummy.mapper = mapper;
			dummy.index  = index;
			err          = 0;

			if(found == true)
			{
				err = radix_item_info_apply(&mapper->m_radix, 
							    &info, 
							    RADIX_INFO_INSERT, 
							    &dummy);
			}
			else
			{
				err = radix_tree_insert(&mapper->m_radix, 
							index, 
							&dummy);
			}

			if(err) __mapper_remove_page(mapper, &dummy);

			mcs_unlock(&mapper->m_lock, irq_state);

			if(err) goto fail_radix;

			page = kmem_alloc(&req);
      
			if(page != NULL)
			{
				PAGE_SET(page, PG_INLOAD);
				page->mapper = mapper;
				page->index  = index;

				err = mapper->m_ops->readpage(page, flags, data);
			}

			if((page == NULL) || (err != 0)) 
			{ 
				mcs_lock(&mapper->m_lock, &irq_state);
				__mapper_remove_page(mapper, &dummy);
				wakeup_all(&dummy.wait_queue);
				mcs_unlock(&mapper->m_lock, irq_state);
	
				if(page != NULL)
				{
					if(PAGE_IS(page, PG_BUFFER))
						blkio_destroy(page);
	  
					page->mapper = NULL;
					req.ptr = page;
					kmem_free(&req);
				}

				goto fail_alloc_load;
			}
      
			mcs_lock(&mapper->m_lock, &irq_state);
			found = radix_item_info_lookup(&mapper->m_radix, index, &info);
			assert((found == true) && (info.item == &dummy));
			err = radix_item_info_apply(&mapper->m_radix, &info, RADIX_INFO_SET, page);
			assert(err == 0);
			PAGE_CLEAR(page, PG_INLOAD);
			wakeup_all(&dummy.wait_queue);
			mcs_unlock(&mapper->m_lock,irq_state);
			return page;
		}

		page = (struct page_s*)info.item;

		if(PAGE_IS(page, PG_INLOAD))
		{
			wait_on(&page->wait_queue, WAIT_LAST);
			mcs_unlock(&mapper->m_lock, irq_state);
			sched_sleep(current_thread);
			continue;
		}
    
		mcs_unlock(&mapper->m_lock, irq_state);
		return page;
	}

fail_radix:
	printk(ERROR, "ERROR: %s: cpu %d, pid %d, tid %d, failed to insert/modify node, err %d [%u]\n",
	       __FUNCTION__,
	       cpu_get_id(),
	       current_task->pid,
	       current_thread->info.order,
	       err);

	current_thread->info.errno = (err == ENOMEM) ? err : EIO;
	return NULL;

fail_alloc_load:
	printk(ERROR, "ERROR: %s: cpu %d, pid %d, tid %d, failed to alloc/load page, index %d, err %d [%u]\n",
	       __FUNCTION__,
	       cpu_get_id(),
	       current_task->pid,
	       current_thread->info.order,
	       index,
	       err,
	       cpu_time_stamp());

	current_thread->info.errno = (err == 0) ? ENOMEM : EIO;
	return NULL;
}

void mapper_destroy(struct mapper_s *mapper, bool_t doSync)
{
	kmem_req_t req;
	struct page_s* pages[255];
	uint_t count, j;

	req.type  = KMEM_PAGE;
	req.flags = AF_USER;

	if(doSync == true)
	{
		do   // writing && freeing all dirty pages
		{
			count = radix_tree_gang_lookup_tag(&mapper->m_radix,
							   (void**)pages,
							   0,
							   254,
							   TAG_PG_DIRTY);

			for (j=0; (j < count) && (pages[j] != NULL); ++j) 
			{
				page_lock(pages[j]);
				mapper->m_ops->sync_page(pages[j]);
				page_unlock(pages[j]);

				__mapper_remove_page(mapper,pages[j]);
				req.ptr = pages[j];
				kmem_free(&req);
			}
		}while(count != 0);
	}

	do   // freeing all pages
	{
		count = radix_tree_gang_lookup(&mapper->m_radix, (void**)pages, 0, 254);
 
		for (j=0; (j < count) && (pages[j] != NULL); ++j) 
		{
			if(PAGE_IS(pages[j], PG_DIRTY))
			{
				page_lock(pages[j]);
				page_clear_dirty(pages[j]);
				page_unlock(pages[j]);
			}

			__mapper_remove_page(mapper,pages[j]);
			req.ptr = pages[j];
			kmem_free(&req);
		}
	}while(count != 0);
}

MAPPER_SET_PAGE_DIRTY(mapper_default_set_page_dirty)
{
	bool_t done;
	uint_t irq_state;

	if(PAGE_IS(page, PG_DIRTY))
		return 0;

	struct mapper_s *mapper;
  
	mapper = page->mapper;

	mcs_lock(&mapper->m_lock, &irq_state);
	radix_tree_tag_set(&mapper->m_radix, page->index, TAG_PG_DIRTY);
	mcs_unlock(&mapper->m_lock, irq_state);

	done = page_set_dirty(page);  

	assert(done == true);
	return 1;
}

MAPPER_CLEAR_PAGE_DIRTY(mapper_default_clear_page_dirty)
{
	uint_t irq_state;
	bool_t done;

	if(!(PAGE_IS(page, PG_DIRTY)))
		return 0;

	struct mapper_s *mapper;
  
	mapper = page->mapper;

	mcs_lock(&mapper->m_lock, &irq_state);
	radix_tree_tag_clear(&mapper->m_radix, page->index, TAG_PG_DIRTY);
	mcs_unlock(&mapper->m_lock, irq_state);

	done = page_clear_dirty(page);

	assert(done == true);

	return 1;
}

MAPPER_SYNC_PAGE(mapper_default_sync_page)
{
	struct mapper_s *mapper;
	uint_t irq_state;
	error_t err;
	bool_t done;

	mapper = page->mapper;
	err    = 0;
    
	if(PAGE_IS(page, PG_DIRTY)) 
	{
		err = mapper->m_ops->writepage(page, MAPPER_SYNC_OP, NULL);
    
		if(err) goto MAPPER_SYNC_ERR;
    
		mcs_lock(&mapper->m_lock, &irq_state);
		radix_tree_tag_clear(&mapper->m_radix, page->index, TAG_PG_DIRTY);
		mcs_unlock(&mapper->m_lock, irq_state);
    
		done = page_clear_dirty(page);
		assert(done == true);
	}
  
MAPPER_SYNC_ERR:
	return err;
}

MAPPER_READ_PAGE(mapper_default_read_page)
{
	page_zero(page);
	return 0;
}

MAPPER_WRITE_PAGE(mapper_default_write_page)
{
	return 0;
}
