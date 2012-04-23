/*
 * fat32/fat32_file.c - fat32 file related operations
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
#include <kmem.h>
#include <string.h>
#include <ppm.h>
#include <pmm.h>
#include <page.h>
#include <mapper.h>
#include <vfs.h>
#include <sys-vfs.h>

#include <fat32.h>
#include <fat32-private.h>

KMEM_OBJATTR_INIT(vfat_kmem_file_init)
{
	attr->type   = KMEM_VFAT_FILE;
	attr->name   = "KCM VFAT File";
	attr->size   = sizeof(struct vfat_file_s);
	attr->aligne = 0;
	attr->min    = CONFIG_VFAT_FILE_MIN;
	attr->max    = CONFIG_VFAT_FILE_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}

VFS_OPEN_FILE(vfat_open) 
{
	struct vfat_context_s *ctx;
	struct vfat_node_s *node_info;
	struct vfat_file_s *file_info;
	kmem_req_t req;
  
	ctx       = node->n_ctx->ctx_pv;
	node_info = node->n_pv;
	file_info = file->f_pv;

	if(file_info == NULL)
	{
		req.type  = KMEM_VFAT_FILE;
		req.size  = sizeof(*file_info);
		req.flags = AF_KERNEL;

		if((file_info = kmem_alloc(&req)) == NULL)
			return VFS_ENOMEM;
	}

	file_info->ctx                = ctx;
	file_info->node_cluster       = node_info->node_cluster;
	file_info->pg_current_cluster = node_info->node_cluster;
	file_info->pg_current_rank    = 0;
	file->f_pv                    = file_info;

	return 0;
}

VFS_LSEEK_FILE(vfat_lseek)
{
	struct vfat_context_s *ctx;
	struct vfat_file_s *file_info;
	vfat_cluster_t cluster_rank;
	vfat_cluster_t next_cluster;
	vfat_cluster_t effective_rank;
	uint_t isExtanded;
	error_t err;

	file_info = file->f_pv;
	ctx       = file_info->ctx;
  
	if(file->f_offset == 0)
	{
		file_info->pg_current_cluster = file_info->node_cluster;
		file_info->pg_current_rank    = 0;
		return 0;
	}

	next_cluster   = file_info->pg_current_cluster;
	cluster_rank   = (vfat_offset_t)file->f_offset / ctx->bytes_per_cluster;
	effective_rank = cluster_rank;

	if(cluster_rank != file_info->pg_current_rank)
	{
		if(cluster_rank > file_info->pg_current_rank)
			effective_rank -= file_info->pg_current_rank;
		else
			next_cluster = file_info->node_cluster;
  
		if(effective_rank != 0)
		{
			if((err = vfat_cluster_lookup(ctx, 
						      next_cluster, 
						      effective_rank, 
						      &next_cluster, 
						      &isExtanded)))
				return err;
		}
	}
 
	file_info->pg_current_cluster = next_cluster;
	file_info->pg_current_rank    = cluster_rank;
	return 0;
}

VFS_CLOSE_FILE(vfat_close) 
{
	return 0;
}

VFS_RELEASE_FILE(vfat_release) 
{
	kmem_req_t req;
  
	if(file->f_pv != NULL) 
	{
		req.type   = KMEM_VFAT_FILE;
		req.ptr    = file->f_pv;
		kmem_free(&req);
		file->f_pv = NULL;
	}
	return 0;
}

VFS_READ_DIR(vfat_readdir) 
{
	struct vfat_context_s *ctx;
	struct vfat_file_s *file_info;
	struct vfat_DirEntry_s dir;
	struct page_s *page;
	struct mapper_s *mapper;
	vfat_cluster_t node_cluster;
	uint8_t *buff;
	uint32_t found;

	mapper    = file->f_node->n_mapper;
	file_info = file->f_pv;
	ctx       = file_info->ctx;
	found     = 0;

	/* TODO: dont call mapper every time, as page can be reused */
	while(!found) 
	{
		if ((page = mapper_get_page(mapper, 
					    file->f_offset >> PMM_PAGE_SHIFT, 
					    MAPPER_SYNC_OP,
					    file)) == NULL)
			return VFS_IO_ERR;

		buff  = ppm_page2addr(page);
		buff += file->f_offset % PMM_PAGE_SIZE;

		memcpy(&dir, buff, sizeof(dir));

		if(dir.DIR_Name[0] == 0x00) {
			vfat_dmsg(3,"vfat_readdir: entries termination found (0x00)\n");
			goto VFS_READ_DIR_EODIR;
		}

		if(dir.DIR_Name[0] == 0xE5) {
			vfat_dmsg(3,"entry was freeed previously\n");
			vfat_getshortname((char*)dir.DIR_Name, (char*)dirent->d_name);
			vfat_dmsg(3,"it was %s\n",dirent->d_name);
			goto VFS_READ_DIR_NEXT;
		}

		if(dir.DIR_Attr == 0x0F) {
			vfat_dmsg(3,"this entry is a long one\n");
			vfat_getshortname((char*)dir.DIR_Name, (char*)dirent->d_name);
			vfat_dmsg(3,"trying to read its name %s\n",dirent->d_name);
			goto VFS_READ_DIR_NEXT;
		}

		if(dir.DIR_Name[0] == '.')
			goto VFS_READ_DIR_NEXT;

		found = 1;
		vfat_getshortname((char *)dir.DIR_Name, (char*)dirent->d_name);

		//dirent->d_size = dir.DIR_FileSize;
		dirent->d_type = 0;

		if(dir.DIR_Attr & VFAT_ATTR_DIRECTORY)
			dirent->d_type = VFS_DIR;

		if(dir.DIR_Attr & VFAT_ATTR_SYSTEM)
			dirent->d_type |= VFS_SYS;

		if(dir.DIR_Attr & VFAT_ATTR_ARCHIVE)
			dirent->d_type |= VFS_ARCHIVE;

		if(dir.DIR_Attr & VFAT_ATTR_READ_ONLY)
			dirent->d_type |= VFS_RD_ONLY;

		node_cluster  = dir.DIR_FstClusHI << 16;
		node_cluster |= (0x0000FFFF & dir.DIR_FstClusLO);

		if((!node_cluster) && (dirent->d_type & VFS_SYS)
		   && (dirent->d_type & VFS_RD_ONLY) && (dirent->d_type & VFS_DIR)) {
			dirent->d_type |= VFS_DEV;
			dirent->d_type &= ~(VFS_SYS | VFS_RD_ONLY | VFS_DIR);
		}
		else
			if((!node_cluster) && (dirent->d_type & VFS_SYS) && (dirent->d_type & VFS_RD_ONLY)) {
				dirent->d_type |= VFS_FIFO;
				dirent->d_type &= ~(VFS_SYS | VFS_RD_ONLY);
			}

	VFS_READ_DIR_NEXT:
		file->f_offset += sizeof(struct vfat_DirEntry_s);
	}

VFS_READ_DIR_EODIR:
	return (found) ? 0 : VFS_EODIR;
}

const struct vfs_file_op_s vfat_f_op =
{
	.open    = vfat_open,
	.read    = vfs_default_read,
	.write   = vfs_default_write,
	.lseek   = vfat_lseek,
	.readdir = vfat_readdir,
	.close   = vfat_close,
	.release = vfat_release,
	.mmap    = vfs_default_mmap_file,
	.munmap  = vfs_default_munmap_file
};
