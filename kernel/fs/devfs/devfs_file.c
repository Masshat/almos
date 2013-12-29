/*
 * devfs/devfs_file.c - devfs file related operations
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

#include <config.h>
#include <device.h>
#include <driver.h>
#include <kmem.h>
#include <string.h>
#include <vfs.h>
#include <errno.h>
#include <metafs.h>

#include <devfs.h>
#include <devfs-private.h>


KMEM_OBJATTR_INIT(devfs_kmem_file_init)
{
	attr->type   = KMEM_DEVFS_FILE;
	attr->name   = "KCM DevFs File";
	attr->size   = sizeof(struct devfs_file_s);
	attr->aligne = 0;
	attr->min    = CONFIG_DEVFS_FILE_MIN;
	attr->max    = CONFIG_DEVFS_FILE_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}

VFS_OPEN_FILE(devfs_open)
{
	register error_t err;
	register struct devfs_context_s *ctx;
	register struct devfs_file_s *info;
	register struct device_s *dev;
	dev_request_t rq;
	kmem_req_t req;

	ctx  = node->n_ctx->ctx_pv;
	info = file->f_pv;

	if(!(node->n_attr & VFS_DIR))
	{
		dev = ((struct devfs_node_s*)file->f_node->n_pv)->dev;
    
		if(dev->type == DEV_INTERNAL)
			return EPERM;

		if(dev->type == DEV_BLK)
			VFS_SET(node->n_attr,VFS_DEV_BLK);
		else
			VFS_SET(node->n_attr,VFS_DEV_CHR);
 
		if(dev->op.dev.open != NULL)
		{
			rq.file = file;
			if((err=dev->op.dev.open(dev, &rq)))
				return err;
		}

		return 0;
	}

	if(info == NULL)
	{
		req.type  = KMEM_DEVFS_FILE;
		req.size  = sizeof(*info);
		req.flags = AF_KERNEL;
		info      = kmem_alloc(&req);
	}

	if(info == NULL) return ENOMEM;

	metafs_iter_init(&devfs_db.root, &info->iter);
	info->ctx  = ctx;
	file->f_pv = info;
  
	metafs_print(&devfs_db.root);
	return 0;
}

VFS_READ_FILE(devfs_read)
{
	register struct device_s *dev;
	dev_request_t rq;
	uint_t count;

	dev = ((struct devfs_node_s*)file->f_node->n_pv)->dev;

	rq.dst   = buffer;
	rq.count = size;
	rq.flags = 0;
	rq.file  = file;

	if((count = dev->op.dev.read(dev, &rq)) < 0)
		return count;

	return count;
}

VFS_WRITE_FILE(devfs_write)
{
	register struct device_s *dev;
	dev_request_t rq;

	dev      = ((struct devfs_node_s*)file->f_node->n_pv)->dev;
	rq.src   = (void*) buffer;
	rq.count = size;
	rq.flags = 0;
	rq.file  = file;
  
	return dev->op.dev.write(dev, &rq);
}

VFS_LSEEK_FILE(devfs_lseek)
{
	register struct device_s *dev;
	dev_request_t rq;

	dev = ((struct devfs_node_s*)file->f_node->n_pv)->dev;

	if(dev->op.dev.lseek == NULL)
		return VFS_ENOTUSED;
  
	rq.file = file;
	return dev->op.dev.lseek(dev, &rq);
}

VFS_CLOSE_FILE(devfs_close)
{
	register struct device_s *dev;
	dev_request_t rq;

	if(VFS_IS(file->f_flags,VFS_O_DIRECTORY))
		return 0;

	dev = ((struct devfs_node_s*)file->f_node->n_pv)->dev;

	if(dev->op.dev.close == NULL)
		return 0;
  
	rq.file = file;
	return dev->op.dev.close(dev, &rq);
}

VFS_RELEASE_FILE(devfs_release)
{  
	kmem_req_t req;

	if(file->f_pv == NULL) 
		return 0;
  
	req.type = KMEM_DEVFS_FILE;
	req.ptr  = file->f_pv;
	kmem_free(&req);

	file->f_pv = NULL;
	return 0;
}

VFS_READ_DIR(devfs_readdir)
{
	register struct devfs_file_s *info;
	register struct metafs_s *current;
	register struct device_s *current_dev;
  
	info = file->f_pv;
  
	if(file->f_pv == NULL)
		return ENOTDIR;

	if((current = metafs_lookup_next(&devfs_db.root, &info->iter)) == NULL)
		return EEODIR;

	current_dev    = metafs_container(current, struct device_s, node);
	dirent->d_type = (current_dev->type == DEV_BLK) ? VFS_DEV_BLK : VFS_DEV_CHR;

	strcpy((char*)dirent->d_name, metafs_get_name(current));

	return 0;
}

VFS_MMAP_FILE(devfs_mmap)
{
	register struct device_s *dev;
	dev_request_t rq;
  
	dev = ((struct devfs_node_s*)file->f_node->n_pv)->dev;

	if(dev->op.dev.mmap == NULL)
		return ENODEV;

	rq.flags  = 0;
	rq.file   = file;
	rq.region = region;

	return dev->op.dev.mmap(dev, &rq);
}

VFS_MMAP_FILE(devfs_munmap)
{
	register struct device_s *dev;
	dev_request_t rq;

	dev = ((struct devfs_node_s*)file->f_node->n_pv)->dev;

	if(dev->op.dev.munmap == NULL)
		return ENODEV;

	rq.flags  = 0;
	rq.file   = file;
	rq.region = region;

	return dev->op.dev.munmap(dev, &rq);
}


const struct vfs_file_op_s devfs_f_op = 
{
	.open    = devfs_open,
	.read    = devfs_read,
	.write   = devfs_write,
	.lseek   = devfs_lseek,
	.mmap    = devfs_mmap,
	.munmap  = devfs_munmap,
	.readdir = devfs_readdir,
	.close   = devfs_close,
	.release = devfs_release
};
