/*
 * sysfs/sysfs_node.c - sysfs node related operations
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
#include <vfs.h>

#include <sysfs.h>
#include <sysfs-private.h>

KMEM_OBJATTR_INIT(sysfs_kmem_node_init)
{
	attr->type   = KMEM_SYSFS_NODE;
	attr->name   = "KCM SysFs Node";
	attr->size   = sizeof(struct sysfs_node_s);
	attr->aligne = 0;
	attr->min    = CONFIG_SYSFS_NODE_MIN;
	attr->max    = CONFIG_SYSFS_NODE_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}


VFS_INIT_NODE(sysfs_init_node)
{
	register struct sysfs_node_s *node_info;
	kmem_req_t req;
  
	node_info = node->n_pv;

	if(node->n_pv == NULL)
	{
		req.type  = KMEM_SYSFS_NODE;
		req.size  = sizeof(*node_info);
		req.flags = AF_KERNEL;

		node_info = kmem_alloc(&req);
	}

	if(node_info == NULL)
		return ENOMEM;
  
	node->n_pv = node_info;
	return 0;
}


VFS_RELEASE_NODE(sysfs_release_node)
{
	kmem_req_t req;

	req.type   = KMEM_SYSFS_NODE;
	req.ptr    = node->n_pv;
	kmem_free(&req);
	node->n_pv = NULL;
	return 0;
}


VFS_CREATE_NODE(sysfs_create_node)
{ 
	return ENOTSUPPORTED;
}


VFS_LOOKUP_NODE(sysfs_lookup_node)
{
	register struct sysfs_node_s *parent_info;
	register struct sysfs_node_s *node_info;
	register struct metafs_s *meta;
	register uint_t hasChild;
  
	parent_info = parent->n_pv;
	node_info   = node->n_pv;
  
	sysfs_dmsg(1, "DEBUG: sysfs_lookup: %s, attr %x\n", node->n_name, node->n_attr);

	if((meta = metafs_lookup(parent_info->node, node->n_name)) == NULL)
		return VFS_NOT_FOUND;

	hasChild = metafs_hasChild(meta);

	if(hasChild && !(node->n_attr & VFS_DIR))
		return EISDIR;
  
	if(!(hasChild) && (node->n_attr & VFS_DIR))
		return ENOTDIR;

	if(hasChild)
		node->n_attr |= VFS_DIR;
	else
		node->n_size = SYSFS_BUFFER_SIZE;

	node->n_links   = 1;
	node_info->node = meta;
  
	return VFS_FOUND;
}


VFS_WRITE_NODE(sysfs_write_node)
{
	return 0;
}

VFS_UNLINK_NODE(sysfs_unlink_node)
{
	return ENOTSUPPORTED;
}

const struct vfs_node_op_s sysfs_n_op = 
{
	.init    = sysfs_init_node,
	.create  = sysfs_create_node,
	.lookup  = sysfs_lookup_node,
	.write   = sysfs_write_node,
	.release = sysfs_release_node,
	.unlink  = sysfs_unlink_node,
	.stat    = NULL
};
