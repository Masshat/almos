/*
  This file is part of MutekP.
  
  MutekP is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  MutekP is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with MutekP; if not, write to the Free Software Foundation,
  Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
  
  UPMC / LIP6 / SOC (c) 2009
  Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
*/

#include <sys/types.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#ifndef CONFIG_LIBC_MALLOC_DEBUG
#define CONFIG_LIBC_MALLOC_DEBUG  0
#endif

typedef struct heap_manager_s
{
	pthread_spinlock_t lock;
	uint_t start;
	uint_t limit;
	uint_t current;
	uint_t next;
}heap_manager_t;

struct block_info_s
{
	uint_t busy:1;
	uint_t size:31;
	struct block_info_s *ptr;
} __attribute__ ((packed));

typedef struct block_info_s block_info_t;

static heap_manager_t heap_mgr;
extern uint_t __bss_end;
static int cacheline_size;
static int page_size;

#define ARROUND_UP(val, size) ((val) & ((size) -1)) ? ((val) & ~((size)-1)) + (size) : (val)

void __heap_manager_init(void)
{
	block_info_t *info;

	pthread_spin_init(&heap_mgr.lock, 0);
	heap_mgr.start = (uint_t)&__bss_end;
	heap_mgr.limit = heap_mgr.start + sysconf(_SC_HEAP_MAX_SIZE);

	info = (block_info_t*)heap_mgr.start;
	info->busy = 0;
	info->size = (char*)heap_mgr.limit - (char*)info;
	info->ptr = NULL;

#if CONFIG_LIBC_MALLOC_DEBUG
	printf("%s: start %x, info %x, size %d\n",
	       __FUNCTION__,
	       (unsigned)heap_mgr.start,
	       (unsigned)info,
	       ((block_info_t*)heap_mgr.start)->size);
#endif

	heap_mgr.current = heap_mgr.start;
	heap_mgr.next = heap_mgr.start;
	cacheline_size = sysconf(_SC_CACHE_LINE_SIZE);
	cacheline_size = (cacheline_size <= 0) ? 64 : cacheline_size;
	page_size = sysconf(_SC_PAGE_SIZE);
	page_size = (page_size <= 0) ? 4096 : page_size;
}

void* malloc(size_t size)
{
	block_info_t *current;
	block_info_t *next;
	register size_t effective_size;

	effective_size = size + sizeof(*current);
	effective_size = ARROUND_UP(effective_size, cacheline_size);

	pthread_spin_lock(&heap_mgr.lock);
  
	if(heap_mgr.next > (heap_mgr.limit - effective_size))
		current = (block_info_t*)heap_mgr.start;
	else
		current = (block_info_t*)heap_mgr.next;

#if CONFIG_LIBC_MALLOC_DEBUG
	int cpu;
	pthread_attr_getcpuid_np(&cpu);

	fprintf(stderr, "%s: cpu %d Started [sz %d, esz %d, pg %d, cl %d, start %x, next %x, current %x, busy %d, size %d]\n", 
		__FUNCTION__, 
		cpu, 
		size, 
		effective_size, 
		page_size,
		cacheline_size,
		(unsigned)heap_mgr.start,
		(unsigned)heap_mgr.next,
		(unsigned)current,
		current->busy,
		current->size);
#endif
    
	while(current->busy || (current->size < effective_size)) 
	{
		if((current->busy) && (current->size == 0))
			fprintf(stderr, "Corrupted memory block descriptor: @0x%x\n", (unsigned int)current);

		current = (block_info_t*) ((char*)current + current->size);
    
		if((uint_t)current >= heap_mgr.limit)
		{
			pthread_spin_unlock(&heap_mgr.lock);
#if 1//CONFIG_LIBC_MALLOC_DEBUG
			int cpu;
			pthread_attr_getcpuid_np(&cpu);

			fprintf(stderr, "[%s] cpu %d, tid %d, limit 0x%x, ended with ENOMEM\n", 
				__FUNCTION__, 
				cpu,
				(unsigned)pthread_self(),
				(unsigned)heap_mgr.limit);
#endif
			return NULL;
		}
	}

	if((current->size - effective_size) >= (uint_t)cacheline_size)
	{
		next          = (block_info_t*) ((char*)current + effective_size);
		next->size    = current->size - effective_size;
		next->busy    = 0;    
		heap_mgr.next = (uint_t) next;
		current->size = effective_size;
		current->busy = 1;
	}
	else
	{
		current->busy = 1;
	}

	current->ptr = NULL;

#if CONFIG_LIBC_MALLOC_DEBUG
	fprintf(stderr, "%s: cpu %d End [ 0x%x ]\n", __FUNCTION__, cpu, ((unsigned int)current) + sizeof(*current));
#endif

	pthread_spin_unlock(&heap_mgr.lock);
	return (char*)current + sizeof(*current);
}

void free(void *ptr)
{
	block_info_t *current;
	block_info_t *next;
  
	if(ptr == NULL)
		return;
  
	current = (block_info_t*) ((char*)ptr - sizeof(*current));
	current = (current->ptr != NULL) ? current->ptr : current;

	pthread_spin_lock(&heap_mgr.lock);
	current->busy = 0;

	while ((next = (block_info_t*) ((char*) current + current->size)))
	{ 
		if (((uint_t)next >= heap_mgr.limit) || (next->busy == 1))
			break;

		current->size += next->size;
	}

	if((uint_t)current < heap_mgr.next)
		heap_mgr.next = (uint_t) current;
  
	pthread_spin_unlock(&heap_mgr.lock);
}


void* realloc(void *ptr, size_t size)
{
	block_info_t *info;
	uint_t old_size;
	void* new_zone;
	void* block;

	if(!ptr)
		return malloc(size);

	if(!size)
	{
		free(ptr);
		return NULL;
	}

	/* Try to reuse cache lines */
	info = (block_info_t*)((char*)ptr - sizeof(*info));
	old_size = info->size; 
 
	if(size <= old_size)    
		return ptr;

	/* Allocate new block */
	if(info->ptr == NULL)
	{
		new_zone = malloc(size);
	}
	else
	{
		info = info->ptr;
		new_zone = valloc(size);
	}

	if(new_zone == NULL)
		return NULL;

	memcpy(new_zone, ptr, old_size);

	block = (char*)info + sizeof(*info);

	free(block);
	return new_zone;
}

void* calloc(size_t nmemb, size_t size)
{
	void *ptr = malloc(nmemb * size);

	if(ptr == NULL)
		return NULL;

	memset(ptr, 0, (nmemb * size));
  
	return ptr;
}

void* valloc(size_t size)
{
	void *ptr;
	uintptr_t block;
	block_info_t *info;
	size_t asked_size;
  
	asked_size = size;
	size  = ARROUND_UP(size, page_size);
	block = (uintptr_t) malloc(size + page_size);
  
	if(block == 0) return NULL;
  
	ptr   = (void*)((block + page_size) - (block % page_size));
	info  = (block_info_t*) ((char*)ptr - sizeof(*info));

	info->busy = 1;
	info->size = asked_size;
	info->ptr  = (block_info_t*)(block - sizeof(*info));

	return ptr;
}
