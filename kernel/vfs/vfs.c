/*
 * vfs/vfs.c - Virtual File System services
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

#include <scheduler.h>
#include <rwlock.h>
#include <string.h>
#include <stdint.h>
#include <vfs.h>
#include <vfs-private.h>
#include <thread.h>
#include <task.h>
#include <page.h>
#include <ppm.h>
#include <cpu-trace.h>
#include <kmem.h>

#if (VFS_MAX_PATH > PMM_PAGE_SIZE)
#error VFS_MAX_PATH must be less or equal to page size
#endif

struct vfs_context_s *vfs_pipe_ctx;

static uint_t vfs_dir_count(char *path) 
{
	uint_t count   = 0;
	char *path_ptr = path;

	while((path_ptr = strchr(path_ptr, '/')) != NULL) 
	{
		path_ptr ++;
		count    ++;
	}

	return (count == 0) ? 1 : count;
}


static void vfs_split_path(char *path, char **dirs) 
{
	uint_t i=0;
	char *path_ptr;

	path_ptr = path;
	dirs[0]  = path_ptr;
	dirs[1]  = NULL;

	if((path_ptr[0] == '/') && (path_ptr[1] == '\0'))
		return;

	path_ptr = (path_ptr[0] == '/') ? path_ptr + 1 : path_ptr;
	dirs[0]  = path_ptr;
	i++;

	while((path_ptr = strchr(path_ptr, '/')) != NULL) 
	{
		*path_ptr = 0;
		dirs[i++] = ++path_ptr;
	}

	dirs[i] = NULL;
}

error_t vfs_get_path(char *u_path, struct page_s **k_path_pg, char **k_path, uint_t *len)
{
	kmem_req_t req;
	struct page_s *page;
	char *path;
	uint_t count;
	error_t err;

	req.type  = KMEM_PAGE;
	req.size  = 0;
	req.flags = AF_KERNEL; 
	count     = 0;
	page      = kmem_alloc(&req);
  
	if(page == NULL) 
		return ENOMEM;
  
	path = ppm_page2addr(page);
	err  = cpu_uspace_strlen(u_path, &count);
  
	if(err) goto fail_len;
  
	if(count > VFS_MAX_PATH)
	{
		err = ERANGE;
		goto fail_rng;
	}

	err = cpu_uspace_copy(path, u_path, count);
  
	if(err) goto fail_cpy;

	path[count] = 0;

	if(len != NULL)
		*len = count;

	vfs_dmsg(1, "%s: path [%s]\n", __FUNCTION__, path);
	*k_path_pg = page;
	*k_path    = path;
	return 0;

fail_cpy:
fail_len:
fail_rng:
	req.ptr = page;
	kmem_free(&req);
	return err;
}

error_t vfs_put_path(struct page_s *page)
{
	kmem_req_t req;
  
	req.type = KMEM_PAGE;
	req.ptr  = page;

	kmem_free(&req);
	return 0;
}

error_t vfs_stat(struct vfs_node_s *cwd, char *path, struct vfs_node_s **node)
{
	struct page_s *path_pg;
	bool_t isAbsolutePath;
	bool_t isByPath;
	char *str;
	error_t err;

	isByPath = (*node == NULL);

	vfs_dmsg(1,"%s: called, isByPath %d\n", __FUNCTION__, isByPath);

	if(isByPath)
	{
		err = vfs_get_path(path, &path_pg, &str, NULL);

		if(err) return err;

		char *dirs_ptr[vfs_dir_count(str) + 1];

		vfs_split_path(str,dirs_ptr);

		isAbsolutePath = (str[0] == '/') ? 1 : 0 ;
 
		err = vfs_node_load(cwd, dirs_ptr, 0, isAbsolutePath, node);

		vfs_put_path(path_pg);

		if(err) return err;
	}
  
	if((*node)->n_op->stat == NULL)
		return ENOSYS;

	err = (*node)->n_op->stat(*node);
  
	if(isByPath)
		vfs_node_down_atomic(*node);

	return err;
}


error_t vfs_open(struct vfs_node_s *cwd,
		 char *path,
		 uint_t flags,
		 uint_t mode,
		 struct vfs_file_s **file) 
{
	struct vfs_node_s *node;
	struct vfs_file_s *file_ptr;
	struct page_s *path_pg;
	char *str;
	error_t err;
	uint_t isAbsolutePath;
	kmem_req_t req;

	cpu_trace_write(current_thread()->local_cpu, vfs_open);

	file_ptr = NULL;

	err = vfs_get_path(path, &path_pg, &str, NULL);

	if(err) return err;

	char *dirs_ptr[vfs_dir_count(str) + 1];

	vfs_split_path(str,dirs_ptr);

	isAbsolutePath = (str[0] == '/') ? 1 : 0 ;
  
	err = vfs_node_load(cwd,dirs_ptr, flags, isAbsolutePath, &node);

	vfs_put_path(path_pg);
  
	if(err) return err;

	if((flags & VFS_DIR) && !(node->n_attr & VFS_DIR))
	{
		err = ENOTDIR;
		goto VFS_OPEN_ERROR;
	}

	if(VFS_IS(flags, VFS_O_TRUNC))
	{
		err = vfs_node_trunc(node);
		if(err) return err;
	}

	if((file_ptr = vfs_file_get(node)) == NULL)
		goto VFS_OPEN_ERROR;

	file_ptr->f_flags = flags & 0xFFFF0000;
	file_ptr->f_mode  = mode;

	if((err = file_ptr->f_op->open(node,file_ptr)))
		goto VFS_OPEN_ERROR;

	if(VFS_IS(flags,VFS_O_APPEND))
		if((err = vfs_lseek(file_ptr,0,VFS_SEEK_END, NULL)))
			goto VFS_OPEN_ERROR;

	if(VFS_IS(file_ptr->f_node->n_attr, VFS_DEV_BLK | VFS_DEV_CHR))
		VFS_SET(file_ptr->f_flags, VFS_O_DEV);

	vfs_dmsg(1, "%s: node [%s], size %d\n",
		 __FUNCTION__,
		 file_ptr->f_node->n_name,
		 (uint32_t)file_ptr->f_node->n_size);

	*file = file_ptr;
	return 0;

VFS_OPEN_ERROR:
	vfs_dmsg(1,"[ %x :: %d] vfs_open : error while doing open, code %d\n",
		 current_thread, current_thread->lcpu->gid, err);

	spinlock_lock(&vfs_node_freelist.lock);
	vfs_node_down(node);
	spinlock_unlock(&vfs_node_freelist.lock);

	if(file_ptr == NULL)
		return ENOMEM;

	file_ptr->f_op->release(file_ptr);

	req.type = KMEM_VFS_FILE;
	req.ptr  = file_ptr;
	kmem_free(&req);
	return err;
}

error_t vfs_chdir(char *pathname, struct vfs_node_s *cwd, struct vfs_node_s **new_cwd) 
{
	struct vfs_node_s *node;
	struct page_s *path_pg;
	uint_t flags;
	error_t err;
	char *str;
	uint_t isAbsolutePath;

	cpu_trace_write(current_thread()->local_cpu, vfs_chdir);

	err = vfs_get_path(pathname, &path_pg, &str, NULL);

	if(err) return err;

	char *dirs_ptr[vfs_dir_count(str) + 1];

	vfs_split_path(str,dirs_ptr);

	flags = VFS_DIR;

	isAbsolutePath = (str[0] == '/') ? 1 : 0 ;
	err = vfs_node_load(cwd,dirs_ptr, flags, isAbsolutePath, &node);

	vfs_put_path(path_pg);

	if(err) return err;

	spinlock_lock(&vfs_node_freelist.lock);
	vfs_node_down(cwd);
	spinlock_unlock(&vfs_node_freelist.lock);

	*new_cwd = node;
	return 0;
}


error_t vfs_pipe(struct vfs_file_s *pipefd[2]) {
#ifdef CONFIG_DRIVER_FS_PIPE
	/* FIXME: CODE HERE IS SOMEHOW OBSOLET */
	struct vfs_node_s *node;
	struct vfs_file_s *fd_in;
	struct vfs_file_s *fd_out;

	spinlock_lock(&vfs_node_freelist.lock);

	if((node = vfs_node_freelist_get(vfs_pipe_ctx)) == NULL)
	{
		spinlock_unlock(&vfs_node_freelist.lock);
		return ENOMEM;
	}

	spinlock_unlock(&vfs_node_freelist.lock);

	VFS_SET(node->n_attr,VFS_PIPE);

	vfs_dmsg(1,"vfs_pipe: got a node, do n_op->init(node)\n");
	vfs_dmsg(1,"vfs_pipe: do n_op->init(node) OK\n");

	vfs_node_up(node);
	vfs_node_up(node);

	node->n_readers = 1;
	node->n_writers = 1;

	if((fd_in = vfs_file_get(node)) == NULL)
		goto VFS_PIPE_ERROR_FILE;

	if((fd_out = vfs_file_freelist_get(node)) == NULL) {
		vfs_file_freelist_add(fd_in);
		goto VFS_PIPE_ERROR_FILE;
	}

	VFS_SET(fd_in->f_flags, VFS_O_PIPE | VFS_O_WRONLY);
	VFS_SET(fd_out->f_flags,VFS_O_PIPE | VFS_O_RDONLY);
	pipefd[0] = fd_out;
	pipefd[1] = fd_in;
	return 0;

VFS_PIPE_ERROR_FILE:
	spinlock_lock(&vfs_node_freelist.lock);
	vfs_node_freelist_add(node,1);
	spinlock_unlock(&vfs_node_freelist.lock);
	return ENOMEM;

#else
	return ENOTSUPPORTED;
#endif
}


error_t vfs_mkfifo(struct vfs_node_s *cwd, char *pathname, uint_t mode) {
#ifdef CONFIG_DRIVER_FS_PIPE
	struct vfs_node_s *node;
	uint_t flags;
	error_t err;
	char str[VFS_MAX_PATH];
	char *dirs_ptr[vfs_dir_count(pathname) + 1];
	uint_t isAbsolutePath;

	err   = 0;
	flags = 0;
	VFS_SET(flags, VFS_O_CREATE | VFS_O_EXCL | VFS_FIFO);
	strcpy(str,pathname);
	vfs_split_path(str,dirs_ptr);
	isAbsolutePath = (pathname[0] == '/') ? 1 : 0 ;

	if((err = vfs_node_load(cwd,dirs_ptr, flags, isAbsolutePath, &node)))
		return err;

	spinlcok_lock(&vfs_node_freelist.lock);
	vfs_node_down(node);
	spinlock_unlock(&vfs_node_freelist.lock);
	return 0;
#else
	return ENOTSUPPORTED;
#endif
}

error_t vfs_unlink(struct vfs_node_s *cwd, char *pathname) 
{
	struct page_s *path_pg;
	struct vfs_node_s *node;
	struct vfs_node_s *parent;
	uint_t isAbsolutePath;
	char *str;
	error_t err;

	cpu_trace_write(current_thread()->local_cpu, vfs_unlink);

	err = vfs_get_path(pathname, &path_pg, &str, NULL);

	if(err) return err;

	char *dirs_ptr[vfs_dir_count(str) + 1];

	vfs_split_path(str,dirs_ptr);

	isAbsolutePath = (str[0] == '/') ? 1 : 0 ;
	vfs_dmsg(1,"vfs_unlink started\n");

	err = vfs_node_load(cwd,dirs_ptr, 0, isAbsolutePath, &node);

	vfs_put_path(path_pg);

	if(err) return err;

	if(VFS_IS(node->n_attr,VFS_DIR))
	{
		err = EISDIR;
		goto VFS_UNLINK_ERROR;
	}

	parent = node->n_parent;
	spinlock_lock(&vfs_node_freelist.lock);
	node->n_links --;
  
	if(node->n_links == 0) 
	{
		VFS_SET(parent->n_state,VFS_INLOAD);
		metafs_unregister(&parent->n_meta, &node->n_meta);

		vfs_dmsg(1,"vfs_unlink: parent's (%s) state is set to INLOAD, node %s  detached from its father's list\n",
			 parent->n_name,
			 node->n_name);

		spinlock_unlock(&vfs_node_freelist.lock);

		if((err=parent->n_op->unlink(node)))
			goto VFS_UNLINK_ERROR;

		vfs_dmsg(1,"vfs_unlink: node %s has been removed from its parent's children list\n", 
			 node->n_name);

		spinlock_lock(&vfs_node_freelist.lock);
		VFS_CLEAR(parent->n_state,VFS_INLOAD);
		wakeup_all(&node->n_wait_queue);
	}

	vfs_node_down(node);
	spinlock_unlock(&vfs_node_freelist.lock);
	return 0;

VFS_UNLINK_ERROR:
	spinlock_lock(&vfs_node_freelist.lock);

	if(err != EISDIR)
	{
		VFS_CLEAR(node->n_parent->n_state, VFS_INLOAD);
		wakeup_all(&node->n_wait_queue);
	}

	vfs_node_down(node);
	spinlock_unlock(&vfs_node_freelist.lock);
	return err;
}

error_t vfs_close(struct vfs_file_s *file, uint_t *refcount) 
{
	kmem_req_t req;
	uint_t count;
	error_t err;

	cpu_trace_write(current_thread()->local_cpu, vfs_close);

	assert(file != NULL);

	/* TODO: THIS TREATEMENT MUST BE MOVED TO CLOSE EVENT OF FIFO FILE-SYSTEM */
#if 0
	if(VFS_IS(file->f_flags, VFS_O_PIPE)) 
	{
		if(VFS_IS(file->f_flags, VFS_O_RDONLY))
			file->f_node->n_readers --;

		if(VFS_IS(file->f_flags, VFS_O_WRONLY))
			file->f_node->n_writers --;
	}
#endif

	count = atomic_add(&file->f_count, -1);
	*refcount = count;
  
	if(count > 1) return 0;

	vfs_dmsg(1, "%s: cpu %d, pid %d, closing [%s]\n", 
		 __FUNCTION__,
		 cpu_get_id(),
		 current_task->pid,
		 file->f_node->n_name);

	if(file->f_node != NULL)
	{
		spinlock_lock(&vfs_node_freelist.lock);
		vfs_node_down(file->f_node);
		spinlock_unlock(&vfs_node_freelist.lock);
	}

	if((err=file->f_op->close(file)))
		return err;

	file->f_op->release(file);
  
	req.type = KMEM_VFS_FILE;
	req.ptr  = file;
	kmem_free(&req);
	return 0;
}

error_t vfs_closedir(struct vfs_file_s *file, uint_t *refcount) 
{
	if(!(VFS_IS(file->f_flags, VFS_O_DIRECTORY)))
		return EBADF;

	return vfs_close(file, refcount);
}

error_t vfs_create(struct vfs_node_s *cwd,
		   char *path,
		   uint_t flags,
		   uint_t mode,
		   struct vfs_file_s **file) 
{
	flags &= 0xFFF80000;
	VFS_SET(flags,VFS_O_CREATE);
	return vfs_open(cwd,path,flags,mode,file);
}

error_t vfs_mkdir(struct vfs_node_s *cwd, char *pathname, uint_t mode) 
{
	struct vfs_file_s *file;
	uint_t flags;
	error_t err;
	uint_t count;

	cpu_trace_write(current_thread()->local_cpu, vfs_mkdir);
	flags = 0;
	VFS_SET(flags,VFS_O_DIRECTORY | VFS_O_CREATE | VFS_O_EXCL | VFS_DIR);

	if((err=vfs_open(cwd,pathname,flags,mode,&file)))
		return err;

	return vfs_close(file, &count);
}

error_t vfs_opendir(struct vfs_node_s *cwd, char *path, uint_t mode, struct vfs_file_s **file) 
{
	error_t err;
	uint_t flags;

	cpu_trace_write(current_thread()->local_cpu, vfs_opendir);

	err   = 0;
	flags = 0x00;
	VFS_SET(flags, VFS_DIR);

	if((err = vfs_open(cwd,path,flags,mode,file)))
		return err;

	(*file)->f_offset = 0;
	VFS_SET((*file)->f_flags,VFS_O_DIRECTORY);
	return 0;
}

error_t vfs_readdir(struct vfs_file_s *file, struct vfs_dirent_s *dirent) 
{
	error_t err = 0;

	cpu_trace_write(current_thread()->local_cpu, vfs_readdir);

	if(!(VFS_IS(file->f_flags, VFS_O_DIRECTORY)))
		return EBADF;

	rwlock_wrlock(&file->f_rwlock);
	err = file->f_op->readdir(file,dirent);
	rwlock_unlock(&file->f_rwlock);
	return err;
}

ssize_t vfs_read(struct vfs_file_s *file, uint8_t *buffer, size_t count) 
{
	size_t available_size;
	size_t size_to_read;
	ssize_t size;

	cpu_trace_write(current_thread()->local_cpu, vfs_read);

	if(VFS_IS(file->f_flags,VFS_O_DIRECTORY))
		return -EISDIR;

	if(!(VFS_IS(file->f_flags,VFS_O_RDONLY)))
		return -EBADF;

	rwlock_wrlock(&file->f_rwlock);
	rwlock_rdlock(&file->f_node->n_rwlock);

	available_size = file->f_node->n_size - file->f_offset;
#if 0
	size_to_read = (count >= available_size) ?  available_size : count;
#else
	size_to_read = (VFS_IS(file->f_flags,VFS_O_PIPE | VFS_O_DEV)) ? count : (count >= available_size) ? available_size : count;
#endif

	if((size = file->f_op->read(file,buffer,size_to_read)) < 0)
		goto VFS_READ_ERROR;

	vfs_dmsg(1,"vfs_read: n_size %d, count %d, available_size %d, size_to_read %d, read %d\n",
		 file->f_node->n_size, count, available_size, size_to_read, size);

	file->f_offset += size;

VFS_READ_ERROR:
	rwlock_unlock(&file->f_node->n_rwlock);
	rwlock_unlock(&file->f_rwlock);
	return size;
}


ssize_t vfs_write (struct vfs_file_s *file, uint8_t *buffer, size_t count) 
{
	ssize_t size;
	uint_t hasToLock;

	cpu_trace_write(current_thread()->local_cpu, vfs_write);

	if(VFS_IS(file->f_flags,VFS_O_DIRECTORY))
		return EINVAL;

	if(!(VFS_IS(file->f_flags,VFS_O_WRONLY)))
		return EBADF;

	hasToLock = 0;
	rwlock_wrlock(&file->f_rwlock);
	rwlock_rdlock(&file->f_node->n_rwlock);

	/* TODO: Move VFS-locking-policies to per-file-system local policies  */
	if(!(VFS_IS(file->f_flags, VFS_O_DEV)) && ((count + file->f_offset) > file->f_node->n_size))
	{
		hasToLock = 1;
		rwlock_unlock(&file->f_node->n_rwlock);
		rwlock_wrlock(&file->f_node->n_rwlock);
	}

	if((size = file->f_op->write(file,buffer,count)) < 0)
		goto VFS_WRITE_ERROR;

	file->f_offset += size;

	vfs_dmsg(1,"vfs_write: %d has been wrote, f_offset %d, n_size %d, hasToLock %d\n",
		 size, file->f_offset,
		 file->f_node->n_size,
		 hasToLock);

	if(hasToLock) 
	{
		if(file->f_offset > file->f_node->n_size)
		{
			file->f_node->n_size = file->f_offset;

			if(VFS_IS(file->f_flags, VFS_O_SYNC))
			{
				vfs_dmsg(1,"%s: node size %d, sync to disk\n", 
					 __FUNCTION__,
					 file->f_node->n_size);
				/* 
				 * No need to lock node_freelist & change node's state to INLOAD 
				 * as node count is at least 1 _and_ node wrlock is taken 
				 */
				file->f_node->n_op->write(file->f_node);
			}
			else
			{
				vfs_dmsg(1,"%s: node size %d, set it to DIRTY\n", 
					 __FUNCTION__,
					 file->f_node->n_size);

				/*
				 * No need to lock node_freelist as node count is at least 1 _and_ 
				 * node wrlock is taken 
				 */
				VFS_SET(file->f_node->n_state, VFS_DIRTY);
			}
		}
	}

VFS_WRITE_ERROR:
	rwlock_unlock(&file->f_node->n_rwlock);
	rwlock_unlock(&file->f_rwlock);
	return size;
}


error_t vfs_lseek(struct vfs_file_s *file, size_t offset, uint_t whence, size_t *new_offset_ptr) 
{
	size_t old_offset;
	uint64_t new_offset;
	error_t err;
	uint_t hasToLock;
	size_t node_size;

	cpu_trace_write(current_thread()->local_cpu, vfs_lseek);

	if(VFS_IS(file->f_flags,VFS_O_DIRECTORY))
		return EBADF;

	err = 0;
	hasToLock  = 0;
	old_offset = file->f_offset;
	new_offset = file->f_offset;
#if 0
	if((offset == 0) && (whence == VFS_SEEK_CUR))
	{
		if(new_offset_ptr != NULL)
			*new_offset_ptr = old_offset;

		return 0;
	}
#endif
	rwlock_wrlock(&file->f_rwlock);
	rwlock_rdlock(&file->f_node->n_rwlock);
	node_size = file->f_node->n_size;

	vfs_dmsg(1,"vfs_lseek: started asked size %d\n",offset);

	switch(whence) 
	{
	case VFS_SEEK_SET:

		if(offset == old_offset)	/* Nothing to do ! */
			goto VFS_LSEEK_ERROR;

		new_offset = offset;
		break;

	case VFS_SEEK_CUR:
		new_offset += offset;
		break;

	case VFS_SEEK_END:
		new_offset = node_size + offset;
		break;

	default:
		err = EINVAL;
		goto VFS_LSEEK_ERROR;
	}

	if( new_offset >= UINT32_MAX) 
	{
		err = EOVERFLOW;
		goto VFS_LSEEK_ERROR;
	}

	if(new_offset > node_size) 
	{
		hasToLock = 1;
		rwlock_unlock(&file->f_node->n_rwlock);
		rwlock_wrlock(&file->f_node->n_rwlock);
	}

	file->f_offset = (size_t) new_offset;

	if((err=file->f_op->lseek(file))) 
	{
		err = -err;
		file->f_offset = old_offset;
		goto VFS_LSEEK_ERROR;
	}

	if(new_offset_ptr)
		*new_offset_ptr = new_offset;

	if(hasToLock) 
	{
		if(new_offset > file->f_node->n_size)
			file->f_node->n_size = (size_t)new_offset;

		VFS_SET(file->f_node->n_state,VFS_DIRTY);
	}

VFS_LSEEK_ERROR:
	rwlock_unlock(&file->f_node->n_rwlock);
	rwlock_unlock(&file->f_rwlock);
	return err;
}
