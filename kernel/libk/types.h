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
  
   UPMC / LIP6 / SOC (c) 2007-2009
   Copyright Ghassan Almaless <ghassan.almaless@lip6.fr>
*/

#ifndef _TYPES_H_
#define _TYPES_H_

#include <config.h>
#include <stdint.h>

/* General constants and return values */
#ifndef NULL
#define NULL (void*)        0
#endif
#define FALSE               0
#define false               0
#define TRUE                1
#define true                1

#define RETURN_SUCCESS      0
#define RETURN_FAILURE      1
#define EXIT_SUCCESS        0
#define EXIT_FAILURE        1
#define SCOPE_SYS           1
#define SCOPE_USR           2

/* Pthread related constants */
#define PTHREAD_THREADS_MAX           2048
#define PTHREAD_KEYS_MAX              64
#define PTHREAD_DESTRUCTOR_ITERATIONS 32
#define PTHREAD_STACK_SIZE            64*4096
#define PTHREAD_STACK_MIN             4096
#define PTHREAD_BARRIER_SERIAL_THREAD 1
#define PTHREAD_PROCESS_PRIVATE       0
#define PTHREAD_PROCESS_SHARED        1
#define PTHREAD_CREATE_DETACHED       1
#define PTHREAD_CREATE_JOINABLE       0 
#define PTHREAD_EXPLICIT_SCHED        0
#define PTHREAD_INHERIT_SCHED         1
#define PTHREAD_SCOPE_SYSTEM          0
#define PTHREAD_SCOPE_PROCESS         1
#define SEM_VALUE_MAX                 PTHREAD_THREADS_MAX

/* Pthread related types */
typedef unsigned long pthread_t;
typedef unsigned long pthread_mutexattr_t;
typedef unsigned long pthread_barrier_t;
typedef unsigned long pthread_barrierattr_t;
typedef unsigned long sem_t;
typedef unsigned long pthread_cond_t;
typedef unsigned long pthread_condattr_t;
typedef unsigned long pthread_rwlock_t;
typedef unsigned long pthread_rwlockattr_t;
typedef unsigned long pthread_key_t;
typedef unsigned long uint_t;
typedef signed long sint_t;
typedef unsigned long bool_t;

typedef uint_t time_t;
typedef sint_t off_t;
typedef uint32_t fpos_t;
typedef signed long error_t;

typedef unsigned long pid_t;
typedef unsigned long uid_t;
typedef unsigned long gid_t;

/* Page Related Types */
typedef unsigned long ppn_t;
typedef unsigned long vma_t;
typedef unsigned long pma_t;

/* Time Related Types */
typedef uint64_t clock_t;

/* Aligned Variable */
struct cache_line_s
{
  union
  {
    uint_t values[CONFIG_CACHE_LINE_LENGTH];
    uint_t value;
  };
}__attribute__((packed));

typedef struct cache_line_s cacheline_t;
typedef struct cache_line_s global_t;

#define CACHELINE __attribute__((aligned(CONFIG_CACHE_LINE_LENGTH)))

#endif	/* _TYPES_H_ */
