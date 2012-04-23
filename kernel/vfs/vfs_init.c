/*
 * vfs/vfs_init.c - Virtual File System initialization
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
#include <device.h>
#include <page.h>
#include <metafs.h>
#include <thread.h>

#include <vfs.h>
#include <sysfs.h>
#include <devfs.h>
#include <ext2.h>
#include <fat32.h>


#ifdef CONFIG_DRIVER_FS_PIPE
#include <pipe.h>
#endif

KMEM_OBJATTR_INIT(vfs_kmem_context_init)
{
	attr->type   = KMEM_VFS_CTX;
	attr->name   = "KCM VFS CTX";
	attr->size   = sizeof(struct vfs_context_s);
	attr->aligne = 0;
	attr->min    = CONFIG_VFS_CTX_MIN;
	attr->max    = CONFIG_VFS_CTX_MAX;
	attr->ctor   = NULL;
	attr->dtor   = NULL;
	return 0;
}

typedef struct fs_type_s
{
	uint_t type;
	bool_t isRoot;
	const char *name;
	const struct vfs_context_op_s *ops;
}fs_type_t;


static fs_type_t fs_tbl[VFS_TYPES_NR] = 
{ 
	{.type = VFS_EXT2_TYPE , .isRoot = true , .name = "Ext2FS", .ops = &ext2_ctx_op},
	{.type = VFS_SYSFS_TYPE, .isRoot = false, .name = "SysFS" , .ops = &sysfs_ctx_op},
	{.type = VFS_DEVFS_TYPE, .isRoot = false, .name = "DevFS" , .ops = &devfs_ctx_op},
	{.type = VFS_VFAT_TYPE , .isRoot = true , .name = "VfatFS", .ops = &vfat_ctx_op},
	{.type = VFS_PIPE_TYPE , .isRoot = false, .name = "FifoFS", .ops = NULL}
};

error_t vfs_node_init(struct vfs_context_s *ctx, struct vfs_node_s *node)
{
	node->n_count  = 1;
	node->n_flags  = 0;
	node->n_attr   = 0;
	node->n_state  = 0;
	node->n_size   = 0;
	node->n_links  = 0;
	node->n_type   = ctx->ctx_type;
	//node->n_readers = 0;
	//node->n_writers = 0;
	node->n_op     = ctx->ctx_node_op;
	node->n_ctx    = ctx;
	node->n_parent = NULL;
	node->n_pv     = NULL;
 
	return node->n_op->init(node);
}

error_t vfs_root_mount(struct device_s *dev,
		       const struct vfs_context_op_s *ctx_op,
		       struct vfs_context_s **vfs_ctx,
		       struct vfs_node_s **vfs_node)
{
	kmem_req_t req;
	struct vfs_context_s *ctx = NULL;
	struct vfs_node_s *root   = NULL;
	error_t err;

	vfs_dmsg(1, "%s: started\n", __FUNCTION__);

	req.type  = KMEM_VFS_CTX;
	req.size  = sizeof(*ctx);
	req.flags = AF_KERNEL;
  
	if((ctx = kmem_alloc(&req)) == NULL)
		return -VFS_ENOMEM;

	vfs_dmsg(1, "%s: root context has been allocated\n", __FUNCTION__);

	if(vfs_node != NULL)
	{
		req.type = KMEM_VFS_NODE;
		req.size = sizeof(*root);

		vfs_dmsg(1, "%s: going to alloc memory req.type %d, req.size %d, req.flags %d\n",
			 __FUNCTION__, req.type, req.size, req.flags);

		if((root = kmem_alloc(&req)) == NULL)
			return -VFS_ENOMEM;
	}

	vfs_dmsg(1, "%s: root node has been allocated\n", __FUNCTION__);

	ctx->ctx_dev = dev;

	if((err=ctx_op->create(ctx)))
		return err;

	if(vfs_node != NULL)
		if((err=vfs_node_init(ctx,root)))
			return err;

	if(vfs_node != NULL)
		if((err=ctx_op->read_root(ctx,root)))
			return err;

	if(vfs_ctx)
		*vfs_ctx = ctx;

	if(vfs_node)
		*vfs_node = root;

	return 0;
}


error_t vfs_init(struct device_s *device,
		 uint_t fs_type,
		 uint_t node_nr,
		 uint_t file_nr,
		 struct vfs_node_s **root)
{
	error_t err;
	struct vfs_node_s *fs_root;
	struct vfs_node_s *devfs_root;
	struct vfs_node_s *sysfs_root;

	vfs_dmsg(1, "%s: started\n", __FUNCTION__);
  
	assert(root != NULL);
	assert(device != NULL);
	assert(device->type == DEV_BLK);

	if(fs_type >= VFS_TYPES_NR)
	{
		printk(ERROR, "ERROR: %s: invalid fs_type value %d\n",
		       __FUNCTION__, 
		       fs_type);

		return EINVAL;
	}

	if(fs_tbl[fs_type].isRoot == false)
	{
		printk(ERROR, "ERROR: %s: fs_type (%s) is not can not be mounted as a root\n",
		       __FUNCTION__,
		       fs_tbl[fs_type].name);

		return EINVAL;
	}

	vfs_dmsg(1, "%s: Init dirty pages_list\n", __FUNCTION__);

	dirty_pages_init();

	vfs_dmsg(1, "%s: Init nodes freelist\n", __FUNCTION__);

	if((err=vfs_node_freelist_init(node_nr)))
		return err;

	vfs_dmsg(1, "%s: going to mount root node\n", __FUNCTION__);
  
	if((err=vfs_root_mount(device, fs_tbl[fs_type].ops, NULL, &fs_root)))
		return err;

	if((err=vfs_root_mount(NULL, fs_tbl[VFS_DEVFS_TYPE].ops, NULL, &devfs_root)))
		return err;

	if((err=vfs_root_mount(NULL, fs_tbl[VFS_SYSFS_TYPE].ops, NULL, &sysfs_root)))
		return err;

#if 0
	if((err=vfs_root_mount(NULL, &pipe_ctx_op, &vfs_pipe_ctx, NULL)))
		return err;
#endif

	vfs_node_up(fs_root);
	vfs_node_up(devfs_root);
	vfs_node_up(sysfs_root);

	devfs_root->n_parent = fs_root;
	sysfs_root->n_parent = fs_root;

	metafs_register(&fs_root->n_meta, &devfs_root->n_meta);
	metafs_register(&fs_root->n_meta, &sysfs_root->n_meta);

	*root = fs_root;
	return err;
}
