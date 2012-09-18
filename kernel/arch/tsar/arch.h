/*
 * arch.h - architecture specific implementation of kernel's HAL
 * (see kern/hal-arch.h)
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

#ifndef _ARCH_H_
#define _ARCH_H_

#include <config.h>
#include <types.h>

#ifndef _HAL_ARCH_H_
#error arch.h connot be included directly, use system.h instead
#endif

/* Thread structure size */
#undef  ARCH_THREAD_PAGE_ORDER
#define ARCH_THREAD_PAGE_ORDER CONFIG_THREAD_PAGE_ORDER

/* CPUs/CLUSTERs Numbers */
#define CACHE_LINE_SIZE      CONFIG_CACHE_LINE_SIZE

#undef CLUSTER_NR
#define CLUSTER_NR           CONFIG_MAX_CLUSTER_NR

#undef CPU_PER_CLUSTER
#define CPU_PER_CLUSTER      CONFIG_MAX_CPU_PER_CLUSTER_NR

#undef ARCH_HAS_BARRIERS
#define ARCH_HAS_BARRIERS    CONFIG_ARCH_HAS_BARRIERS

inline uint_t arch_onln_cpu_nr();
inline uint_t arch_onln_cluster_nr();

/* Timer parameters macros */
#undef CPU_CLOCK_TICK
#undef CYCLES_PER_SECOND
#undef MSEC_PER_TICK

#define CPU_CLOCK_TICK       1000000//80000
#define MSEC_PER_TICK        1
#define CYCLES_PER_SECOND    (1000*MSEC_PER_TICK*CPU_CLOCK_TICK)

/* Terminals related macros */
#define TTY_DEV_NR           CONFIG_TTY_MAX_DEV_NR
#define TTY_BUFFER_DEPTH     CONFIG_TTY_BUFFER_DEPTH

/* Structures definitions */
struct irq_action_s;
struct device_s;

struct arch_cpu_s
{
	struct irq_action_s *irq_vector[CONFIG_CPU_IRQ_NR];
};

struct arch_cluster_s
{
	struct device_s *xicu;
	struct device_s *dma;
};

static inline uint_t arch_cpu_gid(uint_t cid, uint_t cpu_per_cluster, uint_t lid)
{
	return (cid * cpu_per_cluster) + lid;
}

static inline uint_t arch_cpu_lid(uint_t cpu_gid, uint_t cpu_per_cluster)
{
	return (cpu_gid % cpu_per_cluster);
}

static inline uint_t arch_cpu_cid(uint_t cpu_gid, uint_t cpu_per_cluster)
{
	return (cpu_gid / cpu_per_cluster);
}



#endif	/* _ARCH_H_ */
