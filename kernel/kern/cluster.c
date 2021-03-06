/*
 * kern/cluster.c - Cluster-Manger related operations
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
#include <types.h>
#include <errno.h>
#include <spinlock.h>
#include <cpu.h>
#include <scheduler.h>
#include <list.h>
#include <cluster.h>
#include <atomic.h>
#include <sysfs.h>
#include <boot-info.h>
#include <bits.h>
#include <pmm.h>
#include <thread.h>
#include <kmem.h>
#include <task.h>
#include <dqdt.h>

struct cluster_entry_s clusters_tbl[CLUSTER_NR];

void clusters_init(void)
{
	register uint_t i;

	for(i=0; i < CLUSTER_NR; i++)
	{
		clusters_tbl[i].cluster = NULL;
		clusters_tbl[i].flags = CLUSTER_DOWN;
		atomic_init(&clusters_tbl[i].cntr, 0);
		clusters_tbl[i].boot_signal = 0;
	}
}

void clusters_sysfs_register(void)
{
	register uint_t i;
	register struct cluster_entry_s *entry;
  
	for(i=0; i < CLUSTER_NR; i++)
	{
		entry = &clusters_tbl[i];
		if(entry->flags != CLUSTER_DOWN)
			sysfs_entry_register(&sysfs_root_entry, &entry->cluster->node);
	}
}

/* TODO: deal with case of cluster of CPU_ONLY or MEM_ONLY ressources */
error_t cluster_init(struct boot_info_s *info,
		     uint_t start_paddr, 
		     uint_t limit_paddr,
		     uint_t start_vaddr)
{
	uint_t cid;
	uint_t cpu;
	uint_t heap_start;
	uint_t begin_vaddr;
	struct cpu_s *cpu_info;
	struct cluster_s *cluster;
	struct cluster_entry_s *entry;
	struct thread_s *this;
	extern uint_t __heap_start;
	kmem_req_t req;

#if 0
	boot_dmsg("%s: cid %d: start_addr %x, limit_addr %x, start_vaddr %x\n",
		  __FUNCTION__, 
		  info->local_cluster_id, 
		  start_paddr, 
		  limit_paddr, 
		  start_vaddr);
#endif

	cid         = info->local_cluster_id;
	entry       = &clusters_tbl[cid];
	begin_vaddr = start_vaddr;
	start_vaddr = (cid == info->boot_cluster_id) ? (uint_t)&__heap_start : start_vaddr;
	cluster     = (struct cluster_s*) start_vaddr;

	/* As soon as possible we must intialize entry, current cpu & ids */
	entry->cluster    = cluster;
	this              = current_thread;
	cpu_info          = &cluster->cpu_tbl[info->local_cpu_id];
	cpu_info->cluster = cluster;

	thread_set_current_cpu(this,cpu_info);
  
	cluster->id       = cid;
	cluster->cpu_nr   = info->local_cpu_nr;
	cluster->onln_cpu_nr = info->local_onln_cpu_nr;
	cluster->bscpu    = cpu_info;
	/* ------------------------------------------------- */

	spinlock_init(&cluster->lock, "Cluster");
	atomic_init(&cluster->buffers_nr, 0);
	atomic_init(&cluster->vfs_nodes_nr, 0);
	memset(&cluster->levels_tbl[0], 0, sizeof(cluster->levels_tbl));
	memset(&cluster->keys_tbl[0], 0, sizeof(cluster->keys_tbl));
	cluster->next_key = KMEM_TYPES_NR;

	list_root_init(&cluster->devlist);
  
	start_vaddr += sizeof(*cluster);
	start_vaddr  = ARROUND_UP(start_vaddr, PMM_PAGE_SIZE);
	heap_start   = start_vaddr;
	start_vaddr += (1 << (CONFIG_KHEAP_ORDER + PMM_PAGE_SHIFT));
	heap_manager_init(&cluster->khm, heap_start, heap_start, start_vaddr);

	ppm_init(&cluster->ppm, 
		 start_paddr, 
		 limit_paddr, 
		 begin_vaddr, 
		 start_vaddr, 
		 info);
  
	kcm_init(&cluster->kcm, 
		 "KCM", 
		 sizeof(struct kcm_s), 
		 0, 1, 1, NULL, NULL, 
		 &kcm_page_alloc, &kcm_page_free);

	sprintk(cluster->name,
#if CONFIG_ROOTFS_IS_VFAT
		"CID%d"
#else
		"cid%d"
#endif
		,cid);

	sysfs_entry_init(&cluster->node, NULL, cluster->name);

	for(cpu = 0; cpu < cluster->cpu_nr; cpu++)
	{
		cpu_init(&cluster->cpu_tbl[cpu], 
			 cluster, 
			 cpu, 
			 arch_cpu_gid(cid, cluster->cpu_nr, cpu));
	}

	cluster->cpu_tbl[info->local_cpu_id].state = CPU_ACTIVE;

	if(cid == info->boot_cluster_id)
	{
		entry->flags = CLUSTER_UP | CLUSTER_IO;
		cluster->task = NULL;
	}
	else
	{
		entry->flags  = CLUSTER_UP;
		req.type      = KMEM_GENERIC;
		req.size      = sizeof(*cluster->task);
		req.flags     = AF_BOOT | AF_ZERO;
		cluster->task = kmem_alloc(&req);
    
		if(cluster->task == NULL)
		{
			boot_dmsg("%s: cpu %d, cid %d, failed to allocate task [ %u ]\n", 
				  __FUNCTION__,
				  cpu_get_id(),
				  cid,
				  cpu_time_stamp());

			return ENOMEM;
		}
	}
  
	cluster->manager = NULL;
	return 0;
}


error_t cluster_init_table(void)
{
	kmem_req_t req;
	struct cluster_s *cluster;
	struct page_s *page;
	uint_t i;

	req.flags = AF_BOOT;
	req.size  = CONFIG_GLOBAL_CLUSTERS_ORDER;
	req.type  = KMEM_PAGE;
	cluster   = current_cluster;

	page = kmem_alloc(&req);

	if(page == NULL)
		return ENOMEM;

	cluster->clusters_tbl = ppm_page2addr(page);

	for(i = 0; i < CLUSTER_NR; i++)
		cluster->clusters_tbl[i] = clusters_tbl[i].cluster;

	return 0;
}

error_t cluster_init_cores_table(void)
{
	kmem_req_t req;
	struct cluster_s *cluster;
	struct cluster_s *ptr;
	struct page_s *page;
	uint_t i, cpu;
	uint_t cores_per_cluster;

	req.flags = AF_BOOT;
	req.size  = CONFIG_GLOBAL_CLUSTERS_ORDER;
	req.type  = KMEM_PAGE;
	cluster   = current_cluster;

	page = kmem_alloc(&req);

	if(page == NULL)
		return ENOMEM;

	cluster->cores_tbl = ppm_page2addr(page);
	cores_per_cluster  = cluster->cpu_nr;

	for(i = 0; i < CLUSTER_NR; i++)
	{
		ptr = clusters_tbl[i].cluster;

		for(cpu = 0; ((ptr != NULL) && (cpu < cores_per_cluster)); cpu++)
			cluster->cores_tbl[(i * cores_per_cluster) + cpu] = &ptr->cpu_tbl[cpu];
	}

	return 0;
}

struct cluster_s* cluster_cid2ptr(uint_t cid)
{
	register struct cluster_s *cluster;
	register struct cluster_s *ptr;

	cluster = current_cluster;
	ptr     = cluster->clusters_tbl[cid];

	return ptr;
}

EVENT_HANDLER(manager_alarm_event_handler)
{
	struct thread_s *manager;
 
	manager = event_get_senderId(event);
 
	thread_preempt_disable(current_thread);

	//printk(INFO, "%s: cpu %d [%u]\n", __FUNCTION__, cpu_get_id(), cpu_time_stamp());

	sched_wakeup(manager);
  
	thread_preempt_enable(current_thread);

	return 0;
}

void* cluster_manager_thread(void *arg)
{
	register struct dqdt_cluster_s *root;
	register struct cluster_s *root_home;
	register uint_t tm_start;
	register uint_t tm_end;
	register uint_t cpu_id;
	struct cluster_s *cluster;
	struct thread_s *this;
	struct event_s event;
	struct alarm_info_s info;
	register uint_t cntr;
	register bool_t isRootMgr;
	register uint_t period;

	cpu_enable_all_irq(NULL);

	cluster   = arg;
	this      = current_thread;
	cpu_id    = cpu_get_id();
	root      = dqdt_root;
	root_home = dqdt_root->home;
	isRootMgr = (cluster == root_home) ? true : false;
	cntr      = 0;
	period    = (isRootMgr) ? 
		CONFIG_DQDT_ROOTMGR_PERIOD * MSEC_PER_TICK : 
		CONFIG_DQDT_MGR_PERIOD * MSEC_PER_TICK;

	event_set_senderId(&event, this);
	event_set_priority(&event, E_CHR);
	event_set_handler(&event, &manager_alarm_event_handler);
  
	info.event = &event;
	thread_preempt_disable(current_thread);

	while(1)
	{
		tm_start = cpu_time_stamp();
		dqdt_update();
		tm_end   = cpu_time_stamp();

		if(isRootMgr)
		{
			if((cntr % 10) == 0)
			{
				printk(INFO, "INFO: cpu %d, DQDT update ended [ %u - %u ]\n", 
				       cpu_id, 
				       tm_end, 
				       tm_end - tm_start);

				dqdt_print_summary(root);
			}
		}

		alarm_wait(&info, period);
		sched_sleep(this);
		cntr ++;
	}

	return NULL;
}


EVENT_HANDLER(cluster_key_create_event_handler)
{
	struct cluster_s *cluster;
	struct thread_s *sender;
	ckey_t *ckey;
	uint_t key;

	sender  = event_get_senderId(event);
	ckey    = event_get_argument(event);
	cluster = current_cluster;
	key     = cluster->next_key;

	while((key < CLUSTER_TOTAL_KEYS_NR) && (cluster->keys_tbl[key] != NULL))
		key ++;

	if(key < CLUSTER_TOTAL_KEYS_NR)
	{
		ckey->val = key;
		cluster->keys_tbl[key] = (void *) 0x1; /* Reserved */
		cluster->next_key = key;
		event_set_error(event, 0);
	}
	else
		event_set_error(event, ENOSPC);

	sched_wakeup(sender);
	return 0;
}

EVENT_HANDLER(cluster_key_delete_event_handler)
{
	struct cluster_s *cluster;
	struct thread_s *sender;
	ckey_t *ckey;
	uint_t key;

	sender  = event_get_senderId(event);
	ckey    = event_get_argument(event);
	cluster = current_cluster;
	key     = ckey->val;

	if(key < cluster->next_key)
		cluster->next_key = key;

	cluster->keys_tbl[key] = NULL;
	event_set_error(event, 0);

	sched_wakeup(sender);
	return 0;
}

#define _CKEY_CREATE  0x0
#define _CKEY_DELETE  0x1

error_t cluster_do_key_op(ckey_t *key, uint_t op)
{
	struct event_s event;
	struct thread_s *this;
	struct cluster_s *cluster;
	struct cpu_s *cpu;

	this = current_thread;

	event_set_priority(&event, E_FUNC);
	event_set_senderId(&event, this);
	event_set_argument(&event, key);

	if(op == _CKEY_CREATE)
		event_set_handler(&event, cluster_key_create_event_handler);
	else
		event_set_handler(&event, cluster_key_delete_event_handler);

	cluster = current_cluster;
	cpu     = cluster->bscluster->bscpu;
	event_send(&event, &cpu->re_listner);

	sched_sleep(this);

	return event_get_error(&event);
}

error_t cluster_key_create(ckey_t *key)
{
	return cluster_do_key_op(key, _CKEY_CREATE);
}

error_t cluster_key_delete(ckey_t *key)
{
	return cluster_do_key_op(key, _CKEY_DELETE);
}

void* cluster_getspecific(ckey_t *key)
{
	struct cluster_s *cluster;

	cluster = current_cluster;
	return cluster->keys_tbl[key->val];
}

void  cluster_setspecific(ckey_t *key, void *val)
{
	struct cluster_s *cluster;

	cluster = current_cluster;
	cluster->keys_tbl[key->val] = val;
}
