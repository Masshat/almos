/*
 * vfs/vfs-private.h - vfs private helper functions
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

#ifndef _VFS_PRIVATE_H_
#define _VFS_PRIVATE_H_

#include <spinlock.h>
#include <list.h>

struct vfs_context_s;
struct vfs_node_s;
struct vfs_file_s;

struct vfs_node_freelist_s 
{
	spinlock_t lock;			/* FIXME: should be spin_rwlock */
	struct list_entry root;
};

extern struct vfs_node_freelist_s vfs_node_freelist;

KMEM_OBJATTR_INIT(vfs_kmem_context_init);
KMEM_OBJATTR_INIT(vfs_kmem_node_init);
KMEM_OBJATTR_INIT(vfs_kmem_file_init);

/* VFS NODE FREELIST MANIPULATION */
error_t vfs_node_freelist_init(uint_t length);
void vfs_node_freelist_add (struct vfs_node_s *node, uint_t hasError);
struct vfs_node_s* vfs_node_freelist_get (struct vfs_context_s* parent_ctx);
void vfs_node_freelist_unlink(struct vfs_node_s *node);

struct vfs_file_freelist_s
{
	spinlock_t lock;
	struct list_entry root;
};

extern struct list_entry vfs_filelist_root;
extern struct vfs_file_freelist_s vfs_file_freelist;

/* VFS FILE FREELIST MANIPULATION */
struct vfs_file_s * vfs_file_get(struct vfs_node_s *node);

/* nodes references count */
void vfs_node_up(struct vfs_node_s *node);
void vfs_node_down(struct vfs_node_s *node);

#define vfs_node_up_atomic(_node)				\
	do{							\
		spinlock_lock(&vfs_node_freelist.lock);		\
		vfs_node_up(_node);				\
		spinlock_unlock(&vfs_node_freelist.lock);	\
	}while(0)

#define vfs_node_down_atomic(_node)				\
	do{							\
		spinlock_lock(&vfs_node_freelist.lock);		\
		vfs_node_down(_node);				\
		spinlock_unlock(&vfs_node_freelist.lock);	\
	}while(0)

struct vfs_node_s* vfs_node_lookup(struct vfs_node_s *node, char *name);

error_t vfs_node_load(struct vfs_node_s *root,
		      char **path,
		      uint_t flags,
		      uint_t isAbsolutePath,
		      struct vfs_node_s **node);

error_t vfs_node_create(struct vfs_node_s *parent,
			uint_t flags,
			uint_t isLast,
			struct vfs_node_s *node);

error_t vfs_node_trunc(struct vfs_node_s *node);

void vfs_print_node_freelist();

#endif	/* _VFS_PRIVATE_H_ */
