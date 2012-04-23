/*
 * ext2/ext2_node.c - ext2 node related operations
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
#include <thread.h>
#include <task.h>
#include <page.h>
#include <ppm.h>
#include <vfs.h>

#include <ext2.h>
#include <ext2-private.h>

void ext2_to_vfs_access_rights(struct vfs_node_s *node, struct ext2_inode_s *inode)
{
	if(inode->i_mode & EXT2_S_IRUSR)
		node->n_mode |= VFS_IRUSR;
  
	if(inode->i_mode & EXT2_S_IWUSR)
		node->n_mode |= VFS_IWUSR;

	if(inode->i_mode & EXT2_S_IXUSR)
		node->n_mode |= VFS_IXUSR;

	if(inode->i_mode & EXT2_S_IRGRP)
		node->n_mode |= VFS_IRGRP;
  
	if(inode->i_mode & EXT2_S_IWGRP)
		node->n_mode |= VFS_IWGRP;

	if(inode->i_mode & EXT2_S_IXGRP)
		node->n_mode |= VFS_IXGRP;

	if(inode->i_mode & EXT2_S_IROTH)
		node->n_mode |= VFS_IROTH;
  
	if(inode->i_mode & EXT2_S_IWOTH)
		node->n_mode |= VFS_IWOTH;

	if(inode->i_mode & EXT2_S_IXOTH)
		node->n_mode |= VFS_IXOTH;
}

void vfs_to_ext2_access_rights(struct vfs_node_s *node, struct ext2_inode_s *inode)
{
	if(node->n_mode & VFS_IRUSR)
		inode->i_mode |= VFS_IRUSR;
  
	if(node->n_mode & VFS_IWUSR) 
		inode->i_mode |= VFS_IWUSR;

	if(node->n_mode & VFS_IXUSR)
		inode->i_mode |= VFS_IXUSR;

	if(node->n_mode & VFS_IRGRP)
		inode->i_mode |= VFS_IRGRP;
  
	if(node->n_mode & VFS_IWGRP)
		inode->i_mode |= VFS_IWGRP;

	if(node->n_mode & VFS_IXGRP)
		inode->i_mode |= VFS_IXGRP;

	if(node->n_mode & VFS_IROTH)
		inode->i_mode |= VFS_IROTH;
  
	if(node->n_mode & VFS_IWOTH)
		inode->i_mode |= VFS_IWOTH;

	if(node->n_mode & VFS_IXOTH)
		inode->i_mode |= VFS_IXOTH;
}

void vfs_to_ext2_access_mode(struct vfs_node_s *node, struct ext2_inode_s *inode)
{
  
}

static uint8_t ext2_ino2dir_mode_tbl[] = 
{
	EXT2_FT_FIFO,
	EXT2_FT_CHRDEV,
	EXT2_FT_DIR,
	EXT2_FT_BLKDEV,
	EXT2_FT_REGFILE,
	EXT2_FT_SYMLINK,
	EXT2_FT_SOCK,
	EXT2_FT_UNKNOWN
};

uint_t ext2_ino2dir_ftype_convert(uint_t i_mode)
{
	return ext2_ino2dir_mode_tbl[(i_mode >> EXT2_SFT_SHIFT) & EXT2_SFT_MASK];
}

void ext2_free_blks(struct ext2_context_s *ctx, uint32_t *blk_tbl, uint_t count)
{
	uint_t i;

	for(i = 0; i < count; i++)
	{
		if(blk_tbl[i] != 0)
			(void)ext2_bfree(ctx, blk_tbl[i]);
	}
}

void ext2_free_blks_rec(struct ext2_context_s *ctx,
			uint_t level,
			uint_t current_blk, 
			uint_t current_level)
{
	struct ext2_blk_req_s req;
	uint_t count = 16;
	uint32_t blk_tbl[count];
	uint_t i,j;
	error_t err;

	if(current_blk == 0)
		return;

	if(current_level == (level - 1))
	{
		for(i = 0; i < ctx->blk_size / (count << 2); i++)
		{
			req.ctx   = ctx;
			req.flags = EXT2_GF_SYNC;
			req.blkid = current_blk;
			err = ext2_blk_get(&req);

			if(err) return;

			for(j = 0; j < count; j++)
				blk_tbl[j] = *((uint32_t*)req.ptr + i*count + j);

			ext2_blk_put(&req);

			ext2_free_blks(ctx, &blk_tbl[0], count);
		}

		ext2_bfree(ctx, current_blk);
		return;
	}

	for(i = 0; i < ctx->blk_size / (count << 2); i++)
	{
		req.ctx   = ctx;
		req.flags = EXT2_GF_SYNC;
		req.blkid = current_blk;
		err = ext2_blk_get(&req);

		if(err) return;

		for(j = 0; j < count; j++)
			blk_tbl[j] = *((uint32_t*)req.ptr + i*count + j);

		ext2_blk_put(&req);

		for(j = 0; j < count; j++)
			ext2_free_blks_rec(ctx,level, blk_tbl[j], current_level + 1);
	}

	ext2_bfree(ctx, current_blk);
	return;
}


error_t ext2_node_free_blocks(struct vfs_node_s *node)
{
	struct ext2_blk_req_s req;
	struct ext2_inode_s *inode;
	struct ext2_node_s  *n_info;
	uint_t blkid;
	uint_t level,i;

	n_info    = node->n_pv;
	inode     = &n_info->inode;
	req.ctx   = n_info->ctx;
	req.flags = EXT2_GF_SYNC;

	rwlock_wrlock(&n_info->rwlock);

	for(i = 0; i < 12; i++)
	{
		blkid = inode->i_block[i];

		if(blkid != 0)
			ext2_bfree(n_info->ctx, blkid);
	}

	for(level = 1; level < 3; level++)
	{
		blkid = inode->i_block[11 + level];
		ext2_free_blks_rec(n_info->ctx, level, blkid, 0);
	}

	rwlock_unlock(&n_info->rwlock);
	return 0;
}

error_t ext2_dir_init(struct vfs_node_s *parent, struct vfs_node_s *node)
{
	struct mapper_s *mapper;
	struct page_s *page;
	struct ext2_dirEntry_s *entry;
	struct ext2_node_s *n_info;
	struct ext2_node_s *p_info;

	n_info = node->n_pv;
	p_info = parent->n_pv;
	mapper = node->n_mapper;
	
	page   = mapper_get_page(mapper, 0, MAPPER_SYNC_OP, NULL);

	if(page == NULL)
		return current_thread->info.errno;

	entry = ppm_page2addr(page);

	entry->d_inode     = n_info->ino;
	entry->d_rec_len   = ARROUND_UP(sizeof(*entry) + 1, 4);
	entry->d_name_len  = 1;
	entry->d_file_type = EXT2_FT_DIR;
	entry->d_name[0]   = '.';
	entry->d_name[1]   = '\0';

	entry = (struct ext2_dirEntry_s*)((uint8_t*)entry + entry->d_rec_len);
  
	entry->d_inode     = p_info->ino;
	entry->d_rec_len   = n_info->ctx->blk_size - (ARROUND_UP(sizeof(*entry) + 1, 4));
	entry->d_name_len  = 2;
	entry->d_file_type = EXT2_FT_DIR;
	entry->d_name[0]   = '.';
	entry->d_name[1]   = '.';
	entry->d_name[2]   = '\0';
  
	node->n_size = n_info->ctx->blk_size;
	node->n_links ++;
	return 0;
}

error_t ext2_node_link(struct vfs_node_s *parent, struct vfs_node_s *node)
{
	struct ext2_node_s *n_info;
	struct ext2_node_s *p_info;
	struct ext2_dirEntry_s *entry;
	struct mapper_s *mapper;
	struct page_s *page;
	uint8_t *ptr;
	uint_t pages_nr;
	uint_t offset;
	uint_t size,i;
	uint_t len;

	ext2_dmsg(2, "%s: +++ started, parent [%s], node [%s]\n", 
		  __FUNCTION__,
		  &parent->n_name[0],
		  &node->n_name[0]);

	pages_nr = parent->n_size / PMM_PAGE_SIZE + 2; 
	mapper   = parent->n_mapper;
	n_info   = node->n_pv;
	p_info   = parent->n_pv;
	len      = strlen(&node->n_name[0]);
	size     = ARROUND_UP(sizeof(*entry) + len, 4);

	for(i = 0; i < pages_nr; i++)
	{
		page = mapper_get_page(mapper, i, MAPPER_SYNC_OP, NULL);
    
		if(page == NULL)
			return current_thread->info.errno;

		ptr    = ppm_page2addr(page);
		offset = 0;

		page_lock(page);
   
		while(offset < PMM_PAGE_SIZE)
		{
			entry = (struct ext2_dirEntry_s*)(ptr + offset);
   
			if((entry->d_inode == 0) && ((entry->d_rec_len == 0) || (entry->d_rec_len >= size)))
				goto found;

			if(entry->d_rec_len >= (sizeof(*entry) + ARROUND_UP(entry->d_name_len, 4) + size))
				goto found;

#if CONFIG_EXT2_DEBUG
			ext2_dirEntry_print(entry);
#endif
      
			offset += entry->d_rec_len;
		}

		page_unlock(page);
	}

	return ENOSPC;

found:

	ext2_dmsg(1, "%s: entry found:\n", __FUNCTION__);

#if CONFIG_EXT2_DEBUG
	ext2_dirEntry_print(entry);
#endif

	if(entry->d_inode != 0)
	{
		size  = entry->d_rec_len;
		entry->d_rec_len = sizeof(*entry) + ARROUND_UP(entry->d_name_len,4);
		size -= entry->d_rec_len;
		entry = (struct ext2_dirEntry_s*)((uint8_t*)entry + entry->d_rec_len);
		entry->d_rec_len = size;
	}
	else
	{
		if((n_info->ctx->blk_size - (offset % n_info->ctx->blk_size)) < size)
		{
			page_unlock(page);

			printk(WARNING, "%s: unexpected directory format, dir %s, offset %d, size %d, pg %d\n",
			       __FUNCTION__,
			       &parent->n_name[0],
			       offset, 
			       size, 
			       i);
     
			return EIO;
		}

		size = (entry->d_rec_len == 0) ? (n_info->ctx->blk_size - size) : (entry->d_rec_len - size);
		entry->d_rec_len = size;
	}

	entry->d_inode     = n_info->ino;
	entry->d_name_len  = len;
	entry->d_file_type = ext2_ino2dir_ftype_convert(n_info->inode.i_mode);
	strcpy((char*)&entry->d_name[0], (char*)&node->n_name[0]);
  
	p_info->flags |= EXT2_GF_DIRTY;
	page_unlock(page);
  
	size = offset + (i * PMM_PAGE_SIZE);

	if(size > parent->n_size)
	{
		rwlock_wrlock(&p_info->rwlock);
		parent->n_size += (i * PMM_PAGE_SIZE);
		rwlock_unlock(&p_info->rwlock);
	}

	return 0;
}

error_t ext2_node_read(struct vfs_node_s *parent, struct vfs_node_s *node)
{
	struct ext2_inode_s *inode;
	struct ext2_node_s  *n_info;
	uint_t mode;
	error_t err;

	ext2_dmsg(1, "%s: started, node %s\n", __FUNCTION__, node->n_name);
  
	n_info = node->n_pv;

	err = ext2_node_io(parent->n_pv, n_info, EXT2_GF_READ);
  
	if(err) return err;

	inode = &n_info->inode;

	node->n_size  = inode->i_size;
	node->n_links = inode->i_links;
	node->n_type  = VFS_EXT2_TYPE;
	node->n_acl   = inode->i_file_acl;
	mode          = inode->i_mode & EXT2_S_MASK;

	if(mode != EXT2_S_IFDIR)
		node->n_size = node->n_size | (((uint64_t)inode->i_dir_acl) << 32);
  
	if(mode == EXT2_S_IFDIR)
	{
		VFS_SET(node->n_attr,VFS_DIR);
		VFS_SET(node->n_stat.st_mode, VFS_IFDIR);
		node->n_acl = inode->i_dir_acl;
	}
	else if(mode == EXT2_S_IFCHR)
	{
		VFS_SET(node->n_attr,VFS_DEV_CHR);
		VFS_SET(node->n_stat.st_mode, VFS_IFCHR);
	} 
	else if(mode == EXT2_S_IFBLK)
	{
		VFS_SET(node->n_attr,VFS_DEV_BLK);
		VFS_SET(node->n_stat.st_mode, VFS_IFBLK);
	}
	else if(mode == EXT2_S_IFIFO)
	{
		VFS_SET(node->n_attr,VFS_FIFO);
		VFS_SET(node->n_stat.st_mode, VFS_IFIFO);
	}
	else if(mode == EXT2_S_IFSOCK)
	{
		VFS_SET(node->n_attr,VFS_SOCK);
		VFS_SET(node->n_stat.st_mode, VFS_IFSOCK);
	}
	else if(mode == EXT2_S_IFLNK)
	{
		VFS_SET(node->n_attr,VFS_SYMLNK);
		VFS_SET(node->n_stat.st_mode, VFS_IFLNK);
	}
	else
	{
		VFS_SET(node->n_stat.st_mode, VFS_IFREG);
	}

	ext2_to_vfs_access_rights(node, inode);
  
	node->n_stat.st_dev     = (uint_t)n_info->ctx->dev;
	node->n_stat.st_ino     = n_info->ino;
	node->n_stat.st_rdev    = VFS_EXT2_TYPE;
	node->n_stat.st_blksize = n_info->ctx->blk_size;
	node->n_stat.st_blocks  = inode->i_blocks;
	node->n_stat.st_atime   = inode->i_atime;
	node->n_stat.st_ctime   = inode->i_ctime;
	node->n_stat.st_mtime   = inode->i_mtime;

#if CONFIG_EXT2_DEBUG
	ext2_inode_print(inode);
#endif

	ext2_dmsg(1, "+++ %s: ended, node size %d\n", __FUNCTION__,
		  (uint32_t)node->n_size);

	return 0;
}


VFS_INIT_NODE(ext2_init_node)
{
	struct ext2_node_s *node_info;
	kmem_req_t req;
	error_t err;

	ext2_dmsg(1,"+++++ %s called\n", __FUNCTION__);

	if(node->n_pv != NULL)
		return EINVAL;

	if(node->n_type != VFS_EXT2_TYPE)
		return EINVAL;

	err       = ENOMEM;
	req.type  = KMEM_MAPPER;
	req.size  = sizeof(*node->n_mapper);
	req.flags = AF_KERNEL;
  
	node->n_mapper = kmem_alloc(&req);
  
	if(node->n_mapper == NULL)
		return err;

	err = mapper_init(node->n_mapper, 
			  &ext2_node_mapper_op,
			  node,
			  NULL);

	if(err) goto fail_mapper;

	req.type = KMEM_EXT2_NODE;
	req.size = sizeof(*node_info);
    
	if((node_info = kmem_alloc(&req)) == NULL)
		goto fail_node;
     
	node->n_pv = (void *)node_info;

	//mutex_init(&node_info->mutex, "ext2-node");
	rwlock_init(&node_info->rwlock);
	node_info->vfs_node = node;
	node_info->ctx = node->n_ctx->ctx_pv;
	node_info->ino_tbl_blkid = 0;
	node_info->ino = 0;
	node_info->flags = 0;

	ext2_dmsg(1, "+++ %s: ended\n", __FUNCTION__);
	return 0;

fail_node:
fail_mapper:
	req.type = KMEM_MAPPER;
	req.ptr  = node->n_mapper;
	kmem_free(&req);
	return err;
}

VFS_RELEASE_NODE(ext2_release_node)
{
	kmem_req_t req;
	struct ext2_node_s *n_info;

	ext2_dmsg(1,"+++++ %s called\n", __FUNCTION__);
  
	n_info = node->n_pv;

	if(n_info != NULL)
	{
		if(n_info->flags & EXT2_GF_DIRTY)
			ext2_node_io(node->n_parent->n_pv, n_info, EXT2_GF_WRITE);
	}

	if(node->n_mapper != NULL)
	{
		ext2_dmsg(1, "%s: Destroy mapper of node %s\n", __FUNCTION__, node->n_name);
		mapper_destroy(node->n_mapper, true);
		req.type = KMEM_MAPPER;
		req.ptr  = node->n_mapper;
		kmem_free(&req);
	}

	if(n_info != NULL)
	{
		req.type = KMEM_EXT2_NODE;
		req.ptr  = node->n_pv;
		kmem_free(&req);
		node->n_pv = NULL;
	}

	ext2_dmsg(1, "+++ %s: ended\n", __FUNCTION__);
	return 0;
}

VFS_STAT_NODE(ext2_stat_node)
{
	ext2_dmsg(1, "%s: +++ called for node %s\n", __FUNCTION__, node->n_name);
	node->n_stat.st_nlink   = node->n_links;
	node->n_stat.st_uid     = node->n_uid;
	node->n_stat.st_gid     = node->n_gid;
	node->n_stat.st_size    = node->n_size;
	return 0;
}

VFS_CREATE_NODE(ext2_create_node)
{
	struct ext2_node_s *n_info;
	struct ext2_node_s *p_info;
	uint_t ino;
	uint_t grp_id;
	bool_t isDir;
	error_t err;

	ext2_dmsg(1, "++ %s: called for parent [%s] node [%s]\n", 
		  __FUNCTION__,
		  parent->n_name,
		  node->n_name);
   
	p_info = parent->n_pv;
	n_info = node->n_pv;
	grp_id = (p_info->ino - 1) / p_info->ctx->inodes_per_grp;
	isDir  = (node->n_attr & VFS_DIR) ? true : false;

	err = ext2_ialloc(p_info->ctx, 
			  grp_id, 
			  isDir,
			  &ino);

	if(err) return err;

	n_info->ino   = ino;
	node->n_links = 1;
	node->n_size  = 0;

	if(isDir)
	{
		err = ext2_dir_init(parent, node);

		if(err) goto fail_dir;
	}

	vfs_to_ext2_access_mode(node, &n_info->inode);
	vfs_to_ext2_access_rights(node, &n_info->inode);

	n_info->inode.i_uid        = current_task->uid;
	n_info->inode.i_size       = 0;
	n_info->inode.i_atime      = p_info->inode.i_atime + 2;
	n_info->inode.i_ctime      = p_info->inode.i_ctime + 2;
	n_info->inode.i_mtime      = p_info->inode.i_mtime + 2;
	n_info->inode.i_dtime      = p_info->inode.i_dtime;
	n_info->inode.i_gid        = current_task->gid;
	n_info->inode.i_links      = node->n_links;
	n_info->inode.i_flags      = 0;
	n_info->inode.i_generation = 0;
	n_info->inode.i_file_acl   = 0;
	n_info->inode.i_dir_acl    = 0;
	n_info->inode.i_faddr      = 0;

	err = ext2_stat_node(node);

	if(err) goto fail_stat;

	err = ext2_node_io(p_info, n_info, EXT2_GF_WRITE);

	if(err) goto fail_node_io;

	err = ext2_node_link(parent,node);

	if(err) goto fail_link;
  
	return 0;

fail_link:
fail_node_io:
fail_stat:
	ext2_node_free_blocks(node);

fail_dir:
	ext2_ifree(n_info->ctx, ino);
	return err;
}

VFS_LOOKUP_NODE(ext2_lookup_node)
{ 
	struct ext2_node_s *n_info;
	struct ext2_dirEntry_s *entry;
	struct mapper_s *mapper;
	struct page_s *page;
	uint_t count;
	uint8_t *ptr;
	uint_t pages_nr;
	uint_t offset;
	uint_t i;
	uint_t ret;
	error_t err;

	pages_nr = parent->n_size / PMM_PAGE_SIZE; 
	pages_nr = (pages_nr == 0) ? 1 : pages_nr;
	mapper   = parent->n_mapper;
	n_info   = node->n_pv;
	count    = strlen(&node->n_name[0]);

	for(i = 0; i < pages_nr; i++)
	{
		page = mapper_get_page(mapper, i, MAPPER_SYNC_OP, NULL);
    
		if(page == NULL)
			return current_thread->info.errno;

		ptr = ppm_page2addr(page);

		page_lock(page);
    
		offset = 0;

		while(offset < PMM_PAGE_SIZE)
		{
			entry = (struct ext2_dirEntry_s*)(ptr + offset);
      
			if((entry->d_inode == 0) && (entry->d_rec_len == 0))
				break;

			if(entry->d_inode != 0)
			{
#if CONFIG_EXT2_DEBUG
				ext2_dirEntry_print(entry);
#endif

				if(count == entry->d_name_len)
				{
					ret = strncmp(&node->n_name[0], (char*)&entry->d_name[0], count);
	  
					if(ret == 0)
						goto found;
				}
			}

			offset += entry->d_rec_len;
		}

		page_unlock(page);
	}

	return VFS_NOT_FOUND;

found:
  
	if((node->n_attr & VFS_DIR) && (entry->d_file_type != EXT2_FT_DIR))
		return ENOTDIR;

	n_info->ino = entry->d_inode;

	page_unlock(page);

	err = ext2_node_read(parent,node);
  
	if(err) return err;

	return VFS_FOUND;
}

VFS_WRITE_NODE(ext2_write_node)
{
	struct ext2_node_s *n_info;
  
	n_info = node->n_pv;

	vfs_to_ext2_access_mode(node, &n_info->inode);
	vfs_to_ext2_access_rights(node, &n_info->inode);

	n_info->inode.i_uid        = node->n_stat.st_uid;
	n_info->inode.i_gid        = node->n_stat.st_gid;
	n_info->inode.i_links      = node->n_links;
	n_info->inode.i_size       = (uint32_t)node->n_size;
	n_info->inode.i_flags      = 0;
	n_info->inode.i_generation = 0;
	n_info->inode.i_file_acl   = 0;
	n_info->inode.i_dir_acl    = (node->n_attr & VFS_DIR) ? 0 : (node->n_size >> 32);
	n_info->inode.i_faddr      = 0;
  
	return ext2_node_io(node->n_parent->n_pv, n_info, EXT2_GF_WRITE);
}

VFS_UNLINK_NODE(ext2_unlink_node)
{
	return ENOSYS;
}

/* FIXME: do the complete work ! */
VFS_TRUNC_NODE(ext2_trunc_node)
{
	node->n_size = 0;
	return 0;
}


const struct vfs_node_op_s ext2_n_op =
{
	.init    = ext2_init_node,
	.create  = ext2_create_node,
	.lookup  = ext2_lookup_node,
	.write   = ext2_write_node,
	.release = ext2_release_node,
	.unlink  = ext2_unlink_node,
	.stat    = ext2_stat_node,
	.trunc   = ext2_trunc_node
};

KMEM_OBJATTR_INIT(ext2_kmem_node_init)
{
	attr->type   = KMEM_EXT2_NODE;
	attr->name   = "KCM EXT2 Node";
	attr->size   = sizeof(struct ext2_node_s);
	attr->aligne = 0;
	attr->min    = CONFIG_EXT2_NODE_MIN;
	attr->max    = CONFIG_EXT2_NODE_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}
