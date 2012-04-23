/*
 * ext2/ext2_context.c - ext2 context related operations
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

#include <kdmsg.h>
#include <device.h>
#include <driver.h>
#include <kmem.h>
#include <rwlock.h>
#include <string.h>
#include <page.h>
#include <pmm.h>
#include <cluster.h>
#include <thread.h>
#include <mapper.h>
#include <vfs.h>

#include <ext2.h>
#include <ext2-private.h>
 
error_t ext2_context_init(struct ext2_context_s *ctx, struct device_s *dev, struct mapper_s *mapper)
{
	size_t blk_size;
	dev_params_t params;
	struct ext2_super_blk_s *sb;
	struct page_s *page;
	error_t err;

	if((err = dev->op.dev.get_params(dev, &params)) != 0)
		return err;

	if(params.sector_size > PMM_PAGE_SIZE)
	{
		printk(ERROR, "ERROR: %s: not supported sector size %d\n", 
		       __FUNCTION__, params.sector_size);
		return -1;
	}
  
	blk_size = params.sector_size;

	/* must be set as soos as possible, used by ext2_mapper */
	ctx->dev              = dev;
	ctx->mapper           = mapper;
	ctx->bytes_per_sector = blk_size; 
	/* -------------------------------------------------- */

	page = mapper_get_page(mapper, 0, MAPPER_SYNC_OP, NULL);
  
	if(page == NULL)
	{
		printk(ERROR, "ERROR: %s: I/O error, could not read super block from the device.\n", __FUNCTION__);
		return -1;
	}

	sb = (struct ext2_super_blk_s *)(((uint_t)ppm_page2addr(page)) + 1024);
  
	if(sb->s_magic != 0xEF53)
	{
		printk(ERROR, "ERROR: %s: unexpected file system format\n", __FUNCTION__);
		return -1;
	}

	if(sb->s_state != EXT2_VALID_FS)
	{
		printk(WARNING, "WARNING: %s: File system is not in valid state [ %d ]\n", 
		       __FUNCTION__, sb->s_state);
    
		if(sb->s_errors != EXT2_ERRORS_CONTINUE)
		{
			printk(ERROR, "ERROR: %s: invalid state of file system, s_errors [%d]\n", 
			       __FUNCTION__, sb->s_errors);

			return -1;
		}
	}

	blk_size = 1024 << sb->s_log_block_size;
  
	if((blk_size > PMM_PAGE_SIZE) || (ctx->bytes_per_sector > blk_size))
	{
		printk(ERROR, "ERROR: %s: unexpected block size %d, sector size %d, page size %d\n",
		       __FUNCTION__, 
		       blk_size, 
		       ctx->bytes_per_sector,
		       PMM_PAGE_SIZE);

		return -1;
	}

	PAGE_SET(page,PG_PINNED);
	page_refcount_up(page);

	spinlock_init(&ctx->lock, "Ext2fs ctx");

	ctx->sb                = sb;
	ctx->last_free_blk_grp = -1;
	ctx->last_free_ino_grp = -1;
	ctx->flags             = 0;
	ctx->blk_size          = blk_size;
	ctx->blks_per_page     = PMM_PAGE_SIZE / blk_size;
	ctx->blks_per_grp      = sb->s_blocks_per_group;
	ctx->inode_size        = sb->s_inode_size;
	ctx->inodes_per_blk    = ctx->blk_size / sb->s_inode_size; 
	ctx->inodes_per_grp    = sb->s_inodes_per_group;
	ctx->group_count       = sb->s_blocks_count / sb->s_blocks_per_group;

	if((ctx->group_count * sb->s_blocks_per_group) != sb->s_blocks_count)
		ctx->group_count ++;
  
	if((err = ext2_group_cache_init(ctx)) != 0)
		return err;
  
	/* Informatif traces */
#if CONFIG_EXT2_DEBUG
	ext2_sb_print(sb);
  
	uint_t i;
	for(i=0; i < ctx->group_cache->count; i++)
	{
		ext2_block_group_print(&ctx->group_cache->tbl[i].info, i);
	}
#endif

	return 0;
}

VFS_CREATE_CONTEXT(ext2_create_context)
{
	kmem_req_t req;
	struct ext2_context_s *ctx;
	struct mapper_s *mapper;
	error_t err;
  
	ext2_dmsg(2, "%s: started\n", __FUNCTION__);

	req.type  = KMEM_EXT2_CTX;
	req.size  = sizeof(*ctx);
	req.flags = AF_KERNEL;
	err       = ENOMEM;

	if ((ctx = kmem_alloc(&req)) == NULL)
		return err;
  
	req.type  = KMEM_MAPPER;
	req.size  = sizeof(*mapper);
  
	mapper = kmem_alloc(&req);

	if(mapper == NULL)
		goto fail_mapper;
  
	mapper->m_node = NULL;
	mapper->m_ops  = &ext2_node_mapper_op;
	mapper->m_data = ctx;

	err = ext2_context_init(ctx,context->ctx_dev,mapper);

	if(err)
		goto fail_init;
  
	context->ctx_type    = VFS_EXT2_TYPE;
	context->ctx_op      = (struct vfs_context_op_s *) &ext2_ctx_op;
	context->ctx_node_op = (struct vfs_node_op_s *)    &ext2_n_op;
	context->ctx_file_op = (struct vfs_file_op_s *)    &ext2_f_op;
	context->ctx_pv      = (void *) ctx;

	return 0;

fail_init:
	req.type = KMEM_MAPPER;
	req.ptr  = mapper;
	kmem_free(&req);

fail_mapper:
	req.type = KMEM_EXT2_CTX;
	req.ptr  = ctx;
	kmem_free(&req);

	return err;
}

VFS_DESTROY_CONTEXT(ext2_destroy_context)
{
	kmem_req_t req;
	struct ext2_context_s *ctx;
	error_t err;
  
	ctx = context->ctx_pv;
  
	if(ctx == NULL)
	{
		printk(ERROR, "ERROR: %s: unexpected ctx\n", __FUNCTION__);
		return EINVAL;
	}

	if(ctx->flags & EXT2_GF_DIRTY)
	{
		printk(ERROR, "ERROR: %s: ctx is dirty, flags 0x%x\n", 
		       __FUNCTION__, 
		       ctx->flags);

		return EINVAL;
	}

	err = ext2_group_cache_destroy(ctx);

	if(err)
		printk(WARNING, "WARNING: %s: failed to destroy grp-cache", __FUNCTION__);

	mapper_destroy(ctx->mapper, true);
	req.type = KMEM_MAPPER;
	req.ptr  = ctx->mapper;
	kmem_free(&req);

	spinlock_destroy(&ctx->lock);
  
	req.type = KMEM_EXT2_CTX;
	req.ptr  = ctx;
	kmem_free(&req);
	return 0;
}

VFS_READ_ROOT(ext2_read_root)
{
	struct ext2_node_s *n_info;
	error_t err;
	
	n_info      = root->n_pv;
	n_info->ino = EXT2_ROOT_INO;

	strcpy(root->n_name,"/");

	err = ext2_node_read(root,root);

	if(err) return err;

	if((n_info->inode.i_flags & EXT2_BTREE_FL)  ||
	   (n_info->inode.i_flags & EXT2_INDEX_FL)  ||
	   (n_info->inode.i_flags & EXT2_JOURNAL_DATA_FL))
	{
		printk(WARNING, "WARNING: only linked list directories are supported\n");
		return ENOSYS;
	}

	return 0;
}


VFS_WRITE_ROOT(ext2_write_root)
{
	return ENOSYS;
}

KMEM_OBJATTR_INIT(ext2_kmem_context_init)
{
	attr->type    = KMEM_EXT2_CTX;
	attr->name    = "KCM EXT2 CTX";
	attr->size    = sizeof(struct ext2_context_s);
	attr->aligne  = 0;
	attr->min     = CONFIG_EXT2_CTX_MIN;
	attr->max     = CONFIG_EXT2_CTX_MAX;
	attr->ctor    = NULL;
	attr->dtor    = NULL;
	return 0;
}

const struct vfs_context_op_s ext2_ctx_op =
{
	.create     = ext2_create_context,
	.destroy    = ext2_destroy_context,
	.read_root  = ext2_read_root,
	.write_root = ext2_write_root
};
