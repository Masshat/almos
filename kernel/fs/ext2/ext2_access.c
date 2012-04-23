/*
 * ext2/ext2_access.c - ext2 helper functions
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

#include <string.h>
#include <kmem.h>
#include <page.h>
#include <pmm.h>
#include <vfs.h>
#include <cluster.h>
#include <thread.h>
#include <task.h>
#include <mapper.h>

#include <ext2-private.h>

#define EXT2_MAX_TRY_NR 10

error_t ext2_blk_get(struct ext2_blk_req_s *rq)
{
	uint_t index;
	struct ext2_context_s *ctx;
	struct mapper_s *mapper;
	struct page_s *page;
	uint_t try;
	uint_t flags;
  
	ctx    = rq->ctx;
	mapper = ctx->mapper;
	index  = rq->blkid / ctx->blks_per_page;
	page   = NULL;
	flags  = (rq->flags & EXT2_GF_SYNC) ? MAPPER_SYNC_OP : 0;

	for(try = 0; try < EXT2_MAX_TRY_NR; try++)
	{
		page = mapper_get_page(mapper, index, flags, NULL);

		if(page == NULL)
			return -1;

		page_lock(page);

		if(page->mapper == mapper)
			break;

		page_unlock(page);
	}

	if(try == EXT2_MAX_TRY_NR)
		return ENOMEM;

	rq->page = page;
	rq->ptr  = (void*)((uint_t)ppm_page2addr(page) + (rq->blkid % ctx->blks_per_page) * ctx->blk_size);

	return 0;
}


error_t ext2_blk_put(struct ext2_blk_req_s *rq)
{
	if(rq->flags & EXT2_GF_FREE)
	{
		if(PAGE_IS(rq->page, PG_DIRTY))
			rq->page->mapper->m_ops->sync_page(rq->page);

		mapper_remove_page(rq->page);
		page_unlock(rq->page);
		return 0;
	}
    
	if(rq->flags & EXT2_GF_DIRTY)
		page_set_dirty(rq->page);
  
	page_unlock(rq->page);
	return 0;
}


/* this function do not modify g_flags, g_lock, g_id */
error_t ext2_group_init(struct ext2_context_s *ctx,
			struct ext2_group_s *group, 
			struct ext2_group_desc_s *ptr, 
			uint_t grp_id, 
			uint_t blk_id)
{
	struct ext2_blk_req_s req;
	sint_t index;
	error_t err;
  
	group->info.g_block_bitmap      = ptr->g_block_bitmap;
	group->info.g_inode_bitmap      = ptr->g_block_bitmap;
	group->info.g_inode_table       = ptr->g_inode_table;
	group->info.g_free_blocks_count = ptr->g_free_blocks_count;
	group->info.g_free_inodes_count = ptr->g_free_inodes_count;
	group->info.g_used_dirs_count   = ptr->g_used_dirs_count;

	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = ptr->g_block_bitmap;

	if((err = ext2_blk_get(&req)) != 0)
		return err;

	index = bitmap_ffc(req.ptr, ctx->blk_size);
  
	if((index == -1) && (ptr->g_free_blocks_count != 0))
	{
		printk(WARNING, "WARNING: %s: (blk) inconsistent group, id %d, count %d\n",
		       __FUNCTION__, grp_id, ptr->g_free_blocks_count);

		return EINVAL;
	}

	group->info.g_next_blk = (uint16_t) index;
	ext2_blk_put(&req);

	req.blkid = ptr->g_inode_bitmap;

	if((err = ext2_blk_get(&req)) != 0)
		return err;

	index = bitmap_ffc(req.ptr, ctx->blk_size);
  
	if(ptr->g_free_inodes_count != 0)
	{
		if((index == -1) || (index >= ctx->inodes_per_grp))
		{
			printk(WARNING, "WARNING: %s: (ino) inconsistent group, id %d, count %d\n",
			       __FUNCTION__, 
			       grp_id, 
			       ptr->g_free_inodes_count);
      
			return EINVAL;
		}
	}

	group->info.g_next_ino = (uint16_t) index;
	ext2_blk_put(&req);

	bool_t isAtomic = false;

	while((group->info.g_free_blocks_count > 0) && 
	      (grp_id < ctx->last_free_blk_grp)     && 
	      (isAtomic == false))
	{
		isAtomic = cpu_atomic_cas(&ctx->last_free_blk_grp, 
					  ctx->last_free_blk_grp, 
					  grp_id);
	}

	isAtomic = false;

	while((group->info.g_free_inodes_count > 0) && 
	      (grp_id < ctx->last_free_ino_grp)     && 
	      (isAtomic == false))
	{
		isAtomic = cpu_atomic_cas(&ctx->last_free_ino_grp, 
					  ctx->last_free_ino_grp, 
					  grp_id);
	}

	return 0;
}

error_t ext2_group_sync(struct ext2_context_s *ctx, struct ext2_group_s *group)
{
	struct ext2_blk_req_s req;
	struct ext2_group_desc_s *ptr;
	uint_t groups_per_blk;
	uint_t blkid;
	error_t err;

	groups_per_blk = ctx->blk_size / sizeof(*group);  
	blkid          = (ctx->blk_size == 1024) ? 2 : 1;
	blkid         += group->info.g_id / groups_per_blk; 

	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = blkid;
  
	if((err = ext2_blk_get(&req)) != 0)
		return err;
  
	ptr  = req.ptr;
	ptr += group->info.g_id % groups_per_blk;
	ptr->g_free_blocks_count = group->info.g_free_blocks_count;
	ptr->g_free_inodes_count = group->info.g_free_inodes_count;
	ptr->g_used_dirs_count   = group->info.g_used_dirs_count;

	req.flags = EXT2_GF_DIRTY;
	ext2_blk_put(&req);
	return 0;
}

error_t ext2_group_load(struct ext2_context_s *ctx, struct ext2_group_s *group)
{
	struct ext2_blk_req_s req;
	struct ext2_group_desc_s *ptr;
	uint_t groups_per_blk;
	uint_t blkid;
	error_t err;

	groups_per_blk = ctx->blk_size / sizeof(*group);  
	blkid  = (ctx->blk_size == 1024) ? 2 : 1;
	blkid += group->info.g_id / groups_per_blk; 

	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = blkid;
  
	if((err = ext2_blk_get(&req)) != 0)
		return err;
  
	ptr  = req.ptr;
	ptr += group->info.g_id % groups_per_blk;

	err  = ext2_group_init(ctx, group, ptr, group->info.g_id, blkid);
	ext2_blk_put(&req);
	return err;
}

error_t ext2_group_destroy(struct ext2_context_s *ctx, struct ext2_group_s *group)
{
  
	if((group->info.g_flags & EXT2_GF_VALID) && (group->info.g_flags & EXT2_GF_DIRTY))
		return ext2_group_sync(ctx,group);

	return 0;
}

error_t ext2_group_cache_init(struct ext2_context_s *ctx)
{
	struct page_s *page;
	struct ext2_group_desc_s *ptr;
	struct ext2_group_s *grp_ptr;
	struct ext2_group_cache_s *cache;
	struct ext2_blk_req_s req;
	kmem_req_t rq;
	error_t err;
	uint_t blk_count;
	uint_t blk_start;
	uint_t group_count;
	uint_t groups_per_blk;
	uint_t group;
	uint_t count;
	uint_t i;

	rq.type  = KMEM_PAGE;
	rq.size  = 0;
	rq.flags = AF_KERNEL; 

	page = kmem_alloc(&rq);

	if(page == NULL)
	{
		printk(WARNING, "WARNING: %s: no memory !\n", __FUNCTION__);
		return ENOMEM;
	}

	cache = ppm_page2addr(page);
	ctx->group_cache    = cache;
	ctx->group_cache_pg = page;

	spinlock_init(&cache->lock, "group-cache");

	group_count    = PMM_PAGE_SIZE / sizeof(struct ext2_group_s);
	group_count   -= (sizeof(struct ext2_group_cache_s) / sizeof(struct ext2_group_s)) + 1;
 
	group_count    = MIN(group_count, ctx->group_count);
	cache->count   = group_count;
	wait_queue_init(&cache->wait_queue, "group-cache");

	blk_count      = (ctx->group_count * sizeof(*ptr)) / ctx->blk_size;
	blk_count      = (blk_count == 0) ? 1 : blk_count;
	blk_start      = (ctx->blk_size == 1024) ? 2 : 1;
	groups_per_blk = ctx->blk_size / sizeof(*ptr);
	count          = 0;
	req.ctx        = ctx;
	req.flags      = EXT2_GF_SYNC;
  
	for(i = 0; i < blk_count; i++)
	{
		req.blkid = blk_start + i;
    
		if((err = ext2_blk_get(&req)) != 0)
			return err;

		for(group = 0; (group < groups_per_blk) && (count < group_count); group++, count++)
		{
			grp_ptr = &cache->tbl[count];
			grp_ptr->info.g_id = count;
			spin_init((slock_t*)&grp_ptr->info.g_lock);
      
			ptr = (struct ext2_group_desc_s*)req.ptr + group;
			err = ext2_group_init(ctx, &cache->tbl[count], ptr, count, req.blkid);

			if(err) break;
      
			grp_ptr->info.g_flags = EXT2_GF_VALID;
		}

		ext2_blk_put(&req);
    
		if(err) return err;
	}

	memset(cache->lru_bitmap, 0, sizeof(cache->lru_bitmap));
	bitmap_set_range(cache->lru_bitmap, 0, group_count);
	return 0;
}

error_t ext2_group_get(struct ext2_context_s *ctx, uint_t grp_id, struct ext2_group_s **ptr)
{
	struct thread_s *this;
	struct ext2_group_cache_s *cache;
	struct ext2_group_s *group;
	uint_t index;
	sint_t victim;
	bool_t isHit;
	error_t err;
 
	cache  = ctx->group_cache;

	while(1)
	{
		index = grp_id % cache->count;
		group = &cache->tbl[index];
		spin_lock((slock_t*)&group->info.g_lock);

		isHit = ((group->info.g_id == grp_id) && (group->info.g_flags & EXT2_GF_VALID));

		if(isHit)
		{
			if(group->info.g_id & EXT2_GF_BUSY)
			{
				spinlock_lock(&cache->lock);

				this = current_thread;
	
				printk(INFO, "INFO: %s: cpu %d, task %d, thread %d, going to wait for grp %d\n",
				       __FUNCTION__,
				       cpu_get_id(),
				       this->task->pid,
				       this->info.order,
				       grp_id);
	
				wait_on(&cache->wait_queue, WAIT_LAST);
				spinlock_unlock(&cache->lock);
				spin_unlock((slock_t*)&group->info.g_lock);
				sched_sleep(this);
				continue;
			}
      
			spinlock_lock(&cache->lock);
			bitmap_clear(cache->lru_bitmap, grp_id);
			spinlock_unlock(&cache->lock);

			group->info.g_flags |= EXT2_GF_BUSY;
			spin_unlock((slock_t*)&group->info.g_lock);
			*ptr = group;
			return 0;
		}
    
		spin_unlock((slock_t*)&group->info.g_lock);

		victim = bitmap_ffs(cache->lru_bitmap, sizeof(cache->lru_bitmap));
    
		if(victim == -1)
		{
			spinlock_lock(&cache->lock);
			bitmap_set_range(cache->lru_bitmap, 1, MIN(ctx->group_count, cache->count));
			spinlock_unlock(&cache->lock);

			victim = 0;
		}

		group = &cache->tbl[victim];
		spin_lock((slock_t*)&group->info.g_lock);
		group->info.g_flags |= EXT2_GF_BUSY;
		spin_unlock((slock_t*)&group->info.g_lock);

		if(group->info.g_flags & EXT2_GF_DIRTY)
			ext2_group_sync(ctx,group);

		group->info.g_id = grp_id;

		err = ext2_group_load(ctx,group);

		spin_lock((slock_t*)&group->info.g_lock);

		group->info.g_flags = (err) ? 0 : EXT2_GF_VALID | EXT2_GF_BUSY;

		spinlock_lock(&cache->lock);
		wakeup_all(&cache->wait_queue);
		spinlock_unlock(&cache->lock);

		spin_unlock((slock_t*)&group->info.g_lock);

		*ptr = group;
		return err;
	}
  
	return 0;
}

error_t ext2_group_put(struct ext2_context_s *ctx, struct ext2_group_s *ptr, uint_t flags)
{
	struct ext2_group_cache_s *cache;

	cache = ctx->group_cache;

	spin_lock((slock_t*)&ptr->info.g_lock);

	if(flags & EXT2_GF_DIRTY)
		ptr->info.g_flags |= EXT2_GF_DIRTY;
  
	ptr->info.g_flags &= ~EXT2_GF_BUSY;

	spinlock_lock(&cache->lock);
	bitmap_clear(cache->lru_bitmap, ptr->info.g_id);
	spinlock_unlock(&cache->lock);  

	spin_unlock((slock_t*)&ptr->info.g_lock);
	return 0;
}


error_t ext2_group_cache_destroy(struct ext2_context_s *ctx)
{
	kmem_req_t req;
	struct ext2_group_cache_s *cache;
	uint_t i, count;
	error_t err;

	cache = ctx->group_cache;
	spinlock_destroy(&cache->lock);
	wait_queue_destroy(&cache->wait_queue);
  
	count = MIN(ctx->group_count, cache->count);

	for(i = 0; i < count; i++)
	{
		err = ext2_group_destroy(ctx,&cache->tbl[i]);

		if(err) 
		{
			printk(WARNING, "WARNING: %s: failed to destroy group, idx %d, id %d\n",
			       __FUNCTION__, i , cache->tbl[i].info.g_id);
		}
	}

	req.type = KMEM_PAGE;
	req.ptr  = ctx->group_cache_pg;
	kmem_free(&req);
	return 0;
}

error_t ext2_group_bfree(struct ext2_context_s *ctx, 
			 struct ext2_group_s *group,
			 uint_t blkid)
{
	struct ext2_blk_req_s req;
	bool_t state;
	uint_t index;
	error_t err;

	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = group->info.g_block_bitmap;
  
	if((err = ext2_blk_get(&req)) != 0)
		return EIO;

	index = blkid % ctx->blks_per_grp;
	state = bitmap_state(req.ptr, index);
  
	if(state == 0)
	{
		printk(WARNING, "WARNING: %s: inconsistent blk (#%d) group (#%d), try to run fsck, dev %s\n",
		       __FUNCTION__,
		       blkid,
		       group->info.g_id,
		       ctx->dev->name);
	}

	bitmap_clear(req.ptr, index);

	if(index < group->info.g_next_blk)
		group->info.g_next_blk = index;
 
	group->info.g_free_blocks_count ++;
	ext2_blk_put(&req);
	return 0;
}

error_t ext2_bfree(struct ext2_context_s *ctx, uint_t blkid)
{
	struct ext2_group_s *group;
	uint_t grp_id;
	error_t err;

	grp_id = blkid / ctx->blks_per_grp;

	if((err = ext2_group_get(ctx, grp_id, &group)) != 0)
		return err;

	err = ext2_group_bfree(ctx, group, blkid);

	ext2_group_put(ctx, group, (err == 0) ? EXT2_GF_DIRTY : 0);

	if(err == 0)
		cpu_atomic_inc(&ctx->sb->s_free_blocks_count);

	return err;
}

error_t ext2_group_ifree(struct ext2_context_s *ctx, 
			 struct ext2_group_s *group,
			 uint_t ino)
{
	struct ext2_blk_req_s req;
	bool_t state;
	uint_t index;
	error_t err;

	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = group->info.g_inode_bitmap;
  
	if((err = ext2_blk_get(&req)) != 0)
		return EIO;

	index = (ino - 1) % ctx->inodes_per_grp;
	state = bitmap_state(req.ptr, index);
  
	if(state == 0)
	{
		printk(WARNING, "WARNING: %s: inconsistent ino (#%d) group (#%d), try to run fsck, dev %s\n",
		       __FUNCTION__,
		       ino,
		       group->info.g_id,
		       ctx->dev->name);
	}

	bitmap_clear(req.ptr, index);

	if(ino < group->info.g_next_ino)
		group->info.g_next_blk = index;
 
	group->info.g_free_inodes_count ++;
	ext2_blk_put(&req);
	return 0;
}

error_t ext2_ifree(struct ext2_context_s *ctx, uint_t ino)
{
	struct ext2_group_s *group;
	uint_t grp_id;
	error_t err;
 
	grp_id = (ino - 1) / ctx->inodes_per_grp;

	if((err = ext2_group_get(ctx, grp_id, &group)) != 0)
		return err;

	err = ext2_group_ifree(ctx, group, ino);

	ext2_group_put(ctx, group, (err == 0) ? EXT2_GF_DIRTY : 0);

	if(err == 0)
		cpu_atomic_inc(&ctx->sb->s_free_inodes_count);

	return err;
}

error_t ext2_group_balloc(struct ext2_context_s *ctx, 
			  struct ext2_group_s *group,
			  uint_t *blkid)
{
	struct ext2_blk_req_s req;
	sint_t index;
	error_t err;

	if(group->info.g_next_blk == ctx->blks_per_grp)
		group->info.g_next_blk = 0;
  
	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = group->info.g_block_bitmap;
  
	if((err = ext2_blk_get(&req)) != 0)
		return EIO;

	index = bitmap_ffc2(req.ptr, group->info.g_next_blk, ctx->blk_size);
  
	if(index == -1)
	{
		group->info.g_free_blocks_count = 0;
    
		printk(WARNING, "WARNING: %s: inconsistent blk group, try to run fsck, dev %s\n",
		       __FUNCTION__, 
		       ctx->dev->name);
    
		ext2_blk_put(&req);
		return -1;
	}

	bitmap_set(req.ptr, index);
	group->info.g_next_blk = index + 1;
	group->info.g_free_blocks_count --;
	ext2_blk_put(&req);
	*blkid = (group->info.g_id * ctx->blks_per_grp) + index;
	return 0;
}

error_t ext2_do_balloc(struct ext2_context_s *ctx, uint_t grp_id, uint_t *blkid)
{
	struct ext2_group_s *group;
	error_t err;
	bool_t isAtomic;
 
	if((err = ext2_group_get(ctx, grp_id, &group)) != 0)
		return err;

	err = -1;

	if(group->info.g_free_blocks_count > 0)
	{
		err = ext2_group_balloc(ctx, group, blkid);
	}
	else
	{
		isAtomic = false;

		while((group->info.g_free_blocks_count == 0) && 
		      (grp_id == ctx->last_free_blk_grp) && (isAtomic == false))
		{
			isAtomic = cpu_atomic_cas(&ctx->last_free_blk_grp, 
						  ctx->last_free_blk_grp, 
						  (grp_id + 1) % ctx->group_count);
		}
	}

	ext2_group_put(ctx, group, (err == 0) ? EXT2_GF_DIRTY : 0);

	if(err == 0)
		cpu_atomic_dec(&ctx->sb->s_free_blocks_count);

	return err;
}

error_t ext2_balloc(struct ext2_context_s *ctx, uint_t grp_id, uint_t *blkid)
{
	error_t err;
	uint_t try_cntr;
	uint_t index;

	if(ctx->sb->s_free_blocks_count == 0)
		return ENOSPC;
  
	err = ext2_do_balloc(ctx, grp_id, blkid);
  
	if(err >= 0) return err;

	index    = ctx->last_free_blk_grp;
	try_cntr = 0;

	while((ctx->sb->s_free_blocks_count > 0) && (try_cntr < 2*ctx->group_count))
	{
		err = ext2_do_balloc(ctx, index, blkid);
    
		if(err >= 0) return err;

		index = (index + 1) % ctx->group_count;
		try_cntr ++;
	}

	return ENOSPC;
}

error_t ext2_group_ialloc(struct ext2_context_s *ctx, 
			  struct ext2_group_s *group,
			  bool_t isDir,
			  uint_t *ino)
{
	struct ext2_blk_req_s req;
	sint_t index;
	error_t err;
	uint_t inodes_count;

	if(group->info.g_next_ino == ctx->inodes_per_grp)
		group->info.g_next_ino = 0;
  
	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = group->info.g_inode_bitmap;
  
	if((err = ext2_blk_get(&req)) != 0)
		return EIO;
  
	inodes_count = ARROUND_UP(ctx->inodes_per_grp, 8);
	index        = bitmap_ffc2(req.ptr, group->info.g_next_ino, inodes_count >> 3);
  
	if(index == -1)
	{
		group->info.g_free_inodes_count = 0;
    
		printk(WARNING, "WARNING: %s: inconsistent ino group, try to run fsck, dev %s\n",
		       __FUNCTION__, 
		       ctx->dev->name);
    
		ext2_blk_put(&req);
		return -1;
	}

	bitmap_set(req.ptr, index);
	group->info.g_next_ino = index + 1;
	group->info.g_free_inodes_count --;

	if(isDir)
		group->info.g_used_dirs_count ++;

	ext2_blk_put(&req);
	*ino = (group->info.g_id * ctx->inodes_per_grp) + index + 1;
	return 0;
}

error_t ext2_do_ialloc(struct ext2_context_s *ctx, uint_t grp_id, bool_t isDir, uint_t *ino)
{
	struct ext2_group_s *group;
	error_t err;
	bool_t isAtomic;
 
	if((err = ext2_group_get(ctx, grp_id, &group)) != 0)
		return err;

	err = -1;

	if(group->info.g_free_inodes_count > 0)
	{
		err = ext2_group_ialloc(ctx, group, isDir, ino);
	}
	else
	{
		isAtomic = false;

		while((group->info.g_free_inodes_count == 0) && 
		      (grp_id == ctx->last_free_ino_grp)     && 
		      (isAtomic == false))
		{
			isAtomic = cpu_atomic_cas(&ctx->last_free_ino_grp, 
						  ctx->last_free_ino_grp, 
						  (grp_id + 1) % ctx->group_count);
		}
	}

	ext2_group_put(ctx, group, (err == 0) ? EXT2_GF_DIRTY : 0);
  
	if(err == 0)
		cpu_atomic_dec(&ctx->sb->s_free_inodes_count);

	return err;
}

error_t ext2_ialloc(struct ext2_context_s *ctx, uint_t grp_id, bool_t isDir, uint_t *ino)
{
	error_t err;
	uint_t try_cntr;
	uint_t index;

	if(ctx->sb->s_free_inodes_count == 0)
		return ENOSPC;
  
	err = ext2_do_ialloc(ctx, grp_id, isDir, ino);
  
	if(err >= 0) return err;

	index    = ctx->last_free_ino_grp;
	try_cntr = 0;

	while((ctx->sb->s_free_inodes_count > 0) && (try_cntr < 2*ctx->group_count))
	{
		err = ext2_do_ialloc(ctx, index, isDir, ino);
    
		if(err >= 0) return err;

		index = (index + 1) % ctx->group_count;
		try_cntr ++;
	}

	return ENOSPC;
}

error_t ext2_node_io(struct ext2_node_s *parent, struct ext2_node_s *node, uint_t flags)
{
	struct ext2_blk_req_s req;
	struct ext2_context_s *ctx;
	struct ext2_group_s *group;
	uint_t grp_id;
	uint_t index;
	error_t err;

#if CONFIG_EXT2_DEBUG
	ext2_inode_print(&node->inode);
#endif

	ctx       = node->ctx;
	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	grp_id    = (node->ino - 1) / ctx->inodes_per_grp;
	index     = (node->ino - 1) % ctx->inodes_per_grp;
  
	if(node->ino_tbl_blkid == 0)
	{
		if(grp_id == ((parent->ino - 1) / ctx->inodes_per_grp))
			node->ino_tbl_blkid = parent->ino_tbl_blkid;

		if(node->ino_tbl_blkid == 0)
		{
			if((err = ext2_group_get(ctx, grp_id, &group)) != 0)
				return err;
      
			node->ino_tbl_blkid = group->info.g_inode_table;

			ext2_group_put(ctx, group, 0);
		}
	}

	req.blkid = node->ino_tbl_blkid + (index / ctx->inodes_per_blk);

	err = ext2_blk_get(&req);

	if(err) return err;

	if(flags & EXT2_GF_READ)
	{
		memcpy(&node->inode,
		       (void*)((uint_t)req.ptr + ((index % ctx->inodes_per_blk) * ctx->inode_size)),
		       sizeof(node->inode));
	}
	else
	{
		memcpy((void*)((uint_t)req.ptr + ((index % ctx->inodes_per_blk) * ctx->inode_size)),
		       &node->inode,
		       sizeof(node->inode));
	}

	ext2_blk_put(&req);
  
	return 0;
}


void ext2_sb_print(struct ext2_super_blk_s *sb)
{
	printk(INFO, "Revision level %d\n", sb->s_rev_level);
	printk(INFO, "Total number of inodes %d\n", sb->s_inodes_count);
	printk(INFO, "Total number of blocks %d\n", sb->s_blocks_count);
	printk(INFO, "Total number of blocks reserved %d\n", sb->s_r_blocks_count);
	printk(INFO, "Total number of free blocks %d\n", sb->s_free_blocks_count);
	printk(INFO, "Total number of free inodes %d\n", sb->s_free_inodes_count);
	printk(INFO, "Log block size %d\n", sb->s_log_block_size);
	printk(INFO, "Blocks per group %d\n", sb->s_blocks_per_group);
	printk(INFO, "Inodes per group %d\n", sb->s_inodes_per_group);
	printk(INFO, "Inode size %d\n", sb->s_inode_size);
	printk(INFO, "Fist inode %d\n", sb->s_first_ino);
	printk(INFO, "Inode size %d\n", sb->s_inode_size);
	printk(INFO, "Block group nr %d\n", sb->s_block_group_nr);
	printk(INFO, "Prealloc blocks %d\n", sb->s_prealloc_blocks);
}

void ext2_block_group_print(struct ext2_group_desc_s *ptr, uint_t index)
{
	printk(INFO, "INFO: found group %d\n", index);
	printk(INFO, "INFO: id of first blk of blk bitmap : %d\n", ptr->g_block_bitmap);
	printk(INFO, "INFO: id of first blk of ino bitmap : %d\n", ptr->g_inode_bitmap);
	printk(INFO, "INFO: id of first blk of inode_tbl  : %d\n", ptr->g_inode_table);
	printk(INFO, "INFO: total number of free blk %d\n", ptr->g_free_blocks_count);
	printk(INFO, "INFO: total number of free ino %d\n", ptr->g_free_inodes_count);
	printk(INFO, "INFO: total number of dir blk %d\n", ptr->g_used_dirs_count);
	printk(INFO, "INFO: flags 0x%x, next_ino %d, next_blk %d, lock %d, id %d\n", 
	       ptr->g_flags, 
	       ptr->g_next_ino,
	       ptr->g_next_blk,
	       ptr->g_lock,
	       ptr->g_id);
}

void ext2_inode_print(struct ext2_inode_s *inode)
{
	uint_t i;

	printk(INFO, "INFO: mode %x, uid %x, size %d, atime %d, ctime %d, dtime %d, gid %d\n",
	       inode->i_mode,
	       inode->i_uid,
	       inode->i_size,
	       inode->i_atime,
	       inode->i_ctime,
	       inode->i_mtime,
	       inode->i_gid);

	printk(INFO, "INFO: links %d, blocks %d, flags %x, generation %d, f_acl %x, d_acl %x\n",
	       inode->i_links,
	       inode->i_blocks,
	       inode->i_flags,
	       inode->i_generation,
	       inode->i_file_acl,
	       inode->i_dir_acl);

	printk(INFO, "INFO: blk tbl [");

	for(i = 0; i < 15; i++)
	{
		printk(INFO, "%d, ", inode->i_block[i]);
	}

	printk(INFO, "\b\b]\n");
}

void ext2_dirEntry_print(struct ext2_dirEntry_s *entry)
{
#define MAX_LEN 32
	char name[MAX_LEN];
 
	memset(&name[0], 0, sizeof(name));

	if(entry->d_name_len != 0)
		strncpy(&name[0],(char*)&entry->d_name[0], MIN(entry->d_name_len, (MAX_LEN -1)));

	printk(INFO, "INFO: [dirEntry] ino %d, rec_len %d, name_len %d, f_type %x, name [%s]\n",
	       entry->d_inode,
	       entry->d_rec_len,
	       entry->d_name_len,
	       entry->d_file_type,
	       &name[0]);
}
