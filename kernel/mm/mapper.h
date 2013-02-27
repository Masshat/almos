/*
 * mm/mapper.h - mapping object and its related operations
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

#ifndef _MAPPER_H_
#define _MAPPER_H_

#include <types.h>
#include <radix.h>
#include <mcs_sync.h>
#include <atomic.h>
#include <list.h>

struct vfs_file_s;
struct page_s;

#define MAPPER_SYNC_OP              0x01

#define MAPPER_READ_PAGE(n)         error_t (n) (struct page_s *page, uint_t flags, void *data)
#define MAPPER_WRITE_PAGE(n)        error_t (n) (struct page_s *page, uint_t flags, void *data)
#define MAPPER_SYNC_PAGE(n)         error_t (n) (struct page_s *page)
#define MAPPER_RELEASE_PAGE(n)      error_t (n) (struct page_s *page)
#define MAPPER_SET_PAGE_DIRTY(n)    error_t (n) (struct page_s *page)
#define MAPPER_CLEAR_PAGE_DIRTY(n)  error_t (n) (struct page_s *page)

typedef MAPPER_READ_PAGE(mapper_read_page_t);
typedef MAPPER_WRITE_PAGE(mapper_write_page_t);
typedef MAPPER_SYNC_PAGE(mapper_sync_page_t);
typedef MAPPER_RELEASE_PAGE(mapper_release_page_t);
typedef MAPPER_SET_PAGE_DIRTY(mapper_set_page_dirty_t);
typedef MAPPER_CLEAR_PAGE_DIRTY(mapper_clear_page_dirty_t);

/* caller must have exclusive access to the page! */
struct mapper_op_s 
{
	/* read page from mapper's backend*/
	mapper_read_page_t	    *readpage;

	/* write page to mapper's backend */
	mapper_write_page_t	    *writepage;

	/* sync page if it's dirty */
	mapper_sync_page_t          *sync_page;
  
	/* set page dirty attribute, return true if this dirtied it */
	mapper_set_page_dirty_t     *set_page_dirty;

	/* clear page's dirty attribute, return true if this cleared it */
	mapper_clear_page_dirty_t   *clear_page_dirty;

	/* Release a page from mapper */
	mapper_release_page_t	    *releasepage;
};

struct mapper_s 
{
	atomic_t                     m_refcount;
	//spinlock_t		     m_lock;
	mcs_lock_t	             m_lock;
	radix_t			     m_radix;    // pages depot
	const struct mapper_op_s*    m_ops;	    // operations
	struct vfs_node_s*           m_node;	    // owner
	void*                        m_data;	    // private data
	mcs_lock_t	             m_reg_lock;
	struct list_entry            m_reg_root;
};


// MAPPER API

/**
 * Inititalizes the kmem-cache for the mapper structure
 */
KMEM_OBJATTR_INIT(mapper_kmem_init);

/**
 * Init new mapper object
 *
 * @mapper      mapper object to initialize
 * @ops         mapper's operations
 * @node        mapper's associated node if any
 * @data        Private data if any
 */
error_t mapper_init(struct mapper_s *mapper, 
		    const struct mapper_op_s *ops, 
		    struct vfs_node_s *node,
		    void *data);

/**
 * Finds and gets a page reference.
 *
 * @mapper	mapper to search-in
 * @index	page index
 * @return	page looked up, NULL if not present
 */
struct page_s* mapper_find_page(struct mapper_s* mapper, uint_t index);

/**
 * Finds and gets up to @nr_pages pages references,
 * starting at @start and stores them in @pages.
 *
 * @mapper	mapper to search
 * @start	first page index to be looked up
 * @nr_pages	max pages to look up for
 * @pages	pages looked up
 * @return	number of pages actually in @pages
 */
uint_t mapper_find_pages(struct mapper_s* mapper, 
			 uint_t start, 
			 uint_t nr_pages, 
			 struct page_s** pages);

/**
 * Finds and gets up to @nr_pages pages references,
 * starting at @start and stores them in @pages.
 * Ensures that the returned pages are contiguous.
 *
 * @mapper	mapper to search
 * @start	first page index to be looked up
 * @nr_pages	max pages to look up for
 * @pages	pages looked up
 * @return	number of pages actually in @pages
 */
uint_t mapper_find_pages_contig(struct mapper_s* mapper, 
				uint_t start, 
				uint_t nr_pages, 
				struct page_s** pages);

/**
 * Finds and gets up to @nr_pages pages references,
 * with the tag @tag set, starting at @start
 * and stores them in @pages.
 *
 * @mapper	mapper object to search
 * @start	first page index to be looked up
 * @tag		tag to look for
 * @nr_pages	max pages to look up for
 * @pages	pages looked up
 * @return	number of pages actually in @pages
 */
uint_t mapper_find_pages_by_tag(struct mapper_s* mapper, 
				uint_t start, 
				uint_t tag, 
				uint_t nr_pages, 
				struct page_s** pages);

/**
 * Adds newly allocated pagecache pages.
 *
 * @mapper	mapper object
 * @page	page to add
 * @index	page index
 * @return	error code
 */
error_t mapper_add_page(struct mapper_s *mapper, struct page_s* page, uint_t index);

/**
 * Removes a page from the pagecache and frees it.
 *
 * @page	page to remove
 */
void mapper_remove_page(struct page_s* page);

/**
 * Reads into the pagecache, fills it if needed.
 * If the page already exists, ensures that it is
 * up to date.
 *
 * @mapper	mapper for the page
 * @index	page index
 * @flags       to be set to MAPPER_OP_SYNC for blocking mode
 * @data        opaque parameter
 * @return	the page read
 */
struct page_s* mapper_get_page(struct mapper_s*	mapper, uint_t index, uint_t flags, void *data);


/**
 * Writes and frees all the dirty pages from a mapper.
 *
 * @mapper	mapper to be destroyed
 * @doSync      Sync each dirty page before removing it
 */
void mapper_destroy(struct mapper_s *mapper, bool_t doSync);

/**
 * Generic method to read page (zeroed).
 * @page        buffer page to be zeroed
 * @flags       to be set to MAPPER_SYNC_OP if blocking request
 * @data        not used
 */
MAPPER_READ_PAGE(mapper_default_read_page);

/**
 * Generic method to write page (do nothing).
 * @page        not used
 * @flags       not used
 * @data        not used
 */
MAPPER_READ_PAGE(mapper_default_write_page);

/**
 * Generic method to release a page.
 *
 * @page	buffer page to be released
 * @return	error code, 0 if OK
 */
MAPPER_RELEASE_PAGE(mapper_default_release_page);

/**
 * Generic method to sync a page.
 *
 * @page	buffer page to be synced
 * @return	error code, 0 if OK
 */
MAPPER_SYNC_PAGE(mapper_default_sync_page);

/**
 * Generic method to dirty a page.
 *
 * @page	buffer page to be dirtied
 * @return	non-zero if the page got dirtied
 */
MAPPER_SET_PAGE_DIRTY(mapper_default_set_page_dirty);

/**
 * Generic method to clear a dirty page.
 *
 * @page	buffer page to be cleared
 * @return	non-zero if the page had been cleared
 */
MAPPER_CLEAR_PAGE_DIRTY(mapper_default_clear_page_dirty);

// MAPPER API END

#endif /* _MAPPER_H_ */
