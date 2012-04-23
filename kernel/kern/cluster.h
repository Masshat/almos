/*
 * kern/cluster.h - Cluster-Manager Interface
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

#ifndef _CLUSTER_H_
#define _CLUSTER_H_

#include <config.h>
#include <system.h>
#include <types.h>
#include <spinlock.h>
#include <cpu.h>
#include <list.h>
#include <kmem.h>
#include <atomic.h>
#include <mcs_sync.h>
#include <metafs.h>
#include <ppm.h>
#include <kcm.h>
#include <heap_manager.h>

#define  CLUSTER_DOWN       0x00      
#define  CLUSTER_UP         0x01
#define  CLUSTER_STANDBY    0x02
#define  CLUSTER_CPU_ONLY   0x04
#define  CLUSTER_MEM_ONLY   0x08
#define  CLUSTER_IO         0x10

#define  CLUSTER_TOTAL_KEYS_NR (KMEM_TYPES_NR + CONFIG_CLUSTER_KEYS_NR)

struct task_s;
struct thread_s;
struct heap_manager_s;
struct dqdt_cluster_s;

struct cluster_s
{
	spinlock_t lock;
	atomic_t buffers_nr;
	atomic_t vfs_nodes_nr;
  
	/* Logical cluster */
	struct dqdt_cluster_s *levels_tbl[CONFIG_DQDT_LEVELS_NR];

	/* Physical Pages Manager */
	struct ppm_s ppm;

	/* Kernel Cache Manager */
	struct kcm_s kcm;

	/* Kernel Heap Manager */
	struct heap_manager_s khm;

	/* Cluster keys */
	void *keys_tbl[CLUSTER_TOTAL_KEYS_NR];

	/* Init Info */
	uint_t id;
	uint_t cpu_nr;
	uint_t onln_cpu_nr;
	uint_t clstr_nr;
	uint16_t x_coord;
	uint16_t y_coord;
	uint16_t z_coord;
	uint16_t chip_id;

	/* System Devices */
	struct list_entry devlist;

	/* Chains Info */
	struct list_entry list;
	struct list_entry rope;

	/* CPUs Info */
	struct cpu_s cpu_tbl[CPU_PER_CLUSTER];

	/* Sysfs information */
	char name[SYSFS_NAME_LEN];
	sysfs_entry_t node;

	/* Kernel Task */
	struct task_s *task;

	/* Manger Thread */
	struct thread_s *manager;
  
	/* Hardware related info */
	struct arch_cluster_s arch;

	/* Cluter BootStrap Processor */
	struct cpu_s *bscpu;
};

struct cluster_entry_s
{
	struct cluster_s *cluster;
	uint_t flags;
	atomic_t cntr;
	volatile uint_t boot_signal;
	uint_t vmem_start;
	uint_t vmem_limit;
};

extern struct cluster_entry_s clusters_tbl[CLUSTER_NR];

#define cluster_get_cpus_nr(cluster)

void clusters_init(void);
void clusters_sysfs_register(void);

error_t cluster_init(struct boot_info_s *info, 
		     uint_t start_paddr, 
		     uint_t limit_paddr,
		     uint_t begin_vaddr);

struct cluster_key_s;
typedef struct cluster_key_s ckey_t;

#define cluster_get_keyVal(key_addr)
error_t cluster_key_create(ckey_t *key);
void* cluster_getspecific(ckey_t *key);
error_t cluster_setspecific(ckey_t *key, void *val);
void cluster_key_delete(ckey_t *key);
void* cluster_manager_thread(void *arg);

/////////////////////////////////////////////
//              Private Section            //
/////////////////////////////////////////////
struct cluster_key_s
{
	uint_t val;
	uint_t pad[CONFIG_CACHE_LINE_LENGTH - 1];
}__attribute__ ((packed));

#undef cluster_get_keyVal
#define cluster_get_keyVal(_key) ((_key)->val)
/* Nota: set_id_by_name/get_id_by_name */

#undef cluster_get_cpus_nr
#define cluster_get_cpus_nr(_cluster) ((_cluster)->cpus_nr)
/////////////////////////////////////////////



#endif	/* _CLUSTER_H_ */
