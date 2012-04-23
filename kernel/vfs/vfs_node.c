/*
 * vfs/vfs_node.c - vfs node related operations
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

#include <thread.h>
#include <scheduler.h>
#include <list.h>
#include <string.h>
#include <vfs.h>
#include <vfs-private.h>
#include <task.h>
#include <spinlock.h>
#include <cpu-trace.h>

#include <metafs.h>

struct vfs_node_s* vfs_node_lookup(struct vfs_node_s *node, char *name)
{
	uint_t isDotDot;
	struct task_s *task;
	struct metafs_s *child;

	cpu_trace_write(current_cpu, vfs_node_lookup);

	if(((name[0] == '/') || (name[0] == '.')) && (name[1] == '\0'))
		return node;

	isDotDot = ((name[0] == '.') && (name[1] == '.')) ? 1 : 0;
	task     = current_task;

	if(isDotDot && (node == task->vfs_root))
		return node;

	if(isDotDot)
		return node->n_parent;

	if((child = metafs_lookup(&node->n_meta, name)) == NULL)
		return NULL;

	return metafs_container(child, struct vfs_node_s, n_meta);
}

void vfs_node_up(struct vfs_node_s *node)
{
	node->n_count ++;

	vfs_dmsg(1,"+++++++ UP NODE %s, %d +++++\n",
		 node->n_name,
		 node->n_count);
}

void vfs_node_down(struct vfs_node_s *node)
{
	while(node != NULL)
	{
		vfs_dmsg(1,"+++++++ DOWN NODE %s, %d ++++++\n",
			 node->n_name, node->n_count -1);

		if((--node->n_count)) break;

		if(node->n_links == 0)
		{
			VFS_SET(node->n_state,VFS_INLOAD);
			spinlock_unlock(&vfs_node_freelist.lock);

			node->n_parent->n_op->unlink(node);

			spinlock_lock(&vfs_node_freelist.lock);
			VFS_CLEAR(node->n_state,VFS_INLOAD);
			wakeup_all(&node->n_wait_queue);

			vfs_node_freelist_add(node,1);
		}
		else
		{
			if(VFS_IS(node->n_state,VFS_DIRTY))
			{
				VFS_SET(node->n_state,VFS_INLOAD);
				spinlock_unlock(&vfs_node_freelist.lock);

				node->n_op->write(node);

				spinlock_lock(&vfs_node_freelist.lock);
				VFS_CLEAR(node->n_state,VFS_INLOAD);
				VFS_CLEAR(node->n_state,VFS_DIRTY);
				wakeup_all(&node->n_wait_queue);
			}

			vfs_node_freelist_add(node,0);
		}
		node = node->n_parent;
	}
}


error_t vfs_node_create(struct vfs_node_s *parent,
			uint_t flags,
			uint_t isLast,
			struct vfs_node_s *node)
{
	error_t err = 0;
	struct thread_s *this;

	this = current_thread;

	cpu_trace_write(this->local_cpu, vfs_node_create);

	node->n_attr = (isLast) ? flags & 0x0000FFFF : VFS_DIR;

	err=node->n_op->lookup(parent,node);

	vfs_dmsg(1,"[ %x :: %x ] n_op->lookup(parent %s, node %s) : err %d", this, this->lcpu->gid,
		 parent->n_name, node->n_name, err);

	if((err != VFS_NOT_FOUND) && (err != VFS_FOUND))
		return err;

	if((err == VFS_FOUND) && (flags & VFS_O_EXCL) && (flags & VFS_O_CREATE) && (isLast))
		return EEXIST;

	vfs_dmsg(1,"[ %x :: %d ] node %s, found ? %d, isLast ? %d, VFS_O_CREATE ? %d, VFS_FIFO? %d\n",
		 this, this->lcpu->gid, node->n_name,err,
		 isLast,flags & VFS_O_CREATE, node->n_attr & VFS_FIFO);

	if((err == VFS_NOT_FOUND) && (flags & VFS_O_CREATE) && (isLast))
	{

#if 0
		if((err=node->n_op->init(node)))
			return err;
#endif
    
		if((err=node->n_op->create(parent,node)))
			return err;
	}

	/* FIXME: PUT A TABLE OF MOUNTED FILE SYSTEM'S CONTEXTS, AND INDEX BY TYPE */
#ifdef CONFIG_DRIVER_FS_PIPE
	if(VFS_IS(node->n_attr,VFS_FIFO) && (!(VFS_IS(flags,VFS_O_UNLINK))))
	{
		vfs_dmsg(1,"node %s, is a fifo. releasing it",node->n_name);

		if((err=node->n_op->write(node)))
			return -err;

		if((err=node->n_op->release(node)))
			return -err;

		node->n_type = vfs_pipe_ctx->ctx_type;
		node->n_ctx  = vfs_pipe_ctx;
		node->n_op   = vfs_pipe_ctx->ctx_node_op;
		node->n_pv   = NULL;
		err          = - node->n_op->init(node);
	}
#endif
	return err;
}


error_t vfs_node_load(struct vfs_node_s *root,
		      char **path,
		      uint_t flags,
		      uint_t isAbsolutePath,
		      struct vfs_node_s **node)
{
	struct vfs_node_s *child;
	struct vfs_node_s *current_parent;
	struct thread_s *this;
	struct task_s *task;
	uint_t i;
	uint_t isLast;
	error_t err;
	uint_t isHit;

	task = current_task;
	this = current_thread;

	cpu_trace_write(this->local_cpu, vfs_node_load);

	if(isAbsolutePath)
	{
		vfs_dmsg(2,"[ %x :: %x ] path is absolute, cwd != root\n",this,this->lcpu->gid);
		current_parent = task->vfs_root;
	}
	else
		current_parent = root;

	child = NULL;
	err   = 0;
	isHit = 0;

	spinlock_lock(&vfs_node_freelist.lock);	                 /* <-- */
	vfs_node_up(current_parent);

	for(i=0; path[i] != NULL; i++)
	{
		isLast = (path[i+1] == NULL) ? 1 : 0;

		if((child = vfs_node_lookup(current_parent,path[i])) == NULL)
		{
			if((child = vfs_node_freelist_get(current_parent->n_ctx)) == NULL)
			{
				err = ENOMEM;
				goto VFS_NODE_LOAD_ERROR;
			}
			strcpy(child->n_name, path[i]);
			VFS_SET(child->n_state,VFS_INLOAD);

			metafs_register(&current_parent->n_meta, &child->n_meta);

			spinlock_unlock(&vfs_node_freelist.lock);            /* --> */
			vfs_dmsg(1,"[ %x :: %x ] going to physical load of %s\n",this,current_cpu->gid,child->n_name);
      
			err=vfs_node_create(current_parent, flags, isLast,child);

			spinlock_lock(&vfs_node_freelist.lock);              /* <-- */
			VFS_CLEAR(child->n_state,VFS_INLOAD);
			wakeup_all(&child->n_wait_queue);

			if(err)
			{
				metafs_unregister(&current_parent->n_meta, &child->n_meta);
				vfs_node_freelist_add(child,1);
				goto VFS_NODE_LOAD_ERROR;
			}

			child->n_parent = current_parent;
			goto VFS_NODE_LOAD_CONTINUE;
		}

		if(VFS_IS(child->n_state,VFS_FREE))
		{
			vfs_dmsg(2,"[ %x :: %x ] child %s is in the node_freelist\n", this, this->lcpu->gid,child->n_name);
			isHit = 1;
			vfs_node_freelist_unlink(child);
		}

		if(VFS_IS(child->n_state,VFS_INLOAD))
		{
			vfs_dmsg(2,"node %s is inload\n",child->n_name);
			wait_on(&child->n_wait_queue, WAIT_LAST);
			spinlock_unlock_nosched(&vfs_node_freelist.lock);        /* --> */
			sched_sleep(this);
			spinlock_lock(&vfs_node_freelist.lock);                  /* <-- */

			if((child=vfs_node_lookup(current_parent,path[i])) == NULL)
			{
				err = EIO;
				goto VFS_NODE_LOAD_ERROR;
			}
		}

		if(isLast && ((flags & VFS_DIR) != (child->n_attr & VFS_DIR)))
		{
			err = EISDIR;
			if(isHit) vfs_node_freelist_add(child,1);
			goto VFS_NODE_LOAD_ERROR;
		}

		if((flags & VFS_O_EXCL) && (flags & VFS_O_CREATE) && isLast)
		{
			err = EEXIST;
			if(isHit) vfs_node_freelist_add(child,1);
			goto VFS_NODE_LOAD_ERROR;
		}

		if(!isHit)
		{
			vfs_dmsg(2,"node was in use\n");
			vfs_node_down(current_parent);
		}

	VFS_NODE_LOAD_CONTINUE:
		vfs_node_up(child);
		spinlock_unlock(&vfs_node_freelist.lock);    /* --> */
		current_parent = child;
		spinlock_lock(&vfs_node_freelist.lock);      /* <-- */
	}

	spinlock_unlock(&vfs_node_freelist.lock);      /* --> */
	*node = child;
	return 0;

VFS_NODE_LOAD_ERROR:
	vfs_node_down(current_parent);
	spinlock_unlock(&vfs_node_freelist.lock);      /* --> */

	vfs_dmsg(1,"[ %x :: %x ] %s: Error While Creating/Loading In-Core Node [%s], err %d\n",
		 this, 
		 this->lcpu->gid, 
		 __FUNCTION__, 
		 path[i], 
		 err);
	return err;
}


error_t vfs_node_trunc(struct vfs_node_s *node)
{
	error_t err;
  
	if((node->n_size == 0)       ||
	   (node->n_attr & VFS_DEV)  || 
	   (node->n_attr & VFS_FIFO) || 
	   (node->n_attr & VFS_DIR))
		return 0;			/* Ingored */

	spinlock_lock(&vfs_node_freelist.lock);
	VFS_SET(node->n_state, VFS_INLOAD);
	spinlock_unlock(&vfs_node_freelist.lock);

	err = node->n_op->trunc(node);
  
	spinlock_lock(&vfs_node_freelist.lock);
	VFS_CLEAR(node->n_state, VFS_INLOAD);
	vfs_node_down(node);
	wakeup_all(&node->n_wait_queue);
	spinlock_unlock(&vfs_node_freelist.lock);

	return err;
}
