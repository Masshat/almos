/*
 * ext2/ext2_mapper.c - ext2 mapper related operations
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

#include <stdint.h>
#include <page.h>
#include <ppm.h>
#include <pmm.h>
#include <vfs.h>
#include <thread.h>
#include <driver.h>
#include <blkio.h>
#include <mapper.h>
#include <list.h>
#include <ext2-private.h>

static inline error_t ext2_pgio_meta(struct ext2_context_s* ctx, 
				     struct page_s *page, 
				     uint_t flags) 
{
	struct blkio_s *blkio;
	struct device_s* dev;
	uint_t sectors_per_page;
	uint_t lba_start;
	uint_t vaddr;
	error_t err;

	dev              = ctx->dev;
	sectors_per_page = PMM_PAGE_SIZE / ctx->bytes_per_sector;
	lba_start        = page->index * sectors_per_page;
  
	if((err = blkio_init(dev, page, 1)) != 0)
		return err;

	vaddr = (uint_t) ppm_page2addr(page);

	blkio                 = list_first(&page->root, struct blkio_s, b_list);
	blkio->b_dev_rq.src   = (void*)lba_start;
	blkio->b_dev_rq.dst   = (void*)vaddr;
	blkio->b_dev_rq.count = sectors_per_page;

	err = blkio_sync(page,flags);
	blkio_destroy(page);
	return err;
}

error_t ext2_node_new(struct ext2_context_s *ctx, 
		      uint_t blk_size_log,
		      uint_t grp_id,
		      bool_t isLeaf,
		      uint_t *node_blk)
{
	struct ext2_blk_req_s req;
	void *ptr;
	error_t err;
  
	err = ext2_balloc(ctx, grp_id, node_blk);
  
	if((err) || (isLeaf))
		return err;
  
	req.ctx   = ctx;
	req.flags = EXT2_GF_SYNC;
	req.blkid = *node_blk;

	err = ext2_blk_get(&req);

	if(err) goto fail_node;

	ptr = req.ptr;
	memset(ptr, 0, 1 << blk_size_log);
	req.flags = EXT2_GF_DIRTY;
	ext2_blk_put(&req);
	return 0;

fail_node:
	ext2_bfree(ctx, *node_blk);
	return err;
}

error_t ext2_node_blks_locate(struct ext2_context_s *ctx, 
			      struct ext2_node_s *node,
			      uint_t blk_start,
			      uint_t blk_size_log,
			      uint_t *blks_tbl,
			      bool_t *blks_state_tbl)
{
	struct ext2_blk_req_s req;
	uint_t current_blk_index;
	uint_t current_blk;
	uint_t parent_blk;
	uint_t parent_val;
	uint_t blk_index;
	uint_t level;
	bool_t isNew;
	bool_t hasWrLock;
	uint_t grp_id;
	uint_t i,j;
	error_t err;

	grp_id    = (node->ino - 1) / ctx->inodes_per_grp;
	hasWrLock = false;
	i         = 0;

	rwlock_rdlock(&node->rwlock);

	while(i < ctx->blks_per_page)
	{
		blk_index = blk_start + i;
		level     = ext2_get_indirect_level(blk_index, blk_size_log);
    
		if(level < 0)
		{
			rwlock_unlock(&node->rwlock);
			return ERANGE;
		}

		if(level == 0)
		{
			if(node->inode.i_block[blk_index] != 0)
			{
				blks_tbl[i] = node->inode.i_block[blk_index];
				blks_state_tbl[i] = false;
				i++;
				continue;
			}
      
			if(hasWrLock == false)
			{
				hasWrLock = true;
				rwlock_unlock(&node->rwlock);
				rwlock_wrlock(&node->rwlock);
				continue;
			}

			current_blk = -1;
			err = ext2_balloc(ctx, grp_id, &current_blk);
      
			if(err)
			{
				rwlock_unlock(&node->rwlock);
				return err;
			}

			node->inode.i_block[blk_index] = current_blk;
			blks_tbl[i] = current_blk;
			blks_state_tbl[i] = true;
			i++;
			continue;
		}

		current_blk       = node->inode.i_block[11+level];
		parent_blk        = current_blk;
		req.ctx           = ctx;
		j                 = level;
		isNew             = false;
		current_blk_index = 0;

		blk_index -= 12;

		if(level > 1)
			blk_index  -= 1 << (level-1)*(blk_size_log - 2);

		while((current_blk == 0) || (j > 0))
		{
			if(current_blk == 0)
			{
				if(hasWrLock == false)
				{
					hasWrLock = true;
					rwlock_unlock(&node->rwlock);
					rwlock_wrlock(&node->rwlock);
					goto retry;
				}

				err = ext2_node_new(ctx,
						    blk_size_log,
						    grp_id,
						    ((parent_blk != 0) && (j == 0)), 
						    &current_blk);
	
				if(err)
				{
					rwlock_unlock(&node->rwlock);
					return err;
				}

				node->inode.i_blocks ++;
				node->flags |= EXT2_GF_DIRTY;

				if(parent_blk == 0)
				{
					node->inode.i_block[11+level] = current_blk;
					parent_blk = current_blk;
					continue;
				}
      
				req.flags = EXT2_GF_SYNC;
				req.blkid = parent_blk;
				err       = ext2_blk_get(&req);
	
				if(err)
				{
					rwlock_unlock(&node->rwlock);
					return err;
				}

				*((uint32_t*)req.ptr + current_blk_index) = current_blk;

				req.flags = EXT2_GF_DIRTY;
				ext2_blk_put(&req);

				if(j == 0)
				{
					isNew = true;
					break;
				}

				parent_blk  = current_blk;
				current_blk = 0;
				j--;
				continue;
			}

			req.flags = EXT2_GF_SYNC;
			req.blkid = current_blk;
			err       = ext2_blk_get(&req);
      
			if(err)
			{
				rwlock_unlock(&node->rwlock);
				return err;
			}

			parent_blk        = current_blk;
			parent_val        = 1 << j*(blk_size_log - 2);
			current_blk_index = (j > 1) ? (blk_index / parent_val) : (blk_index % parent_val);
			current_blk       = *((uint32_t*)req.ptr + current_blk_index);

			ext2_blk_put(&req);
			j--;
		}
    
		blks_tbl[i]       = current_blk;
		blks_state_tbl[i] = isNew;
		i++;

	retry:
		; /* skip current loop */
	}

	rwlock_unlock(&node->rwlock);
	return 0;
}

error_t ext2_pgio_map(struct ext2_file_s *file,
		      struct page_s *page,
		      uint_t flags)
{
	struct ext2_context_s *ctx;
	struct ext2_node_s *node;
	struct blkio_s *blkio;
	struct slist_entry *iter;
	uint8_t *ptr;
	uint_t blk_start;
	uint_t blk_log;
	uint_t i;
	error_t err;
	uint_t sectors_per_blk;

	node = file->node;
	ctx  = node->ctx;

	uint_t blks_tbl[ctx->blks_per_page];
	bool_t blks_isNew_tbl[ctx->blks_per_page];

	ptr             = ppm_page2addr(page);
	sectors_per_blk = ctx->blk_size / ctx->bytes_per_sector;
	blk_start       = page->index * ctx->blks_per_page;
	blk_log         = 10 + ctx->sb->s_log_block_size;

	for(i = 0; i < ctx->blks_per_page; i++)
	{
		blks_tbl[i] = 0;
		blks_isNew_tbl[i] = false;
	}

	err = ext2_node_blks_locate(ctx, 
				    node, 
				    blk_start,
				    blk_log,
				    &blks_tbl[0],
				    &blks_isNew_tbl[0]);  
	if(err) return err;

	err = blkio_init(ctx->dev, page, ctx->blks_per_page);

	if(err) return err;
  
	i = 0;

	list_foreach(&page->root, iter)
	{
		blkio = list_element(iter, struct blkio_s, b_list);
     
		if(blks_isNew_tbl[i] == true)
		{
			node->inode.i_blocks ++;
			node->flags |= EXT2_GF_DIRTY;

			if(flags & BLKIO_RD)
			{
				memset(ptr, 0, ctx->blk_size);
				blkio_set_initial(blkio);
			}
		}

		blkio->b_dev_rq.src   = (void*) (blks_tbl[i] * sectors_per_blk);
		blkio->b_dev_rq.dst   = (void*) ptr;
		blkio->b_dev_rq.count = sectors_per_blk;
		ptr                  += ctx->blk_size;
		i ++;
	}

	return err;
}

MAPPER_READ_PAGE(ext2_node_read_page) 
{
	struct ext2_file_s file_info;
	struct mapper_s *mapper;
	uint_t op_flags;
	error_t err;

	op_flags = (flags & MAPPER_SYNC_OP) ? BLKIO_RD | BLKIO_SYNC : BLKIO_RD;

	mapper = page->mapper;

	if(mapper->m_node == NULL)
		return ext2_pgio_meta(mapper->m_data, page, op_flags);

	file_info.node = mapper->m_node->n_pv;

	assert(!(PAGE_IS(page, PG_BUFFER)));

	err = ext2_pgio_map(&file_info, page, op_flags);

	if(err) return err;

	err = blkio_sync(page,op_flags);
	return err;
}

MAPPER_WRITE_PAGE(ext2_node_write_page) 
{
	struct mapper_s *mapper;
	uint_t op_flags;
	error_t err;

	op_flags = (flags & MAPPER_SYNC_OP) ? BLKIO_SYNC : 0;
	mapper   = page->mapper;

	if(mapper->m_node == NULL)
		return ext2_pgio_meta(mapper->m_data, page, op_flags);

	assert(PAGE_IS(page, PG_BUFFER));

	err = blkio_sync(page,op_flags);
	return err;
}

const struct mapper_op_s ext2_node_mapper_op =
{
	.writepage        = ext2_node_write_page,
	.readpage         = ext2_node_read_page,
	.sync_page        = mapper_default_sync_page,
	.set_page_dirty   = mapper_default_set_page_dirty,
	.clear_page_dirty = mapper_default_clear_page_dirty,
	.releasepage      = mapper_default_release_page,
};
