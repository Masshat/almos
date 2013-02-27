/*
 * fat32/fat32_node.c - fat32 node related operations
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

#include <kmem.h>
#include <string.h>
#include <thread.h>
#include <cluster.h>
#include <ppm.h>
#include <pmm.h>
#include <page.h>
#include <mapper.h>
#include <vfs.h>

#include <fat32.h>
#include <fat32-private.h>


KMEM_OBJATTR_INIT(vfat_kmem_node_init)
{
	attr->type   = KMEM_VFAT_NODE;
	attr->name   = "KCM VFAT Node";
	attr->size   = sizeof(struct vfat_node_s);
	attr->aligne = 0;
	attr->min    = CONFIG_VFAT_NODE_MIN;
	attr->max    = CONFIG_VFAT_NODE_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}

inline void vfat_getshortname(char *from, char *to) {
	char *p = to;
	char *q = from;
	uint_fast8_t d;

	for(d=0; (d < 8 && *q != ' '); d++)
		*p++ = *q++;

	if (*(q = &from[8]) != ' ')
	{
		*p++ = '.';
		d = 0;
		while (d++ < 3 && *q != ' ')
			*p++ = *q++;
	}

	*p = 0;
}

static inline void vfat_convert_name(char *str1, char *str2) {
	uint_fast8_t i;
	char *extention_ptr;

	extention_ptr = str2 + 8;
	memset(str2,' ',11);

	for(i=0; ((*str1) && (*str1 != '.') && (i<11)) ; i++)
		*str2++ = *str1++;

	if((i<=8) && (*str1 == '.')) {
		str1++;
		memcpy(extention_ptr,str1,3);
	}
}

VFS_INIT_NODE(vfat_init_node)
{
	struct vfat_node_s *node_info;
	kmem_req_t req;
	error_t err;

	if(node->n_type != VFS_VFAT_TYPE)
		return EINVAL;

	req.type       = KMEM_MAPPER;
	req.size       = sizeof(*node->n_mapper);
	req.flags      = AF_KERNEL;
	node->n_mapper = kmem_alloc(&req);
  
	if(node->n_mapper == NULL)
		return VFS_ENOMEM;

	err = mapper_init(node->n_mapper, NULL, NULL, NULL);

	if(err)
		return err;

	if(node->n_pv != NULL)
		node_info = (struct vfat_node_s*)node->n_pv;
	else 
	{
		req.type = KMEM_VFAT_NODE;
		req.size = sizeof(*node_info);
    
		if((node_info = kmem_alloc(&req)) == NULL)
		{
			req.type = KMEM_MAPPER;
			req.ptr  = node->n_mapper;
			kmem_free(&req);
			return VFS_ENOMEM;
		}
     
		node->n_pv = (void *) node_info;
	}

	memset(node_info, 0, sizeof(*node_info));    
	return 0;
}


VFS_RELEASE_NODE(vfat_release_node)
{
	kmem_req_t req;
  
	if(node->n_mapper != NULL)
	{
		mapper_destroy(node->n_mapper, true);
		req.type = KMEM_MAPPER;
		req.ptr  = node->n_mapper;
		kmem_free(&req);
	}

	if(node->n_pv != NULL)
	{
		req.type = KMEM_VFAT_NODE;
		req.ptr  = node->n_pv;
		kmem_free(&req);
		node->n_pv = NULL;
	}
  
	return 0;
}


VFS_CREATE_NODE(vfat_create_node) 
{
	struct vfat_node_s *node_info;
	struct vfat_node_s *parent_info;
	struct vfat_context_s *ctx;
	struct page_s *page;
	struct mapper_s *mapper;
	struct vfat_DirEntry_s *dir;
	struct page_s *tmp_page;
	kmem_req_t req;
	uint_t current_page;
	uint_t entries_nr;
	uint_t entry;
	vfat_cluster_t new_cluster;
	size_t current_offset;
	error_t err;

	ctx         = (struct vfat_context_s*) parent->n_ctx->ctx_pv;
	entries_nr  = PMM_PAGE_SIZE / sizeof(struct vfat_DirEntry_s);
	mapper      = parent->n_mapper;
	dir         = NULL;
	node_info   = NULL;
	parent_info = NULL;
	new_cluster = 0;
	err         = 0;
	entry       = 0;
	page        = NULL;

	vfat_dmsg(1,"vfat_create_node started, node to be created %s, its parent %s\n",
		  node->n_name,parent->n_name);

	if(node->n_type != VFS_VFAT_TYPE)
		return VFS_EINVAL;

	// find a first cluster for the new file
	if(!(node->n_attr & (VFS_FIFO | VFS_DEV)))
	{
		if(vfat_alloc_fat_entry(ctx, &new_cluster))
			return VFS_IO_ERR;
    
		if(new_cluster == 0)
			return -VFS_ENOSPC;
	}

	node_info      = node->n_pv;
	parent_info    = (struct vfat_node_s*) parent->n_pv;
	current_page   = 0;
	current_offset = 0;

	while(1) 
	{
		if ((page = mapper_get_page(mapper, current_page, MAPPER_SYNC_OP, NULL)) == NULL)
		{
			err = VFS_IO_ERR;
			goto VFAT_CREATE_NODE_ERR;
		}

		dir = (struct vfat_DirEntry_s*) ppm_page2addr(page);
    
		page_lock(page);
    
		for(entry=0; entry < entries_nr; entry ++)
		{
			if((dir[entry].DIR_Name[0] == 0x00) || (dir[entry].DIR_Name[0] == 0xE5))
			{
				vfat_dmsg(1,"%s: found entry %d in page %d, name[0] %d\n",
					  __FUNCTION__,
					  entry, 
					  current_page, 
					  dir[entry].DIR_Name[0]);
				goto FREE_ENTRY_FOUND;
			}
			current_offset += sizeof (struct vfat_DirEntry_s);
		}
    
		page_unlock(page);
		current_page ++;
	}

FREE_ENTRY_FOUND:
	current_offset += sizeof (struct vfat_DirEntry_s);
	// we need to set the next entry to 0 if we got the last one
	if (dir[entry].DIR_Name[0] == 0x00) 
	{
		if(entry == entries_nr - 1) 
		{
			req.type  = KMEM_PAGE;
			req.size  = 0;
			req.flags = AF_USER | AF_ZERO;
			tmp_page  = kmem_alloc(&req);
      
			if(tmp_page != NULL)
				err = mapper_add_page(mapper, tmp_page, current_page + 1);
			else 
				err = ENOMEM;

			if(err)
			{
				page_unlock(page);

				if(tmp_page != NULL)
				{
					req.ptr = tmp_page;
					kmem_free(&req);
				}

				goto VFAT_CREATE_NODE_ERR;
			}
      		
			mapper->m_ops->set_page_dirty(tmp_page);

			if (current_offset == node->n_size)
				node->n_size += ctx->bytes_per_cluster;
		}
		else
			dir[entry+1].DIR_Name[0] = 0x00;
	}

	dir[entry].DIR_FstClusHI = new_cluster >> 16;
	dir[entry].DIR_FstClusLO = new_cluster & 0xFFFF;
	dir[entry].DIR_FileSize  = 0;
	dir[entry].DIR_Attr      = 0;

	if((node->n_attr & (VFS_SYS | VFS_FIFO | VFS_DEV)))
		dir[entry].DIR_Attr |= VFAT_ATTR_SYSTEM;

	if(node->n_attr & VFS_ARCHIVE)
		dir[entry].DIR_Attr |= VFAT_ATTR_ARCHIVE;

	if((node->n_attr & (VFS_RD_ONLY | VFS_FIFO | VFS_DEV)))
		dir[entry].DIR_Attr |= VFAT_ATTR_READ_ONLY;

	vfat_convert_name(node->n_name,(char *)dir[entry].DIR_Name);  /* FIXME: name may be long */

#if VFAT_INSTRUMENT
	wr_count ++;
#endif

	node_info->entry_index    = current_page*entries_nr + entry;
	node_info->parent_cluster = parent_info->node_cluster;
	node_info->node_cluster   = new_cluster;
	node->n_pv                = (void *) node_info;
	node->n_mapper->m_node    = node;
	node->n_mapper->m_ops     = &vfat_file_mapper_op;
  
	if(node->n_attr & VFS_DIR) 
	{
		dir[entry].DIR_Attr  |= VFAT_ATTR_DIRECTORY;
		node_info->flags      = VFAT_ATTR_DIRECTORY;
		node->n_size          = ctx->bytes_per_cluster;
		node->n_mapper->m_ops = &vfat_node_mapper_op;

		req.type  = KMEM_PAGE;
		req.size  = 0;
		req.flags = AF_USER | AF_ZERO;
		tmp_page  = kmem_alloc(&req);
      
		if(tmp_page != NULL)
			err = mapper_add_page(node->n_mapper, tmp_page, 0);
		else 
			err = ENOMEM;

		if(err)
		{
			if(tmp_page != NULL)
			{
				req.ptr = tmp_page;
				kmem_free(&req);
			}

			mapper->m_ops->set_page_dirty(page);
			page_unlock(page);
			return 0;
		}
 
		/* FIXME: we should also create dot & dotdot entries */
	}

	mapper->m_ops->set_page_dirty(page);
	page_unlock(page);
	return 0;

VFAT_CREATE_NODE_ERR:
	vfat_free_fat_entry(ctx,new_cluster);
	return err;
}


VFS_LOOKUP_NODE(vfat_lookup_node)
{
	struct vfat_context_s *ctx;
	struct vfat_node_s *parent_info;
	struct vfat_node_s *node_info;
	struct vfat_entry_request_s rq;
	struct vfat_DirEntry_s dir;
	uint_t entry_index;
	error_t err;

	ctx         = (struct vfat_context_s*) parent->n_ctx->ctx_pv;
	parent_info = parent->n_pv;
	node_info   = node->n_pv;
	err         = 0;

	if(!(parent_info->flags & VFAT_ATTR_DIRECTORY))
		return VFS_ENOTDIR;

	rq.ctx         = ctx;
	rq.parent      = parent;
	rq.entry       = &dir;
	rq.entry_name  = node->n_name;
	rq.entry_index = &entry_index;

	if((err=vfat_locate_entry(&rq)))
		return err;

#if 0
	if(((node->n_attr & VFS_DIR) && 1) ^ ((dir.DIR_Attr & VFAT_ATTR_DIRECTORY) && 1))
		return VFS_ENOTDIR;
#else
	if((node->n_attr & VFS_DIR) && !(dir.DIR_Attr & VFAT_ATTR_DIRECTORY))
		return VFS_ENOTDIR;
#endif

	if(dir.DIR_Attr & VFAT_ATTR_DIRECTORY)
		node->n_attr |= VFS_DIR;
	else
		node->n_size = dir.DIR_FileSize;

	if(dir.DIR_Attr & VFAT_ATTR_SYSTEM)    node->n_attr |= VFS_SYS;
	if(dir.DIR_Attr & VFAT_ATTR_ARCHIVE)   node->n_attr |= VFS_ARCHIVE;
	if(dir.DIR_Attr & VFAT_ATTR_READ_ONLY) node->n_attr |= VFS_RD_ONLY;
 
	node->n_links             = 1;
	node_info->flags          = dir.DIR_Attr;
	node_info->parent_cluster = parent_info->node_cluster;
	node_info->node_cluster   = dir.DIR_FstClusHI << 16;
	node_info->node_cluster  |= (0x0000FFFF & dir.DIR_FstClusLO);
	node_info->entry_index    = entry_index;

	if((!node_info->node_cluster)   && 
	   (node->n_attr & VFS_SYS)     && 
	   (node->n_attr & VFS_RD_ONLY) && 
	   (node->n_attr & VFS_DIR)) 
	{
		node->n_attr |= VFS_DEV;
		node->n_attr &= ~(VFS_SYS | VFS_RD_ONLY | VFS_DIR);
	} 
	else
	{
		if((!node_info->node_cluster) && 
		   (node->n_attr & VFS_SYS)   && 
		   (node->n_attr & VFS_RD_ONLY)) 
		{
			node->n_attr |= VFS_FIFO;
			node->n_attr &= ~(VFS_SYS | VFS_RD_ONLY);
		}
	}

	if((node_info->node_cluster) && (node->n_attr & VFS_DIR))
		node->n_size = vfat_cluster_count(ctx, node_info->node_cluster) * ctx->bytes_per_cluster;

	assert(node->n_mapper != NULL);

	node->n_mapper->m_node = node;
	node->n_mapper->m_ops  = (node->n_attr & VFS_DIR) ? &vfat_node_mapper_op : &vfat_file_mapper_op;

	return VFS_FOUND;
}

VFS_STAT_NODE(vfat_stat_node)
{
	struct vfat_context_s *ctx;
	struct vfat_node_s *node_info;
	uint_t mode;

	node_info = node->n_pv;
	ctx       = node->n_ctx->ctx_pv;

	mode                    = 0;
	node->n_stat.st_dev     = (uint_t)ctx->dev;
	node->n_stat.st_ino     = node_info->node_cluster;
	node->n_stat.st_nlink   = node->n_links;
	node->n_stat.st_uid     = 0;
	node->n_stat.st_gid     = 0;
	node->n_stat.st_rdev    = VFS_VFAT_TYPE;
	node->n_stat.st_size    = node->n_size;
	node->n_stat.st_blksize = ctx->bytes_per_sector;
	node->n_stat.st_blocks  = (uint32_t)node->n_size / ctx->bytes_per_sector;
	node->n_stat.st_atime   = 0;
	node->n_stat.st_mtime   = 0;
	node->n_stat.st_ctime   = 0;

	if(node->n_attr & VFS_DIR)
	{
		VFS_SET(mode, VFS_IFDIR);
	}
	else if(node->n_attr & VFS_FIFO)
	{
		VFS_SET(mode, VFS_IFIFO);
	}
	else if(node->n_attr & VFS_PIPE)
	{
		VFS_SET(mode, VFS_IFIFO);
	}
	else if(node->n_attr & VFS_DEV_CHR)
	{
		VFS_SET(mode, VFS_IFCHR);
	}
	else if(node->n_attr & VFS_DEV_BLK)
	{
		VFS_SET(mode, VFS_IFBLK);
	}
	else
		VFS_SET(mode, VFS_IFREG);

	node->n_stat.st_mode = mode;
	return 0;
}

VFS_WRITE_NODE(vfat_write_node)
{
	struct page_s *page;
	struct mapper_s *mapper;
	struct vfs_node_s *parent;
	struct vfat_context_s *ctx;
	struct vfat_node_s *node_info;
	struct vfat_DirEntry_s *dir;
	uint_t entry_index_page;

	ctx              = node->n_ctx->ctx_pv;
	node_info        = node->n_pv;
	entry_index_page = node_info->entry_index * sizeof(struct vfat_DirEntry_s*) >> PMM_PAGE_SHIFT;
	parent           = node->n_parent;
	mapper           = parent->n_mapper;

	if(node->n_type != VFS_VFAT_TYPE)
		return VFS_EINVAL;

	if ((page = mapper_get_page(mapper, entry_index_page, MAPPER_SYNC_OP, NULL)) == NULL)
		return VFS_IO_ERR;

	dir  = (struct vfat_DirEntry_s*) ppm_page2addr(page);
	dir += node_info->entry_index;

	page_lock(page);

	if(node->n_attr & VFS_DIR) dir->DIR_Attr |= VFAT_ATTR_DIRECTORY;
	if(node->n_attr & VFS_SYS) dir->DIR_Attr |= VFAT_ATTR_SYSTEM;
	if(node->n_attr & VFS_ARCHIVE) dir->DIR_Attr |= VFAT_ATTR_ARCHIVE;
	if(node->n_attr & VFS_RD_ONLY) dir->DIR_Attr |= VFAT_ATTR_READ_ONLY;

	dir->DIR_FileSize = node->n_size;

	mapper->m_ops->set_page_dirty(page);
	page_unlock(page);

#if VFAT_INSTRUMENT
	wr_count ++;
#endif
	return 0;
}

VFS_UNLINK_NODE(vfat_unlink_node)
{
	struct vfat_entry_request_s rq;
	struct page_s *page;
	struct mapper_s *mapper;
	struct vfs_node_s *parent;
	struct vfat_context_s *ctx;
	struct vfat_node_s *node_info;
	struct vfat_node_s *parent_info;
	struct vfat_DirEntry_s *dir;
	uint_t entry_index;
	uint_t entry_index_page;
	error_t err;
	uint_t val;

	err         = 0;
	ctx         = node->n_parent->n_ctx->ctx_pv;
	node_info   = node->n_pv;
	parent_info = node->n_parent->n_pv;
	parent      = node->n_parent;
	mapper      = parent->n_mapper;

	if((node->n_attr & VFS_FIFO) && (node->n_op != &vfat_n_op)) 
	{
		rq.ctx         = ctx;
		rq.parent      = parent;
		rq.entry_name  = node->n_name;
		rq.entry_index = &entry_index;

		if((err=vfat_locate_entry(&rq)))
			return err;

	} else 
	{
		entry_index = node_info->entry_index;
	}

	if(node->n_count == 0)
	{
		err = 0;

		if((node->n_links == 0) && !(node->n_attr & VFS_FIFO))
			err=vfat_free_fat_entry(ctx,node_info->node_cluster);
    
		return err;
	}

	entry_index_page = (entry_index*sizeof(struct vfat_DirEntry_s)) >> PMM_PAGE_SHIFT;
	page             = mapper_get_page(mapper, entry_index_page, MAPPER_SYNC_OP, NULL);

	if(page == NULL)
	{ 
		val = entry_index_page;
		err = VFS_IO_ERR;
		goto UNLINK_IOERR;
	}

	dir = (struct vfat_DirEntry_s*) ppm_page2addr(page);
	dir += entry_index % (PMM_PAGE_SIZE / sizeof(struct vfat_DirEntry_s));

	uint_t entries_nr = PMM_PAGE_SIZE / sizeof(struct vfat_DirEntry_s);
	val = 0;

	if (entry_index == (entries_nr - 1))
	{
		// last entry in the page, looking for the next page
		struct page_s *temp_page;
		struct vfat_DirEntry_s *temp_dir;

		temp_page = mapper_get_page(mapper, entry_index_page+1, MAPPER_SYNC_OP, NULL);

		if(temp_page == NULL)
		{
			val = entry_index_page + 1;
			err = VFS_IO_ERR;
			goto UNLINK_IOERR;
		}

		page_lock(temp_page);
		page_lock(page);

		temp_dir = (struct vfat_DirEntry_s*) ppm_page2addr(temp_page);

		if (temp_dir->DIR_Name[0] == 0x00)
			dir->DIR_Name[0] = 0x00;
		else
			dir->DIR_Name[0] = 0xE5;

		mapper->m_ops->set_page_dirty(page);
		page_unlock(page);
		page_unlock(temp_page);
	} 
	else 
	{
		// we can look for the next entry
		page_lock(page);
		dir->DIR_Name[0] = (dir[1].DIR_Name[0] == 0x00) ? 0x00 : 0xE5;
		mapper->m_ops->set_page_dirty(page);
		page_unlock(page);
	}

#if VFAT_INSTRUMENT
	wr_count ++;
#endif

UNLINK_IOERR:

	if (err == VFS_IO_ERR)
	{
		printk(WARNING,"%s: unable to load page #%d of node %s while removing node %s\n",
		       __FUNCTION__,
		       val,
		       parent->n_name,
		       node->n_name);
	}
	return err;
}


const struct vfs_node_op_s vfat_n_op =
{
	.init    = vfat_init_node,
	.create  = vfat_create_node,
	.lookup  = vfat_lookup_node,
	.write   = vfat_write_node,
	.release = vfat_release_node,
	.unlink  = vfat_unlink_node,
	.stat    = vfat_stat_node
};
