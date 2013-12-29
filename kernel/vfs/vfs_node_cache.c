/*
 * vfs/vfs_node_cache.c - vfs node freelist management
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
#include <vfs.h>
#include <vfs-private.h>
#include <metafs.h>

struct vfs_node_freelist_s vfs_node_freelist;

error_t vfs_node_freelist_init(uint_t length)
{
	list_root_init(&vfs_node_freelist.root);
	spinlock_init(&vfs_node_freelist.lock, "VFS Freelist");
	return 0;
}

void vfs_node_freelist_add(struct vfs_node_s *node, uint_t hasError)
{
	if(VFS_IS(node->n_attr,VFS_PIPE))
	{
		node->n_op->release(node);
		node->n_pv = NULL;

		if(node->n_parent)
			metafs_unregister(&node->n_parent->n_meta, &node->n_meta);
		node->n_parent = NULL;
	}

	vfs_dmsg(1,"vfs_freelist_add: node %s, err %d\n",node->n_name, hasError);

#if VFS_DEBUG
	vfs_print_node_freelist();
#endif

	if(hasError)
		list_add_first(&vfs_node_freelist.root,&node->n_freelist);
	else
		list_add_last(&vfs_node_freelist.root,&node->n_freelist);
 
	VFS_SET(node->n_state,VFS_FREE);

#if VFS_DEBUG
	vfs_print_node_freelist();
#endif
}


struct vfs_node_s* vfs_node_freelist_get(struct vfs_context_s* parent_ctx)
{
	register struct vfs_node_s *node;
	struct cluster_s *cluster;
	uint_t current_nodes_nr;
	kmem_req_t req;

	cluster          = current_cluster;
	current_nodes_nr = atomic_get(&cluster->vfs_nodes_nr);
	node             = NULL;

	if((current_nodes_nr < CONFIG_VFS_NODES_PER_CLUSTER) || (list_empty(&vfs_node_freelist.root)))
	{
		req.type  = KMEM_VFS_NODE;
		req.size  = sizeof(*node);
		req.flags = AF_KERNEL;

		node = kmem_alloc(&req);

		if(node != NULL)
		{
			atomic_add(&cluster->vfs_nodes_nr, 1);
			node->n_op     = NULL;
			node->n_ctx    = NULL;
			node->n_parent = NULL;
			node->n_mapper = NULL;
		}
	}

	if((node == NULL) && (list_empty(&vfs_node_freelist.root)))
		return NULL;

	if(node == NULL)
	{
		node = list_first(&vfs_node_freelist.root, struct vfs_node_s, n_freelist);
		list_unlink(&node->n_freelist);
	}

	if((node->n_parent != NULL) && (node->n_links != 0))
		metafs_unregister(&node->n_parent->n_meta, &node->n_meta);

	vfs_dmsg(1,"vfs_freelist_get: node %s\nafter\n",node->n_name);

#if VFS_DEBUG
	vfs_print_node_freelist();
#endif

	if(node->n_op != NULL) 
		node->n_op->release(node);

	node->n_count   = 0;
	node->n_flags   = 0;
	node->n_attr    = 0;
	node->n_state   = 0;
	node->n_size    = 0;
	node->n_links   = 0;
	//node->n_readers = 0;
	//node->n_writers = 0;
	node->n_parent  = NULL;

	if(node->n_ctx != parent_ctx)
	{
		node->n_type = parent_ctx->ctx_type;
		node->n_ctx  = parent_ctx;
		node->n_op   = parent_ctx->ctx_node_op;
		node->n_pv   = NULL;
	}

	if(node->n_op->init(node))
	{
		vfs_node_freelist_add(node,1);
		return NULL;
	}

	return node;
}

void vfs_node_freelist_unlink(struct vfs_node_s *node)
{
	list_unlink(&node->n_freelist);
	VFS_CLEAR(node->n_state,VFS_FREE);
}

static void vfs_node_ctor(struct kcm_s *kcm, void *ptr)
{
	struct vfs_node_s *node;

	node = (struct vfs_node_s*)ptr;
	rwlock_init(&node->n_rwlock);
	wait_queue_init(&node->n_wait_queue, "VFS Node");
	metafs_init(&node->n_meta, node->n_name);
}

KMEM_OBJATTR_INIT(vfs_kmem_node_init)
{
	attr->type   = KMEM_VFS_NODE;
	attr->name   = "KCM VFS Node";
	attr->size   = sizeof(struct vfs_node_s);
	attr->aligne = 0;
	attr->min    = CONFIG_VFS_NODE_MIN;
	attr->max    = CONFIG_VFS_NODE_MAX;
	attr->ctor   = vfs_node_ctor;
	attr->dtor   = NULL;

	return 0;
}


/* ================================================================ */


void vfs_print_node_freelist()
{
	uint8_t i=0;
	struct vfs_node_s *node;
	struct list_entry *iter;

	printk(DEBUG, "vfs_freelist: [");
	list_foreach_forward(&vfs_node_freelist.root, iter)
	{
		i++;
		node = list_element(iter, struct vfs_node_s, n_freelist);
		printk(DEBUG, "%s, ", node->n_name);
	}
	printk(DEBUG, "\b\b] %d elements\n",i);
}
