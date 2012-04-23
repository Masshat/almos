/*
 * ext2/ext2_file.c - ext2 file related operations
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
#include <ppm.h>
#include <page.h>
#include <vfs.h>

#include <ext2.h>
#include <ext2-private.h>

VFS_OPEN_FILE(ext2_open)
{
	return 0;
}

VFS_LSEEK_FILE(ext2_lseek)
{
	return 0;
}

VFS_CLOSE_FILE(ext2_close)
{
	return 0;
}

VFS_RELEASE_FILE(ext2_release)
{
	return 0;
}

VFS_READ_DIR(ext2_readdir)
{
	struct ext2_dirEntry_s *entry;
	struct page_s *page;
	uint8_t *ptr;
	uint_t count;
	error_t err;
  
	if(file->f_offset >= file->f_node->n_size)
		return VFS_EODIR;

	page = mapper_get_page(file->f_node->n_mapper, 
			       file->f_offset / PMM_PAGE_SIZE, 
			       MAPPER_SYNC_OP, 
			       NULL);
    
	if(page == NULL)
		return current_thread->info.errno;
  
	page_lock(page);

	ptr   = ppm_page2addr(page);
	entry = (struct ext2_dirEntry_s*)(ptr + (file->f_offset % PMM_PAGE_SIZE));
	err   = 0;

	if(entry->d_inode != 0)
	{     
		dirent->d_ino = entry->d_inode;
    
		count = MIN(entry->d_name_len, sizeof(dirent->d_name));

		strncpy((char*)&dirent->d_name[0], (char*)&entry->d_name[0], count);
    
		if(count < sizeof(dirent->d_name))
			dirent->d_name[count] = 0;
		else
			dirent->d_name[sizeof(dirent->d_name) - 1] = 0;

		file->f_offset += entry->d_rec_len;
	}
	else
		err = VFS_EODIR;

	page_unlock(page);

	ext2_dmsg(1, "%s: +++ ended , err %d\n", __FUNCTION__, err);
	return err;
}

const struct vfs_file_op_s ext2_f_op =
{
	.open    = ext2_open,
	.read    = vfs_default_read,
	.write   = vfs_default_write,
	.lseek   = ext2_lseek,
	.readdir = ext2_readdir,
	.close   = ext2_close,
	.release = ext2_release,
	.mmap    = vfs_default_mmap_file,
	.munmap  = vfs_default_munmap_file
};

KMEM_OBJATTR_INIT(ext2_kmem_file_init)
{
	attr->type   = KMEM_EXT2_FILE;
	attr->name   = "KCM EXT2 File";
	attr->size   = sizeof(struct ext2_file_s);
	attr->aligne = 0;
	attr->min    = CONFIG_EXT2_FILE_MIN;
	attr->max    = CONFIG_EXT2_FILE_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}

