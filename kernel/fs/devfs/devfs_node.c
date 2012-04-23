/*
 * devfs/devfs_node.c - devfs node related operations
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
#include <kmem.h>
#include <string.h>
#include <kdmsg.h>
#include <device.h>
#include <vfs.h>

#include <devfs.h>
#include <devfs-private.h>

KMEM_OBJATTR_INIT(devfs_kmem_node_init)
{
	attr->type   = KMEM_DEVFS_NODE;
	attr->name   = "KCM DevFs Node";
	attr->size   = sizeof(struct devfs_node_s);
	attr->aligne = 0;
	attr->min    = CONFIG_DEVFS_NODE_MIN;
	attr->max    = CONFIG_DEVFS_NODE_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}


VFS_INIT_NODE(devfs_init_node)
{
	register struct devfs_node_s *node_info;
	kmem_req_t req;

	node_info = node->n_pv;

	if(node->n_pv == NULL)
	{
		req.type  = KMEM_DEVFS_NODE;
		req.size  = sizeof(*node_info);
		req.flags = AF_KERNEL;
		node_info = kmem_alloc(&req);
	}

	if(node_info == NULL)
		return ENOMEM;
  
	node_info->dev = NULL;
	node->n_pv     = node_info;
	return 0;
}


VFS_RELEASE_NODE(devfs_release_node)
{
	kmem_req_t req;
  
	req.type   = KMEM_DEVFS_NODE;
	req.ptr    = node->n_pv;
	node->n_pv = NULL;
	kmem_free(&req);
	return 0;
}


VFS_CREATE_NODE(devfs_create_node)
{ 
	return ENOTSUPPORTED;
}


VFS_LOOKUP_NODE(devfs_lookup_node)
{
	register struct devfs_node_s *node_info;
	register struct metafs_s *meta_node;
	register struct device_s *dev;
	register error_t err;
	dev_params_t params;

	if(!(parent->n_attr & VFS_DIR))
		return ENOTDIR;
  
	node_info = node->n_pv;

	if((meta_node = metafs_lookup(&devfs_db.root, node->n_name)) == NULL)
		return VFS_NOT_FOUND;

	dev = metafs_container(meta_node, struct device_s, node);
  
	if((err=dev->op.dev.get_params(dev, &params)))
	{
		printk(ERROR,"ERROR: devfs_lookup_node: error %d while getting device parameters\n", err);
		return err;
	}
  
	switch(dev->type)
	{
	case DEV_BLK:
		node->n_attr |= VFS_DEV_BLK;
		break;
	case DEV_CHR:
		node->n_attr |= VFS_DEV_CHR;
		break;
	default:
		node->n_attr |= VFS_DEV;
	}

	node->n_links  = 1;
	node_info->dev = dev;
	node->n_size   = params.size;
  
	return VFS_FOUND;
}

VFS_STAT_NODE(devfs_stat_node)
{
	struct devfs_node_s *node_info;
	struct device_s *dev;
	uint_t mode;

	node_info = node->n_pv;
	dev       = node_info->dev;
	mode      = 0;

	node->n_stat.st_dev     = (uint_t)dev;
	node->n_stat.st_ino     = (uint_t)dev;
	node->n_stat.st_nlink   = node->n_links;
	node->n_stat.st_uid     = 0;
	node->n_stat.st_gid     = 0;
	node->n_stat.st_rdev    = VFS_DEVFS_TYPE;
	node->n_stat.st_size    = node->n_size;
	node->n_stat.st_blksize = 0;
	node->n_stat.st_blocks  = 0;
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
 
	node->n_stat.st_mode = mode;
	return 0;
}


VFS_WRITE_NODE(devfs_write_node)
{
	return 0;
}

VFS_UNLINK_NODE(devfs_unlink_node)
{
	return ENOTSUPPORTED;
}

const struct vfs_node_op_s devfs_n_op = 
{
	.init    = devfs_init_node,
	.create  = devfs_create_node,
	.lookup  = devfs_lookup_node,
	.write   = devfs_write_node,
	.release = devfs_release_node,
	.unlink  = devfs_unlink_node,
	.stat    = devfs_stat_node
};
