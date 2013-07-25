/*
 * arch_init.c - architecture intialization operations (see kern/hal-arch.h)
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
#include <list.h>
#include <bits.h>
#include <cpu.h>
#include <system.h>
#include <cluster.h>
#include <device.h>
#include <driver.h>
#include <thread.h>
#include <task.h>
#include <kmem.h>
#include <ppm.h>
#include <pmm.h>
#include <page.h>
#include <kdmsg.h>
#include <devfs.h>
#include <drvdb.h>
#include <boot-info.h>
#include <soclib_xicu.h>
#define _ARCH_BIB_SIGNATURE_
#include <arch-bib.h>

#define die(args...) do {boot_dmsg(args); while(1);} while(0)

#define ICU_MASK     0xFFFFFF  // interrupt enabled for frist 24 devices 
#define KATTR     (PMM_HUGE | PMM_READ | PMM_WRITE | PMM_EXECUTE | PMM_CACHED | PMM_GLOBAL | PMM_DIRTY | PMM_ACCESSED)
#define KDEV_ATTR (PMM_READ | PMM_WRITE | PMM_GLOBAL | PMM_DIRTY | PMM_ACCESSED)

extern kdmsg_channel_t kboot_tty;

typedef struct arch_bib_header_s  header_info_t;
typedef struct arch_bib_cluster_s cluster_info_t;
typedef struct arch_bib_device_s  dev_info_t;

/** Set XICU device's mask */
void arch_xicu_set_mask(struct cluster_s *cluster, struct device_s *icu);

/** Find the first device having DriverId drvid */
struct device_s* arch_dev_locate(struct list_entry *devlist_root, uint_t drvid);

/** Register all detected devices of given cluster */
void arch_dev_register(struct cluster_s *cluster);

/** Map given phyiscal region into kernel's virtual address space */
uint_t arch_region_map(uint_t vaddr, uint_t base, uint_t size, uint_t attr);

/** HAL-ARCH CPUs & Clusters counters */
static global_t arch_onln_cpus_nr;
static global_t arch_onln_clusters_nr;

/** Return Platform CPUs Online Number */
inline uint_t arch_onln_cpu_nr(void)
{
	return arch_onln_cpus_nr.value;
}

/** Return Platform Clsuters Online Number */
inline uint_t arch_onln_cluster_nr(void)
{
	return arch_onln_clusters_nr.value;
}

/** 
 * HAL-ARCH implementation: 
 * Build kernel virtual mapping of all hardware memory banks.
 * Initialize the boot table entries.
 */
void arch_memory_init(struct boot_info_s *info)
{
	struct task_s *task;
	struct thread_s *this;
	header_info_t *header;
	cluster_info_t *clusters;
	cluster_info_t *cluster_ptr;
	dev_info_t *bsclstr_dev_tbl;
	boot_tbl_entry_t *boot_tbl;
	dev_info_t *dev_tbl;
	uint_t vaddr_start;
	uint_t vaddr_limit;
	uint_t mem_start;
	uint_t mem_limit;
	uint_t clstr_nr;
	uint_t cid;
	uint_t size;
	uint_t i;

	header = (header_info_t*) info->arch_info;
    
	if(strncmp(header->signature, arch_bib_signature, 16))
	{
		while(1);			/* No other way to die !! */
	}
  
	kboot_tty.id = header->bootstrap_tty;

	if(strncmp(header->arch, "SOCLIB-TSAR", 16))
		die("ERROR: Unexpected Hardware Architecture, This Kernel Is Compiled For SOCLIB-TSAR Platform\n");

	clstr_nr = header->x_max * header->y_max;

	boot_dmsg("Initialization of %d/%d Online Clusters\t[ %d ]\n", info->onln_clstr_nr, clstr_nr, cpu_time_stamp());

	clusters        = (cluster_info_t*) ((uint_t)header + sizeof(header_info_t));
	cluster_ptr     = &clusters[info->boot_cluster_id];
	bsclstr_dev_tbl = (dev_info_t*)(cluster_ptr->offset + (uint_t)header);
	boot_tbl        = (boot_tbl_entry_t*) info->boot_tbl;
	cid             = cluster_ptr->cid;

	if((cid > CLUSTER_NR) || (info->onln_clstr_nr > CLUSTER_NR))
	{
		die("ERROR: %s: This Kernel Version Support Upto %d Clusters, onln clusters %d, cid %d\n", 
		    __FUNCTION__, CLUSTER_NR, info->onln_clstr_nr, cid);
	}

	size = bsclstr_dev_tbl[0].size;

	if(size == 0)
		die("ERROR: Unexpected Memory Size, cid %d\n", cid);

	this                         = current_thread;
	task                         = this->task;
	vaddr_start                  = task->vmm.limit_addr;
	vaddr_limit                  = vaddr_start + size;
	task->vmm.limit_addr         = vaddr_limit; //+ PMM_HUGE_PAGE_SIZE;
	boot_tbl[cid].pgdir          = info->boot_pgdir;
	boot_tbl[cid].reserved_end   = info->reserved_start;
	info->reserved_start        -= cluster_ptr->cpu_nr * PMM_PAGE_SIZE;
	boot_tbl[cid].reserved_start = info->reserved_start;
	clusters_tbl[cid].vmem_start = vaddr_start;
	clusters_tbl[cid].vmem_limit = vaddr_limit;
 
#if 1
	boot_dmsg("Mapping Region <0x%x - 0x%x> : <0x%x - 0x%x>\n", 
		  bsclstr_dev_tbl[0].base, 
		  bsclstr_dev_tbl[0].base + size,
		  vaddr_start, vaddr_limit);
#endif

	/* TODO: deal with offline clusters as well */
	for(i = 0; i < info->onln_clstr_nr; i++)
	{
		cluster_ptr = &clusters[i];
		cid = cluster_ptr->cid;
   
		if(cid >= CLUSTER_NR)
		{
			die("ERROR: %s: This Kernel Version Support Upto %d Clusters, found %d @0x%x, info @0x%x [ %d ]\n", 
			    __FUNCTION__, 
			    CLUSTER_NR, 
			    cid, 
			    cluster_ptr, 
			    header, 
			    i);
		}

		dev_tbl = (dev_info_t*)(cluster_ptr->offset + (uint_t)header);
		size = dev_tbl[0].size;

		if(size == 0)
			die("ERROR: This Kernel Version Do Not Support CPU-Only Clusters, cid %d\n", cid);

		if(cid == info->boot_cluster_id)
			continue;

		//size += PMM_HUGE_PAGE_SIZE;
		vaddr_start          = task->vmm.limit_addr;
		vaddr_limit          = vaddr_start + size;
		task->vmm.limit_addr = vaddr_limit;
		mem_start            = bsclstr_dev_tbl[0].base;
		mem_limit            = mem_start + bsclstr_dev_tbl[0].size;
      
		while((vaddr_start < mem_limit) && (vaddr_limit > mem_start))
		{
			vaddr_start          = task->vmm.limit_addr;
			vaddr_limit          = vaddr_start + size;
			task->vmm.limit_addr = vaddr_limit;
		}

		if(task->vmm.limit_addr >= CONFIG_DEVREGION_OFFSET)
			die("ERROR: %s: Kernel Virtual Address Space has been exceeded, cid %d, err 1\n", cid);

		mem_start = info->brom_start;
		mem_limit = info->brom_end;
    
		while((vaddr_start < mem_limit) && (vaddr_limit > mem_start))
		{
			vaddr_start          = task->vmm.limit_addr;
			vaddr_limit          = vaddr_start + size;
			task->vmm.limit_addr = vaddr_limit;
		}

		if(task->vmm.limit_addr >= CONFIG_DEVREGION_OFFSET)
			die("ERROR: %s: Kernel Virtual Address Space has been exceeded, cid %d, err 2\n", cid);
    
		arch_region_map(vaddr_start, dev_tbl[0].base, dev_tbl[0].size, KATTR);
    
		boot_tbl[cid].pgdir          = info->boot_pgdir;
		boot_tbl[cid].reserved_end   = vaddr_start + dev_tbl[0].size;
		boot_tbl[cid].reserved_start = boot_tbl[cid].reserved_end - (cluster_ptr->cpu_nr * PMM_PAGE_SIZE);
		clusters_tbl[cid].vmem_start = vaddr_start;
		clusters_tbl[cid].vmem_limit = vaddr_start + dev_tbl[0].size;
	}
}


/**
 * HAL-ARCH implementation: Initialize current
 * cluster and dynamicly detect its devices, 
 * associate them to approperiate drivers, 
 * and register them into the kernel.
 */
void arch_init(struct boot_info_s *info)
{
	register struct cluster_s *cluster;
	register uint_t dev_nr;
	register uint_t cid;
	register uint_t i;
	struct drvdb_entry_s *entry;
	struct task_s *task0;
	struct device_s *dev;
	header_info_t *header;
	cluster_info_t *clusters;
	cluster_info_t *cluster_ptr;
	boot_tbl_entry_t *boot_tbl;
	dev_info_t *dev_tbl;
	driver_t *driver;
	uint_t dev_base;
	error_t err;
	uint_t size;
	kmem_req_t req;
  
	task0       = current_task;
	header      = (header_info_t*) info->arch_info;
	clusters    = (cluster_info_t*) ((uint_t)header + sizeof(header_info_t));
	cluster_ptr = &clusters[info->local_cluster_id];
	dev_nr      = cluster_ptr->dev_nr;
	dev_tbl     = (dev_info_t*)(cluster_ptr->offset + (uint_t)header);
	boot_tbl    = (boot_tbl_entry_t*) info->boot_tbl;
	cid         = cluster_ptr->cid;

	if(cid > CLUSTER_NR)
		die("ERROR: This Kernel Version Support Upto %d Clusters, found cid %d\n", CLUSTER_NR, cid);

	if(cid == info->boot_cluster_id)
	{
		arch_onln_clusters_nr.value = header->onln_clstr_nr;
		arch_onln_cpus_nr.value     =  header->onln_cpu_nr;
	}

	err = cluster_init(info, 
			   dev_tbl[0].base, 
			   dev_tbl[0].base + dev_tbl[0].size,
			   clusters_tbl[cid].vmem_start);

	if(err) die("ERROR: Failed To Initialize Cluster %d, Err %d\n", cid, err);
  
	cluster           = clusters_tbl[cid].cluster;
	cluster->clstr_nr = info->onln_clstr_nr; // TODO: headr->x_max * header->y_max;
	req.type          = KMEM_GENERIC;
	req.size          = sizeof(*dev);
	req.flags         = AF_BOOT | AF_ZERO;

	for(i=1; i < dev_nr; i++)
	{
		size = ARROUND_UP(dev_tbl[i].size, PMM_PAGE_SIZE);

#if CONFIG_XICU_USR_ACCESS
		if(dev_tbl[i].id == SOCLIB_XICU_ID)
			size += PMM_PAGE_SIZE;
#endif

		dev_base = (uint_t)cpu_atomic_add(&task0->vmm.devreg_addr, size);

		if(dev_base > CONFIG_KERNEL_LIMIT)
			die("ERROR: %s: Kernel Virtual Address Space has been exceeded, cid %d, dev %d\n", cid, i);

		arch_region_map(dev_base, dev_tbl[i].base, size, KDEV_ATTR);
		dev_base += (dev_tbl[i].base & PMM_PAGE_MASK);
    
		if((dev = kmem_alloc(&req)) == NULL)
			die("ERROR: Failed To Allocate Device [Cluster %d, Dev %d]\n",cid,i);
      
		if((entry = drvdb_locate_byId(dev_tbl[i].id)) == NULL)
			die("ERROR: Unknown Device [Cluster %d, Dev %d, Devid %d]\n",cid,i,dev_tbl[i].id);

		dev->base_paddr = (void*)dev_tbl[i].base;
		driver          = drvdb_entry_get_driver(entry);

		if((err=driver->init(dev, (void*)dev_base, dev_tbl[i].size, dev_tbl[i].irq)))
			die("ERROR: Failed To Initialize Device %s [Cluster %d, Dev %d, Err %d]\n",
			    drvdb_entry_get_name(entry), cid, i, err);
      
		list_add_last(&cluster->devlist, &dev->list);
  
#if 1
		boot_dmsg("Found Device: %s (%s)\nBase <0x%x> Size <0x%x> Irq <%d>\n",
			  drvdb_entry_get_name(entry),
			  drvdb_entry_get_info(entry),
			  dev_base,
			  dev_tbl[i].size,
			  dev_tbl[i].irq);
#endif
	}

	arch_dev_register(cluster);
}

uint_t arch_region_map(uint_t vaddr, uint_t base, uint_t size, uint_t attr)
{
	register error_t err;
	register uint_t limit;
	register struct task_s *task;
	register uint_t page_size;
	pmm_page_info_t info;

	task      = current_task;
	limit     = base + size;
	page_size = (attr & PMM_HUGE) ? PMM_HUGE_PAGE_SIZE : PMM_PAGE_SIZE;

#if 1
	boot_dmsg("Mapping Region <0x%x - 0x%x> -> <0x%x - 0x%x>\n", 
		  base, limit, vaddr, vaddr + size);
#endif

	info.cluster = NULL;
	cpu_spinlock_lock(&task->lock);

	while(base < limit)
	{
		info.attr = attr;
		info.ppn  = base >> PMM_PAGE_SHIFT;

		if((err = pmm_set_page(&task->vmm.pmm, vaddr, &info)))
			die("ERROR: Mapping region is failed [vaddr 0x%x, ppn 0x%x]\n", vaddr, base);
    
		vaddr += page_size;
		base  += page_size;
	}

	cpu_spinlock_unlock(&task->lock);
	return vaddr;
}

struct device_s* arch_dev_locate(struct list_entry *devlist_root, uint_t drvid)
{
	struct list_entry *iter;
	struct device_s *dev;
  
	list_foreach(devlist_root, iter)
	{
		dev = list_element(iter, struct device_s, list);
		if(dev->op.drvid == drvid)
			return dev;
	}
  
	return NULL;
}

void arch_dev_register(struct cluster_s *cluster)
{
	struct device_s *xicu;
	struct list_entry *iter;
	struct device_s *dev;
	struct cpu_s *cpu_ptr;
	uint_t cpu;
	error_t err;

	xicu = arch_dev_locate(&cluster->devlist, SOCLIB_XICU_ID);
	dev  = arch_dev_locate(&cluster->devlist, SOCLIB_DMA_ID);

	cluster->arch.xicu = xicu;
	cluster->arch.dma  = dev;

	if(xicu == NULL)
		die("ERROR: No XICU Is Found for Cluster %d\n", cluster->id);
  
	list_foreach(&cluster->devlist, iter)
	{
		dev = list_element(iter, struct device_s, list);

		if((dev != xicu) && (dev->irq != -1))
		{
			err = xicu->op.icu.bind(xicu, dev);
      
			if(err) 
				die("ERROR: Failed to bind device %s, irq %d, on xicu %s @%x [ err %d ]\n", 
				    dev->name, dev->irq, xicu->name, xicu, err); 
		}

		devfs_register(dev);
	}

	arch_xicu_set_mask(cluster, xicu);

	for(cpu = 0; cpu < cluster->cpu_nr; cpu++)
	{
		cpu_ptr = &cluster->cpu_tbl[cpu];
		arch_cpu_set_irq_entry(cpu_ptr, 0, &xicu->action);
	}
}

void arch_xicu_set_mask(struct cluster_s *cluster, struct device_s *icu)
{
	icu->op.icu.set_mask(icu, ICU_MASK, XICU_HWI_TYPE, 0);
	icu->op.icu.set_mask(icu, 0, XICU_HWI_TYPE, 1);
	icu->op.icu.set_mask(icu, 0, XICU_HWI_TYPE, 2);
	icu->op.icu.set_mask(icu, 0, XICU_HWI_TYPE, 3);

#if ARCH_HAS_BARRIERS
	if(cluster->cpu_nr > 1)
	{
		icu->op.icu.set_mask(icu, XICU_CNTR_MASK, XICU_CNTR_TYPE, 1);
		icu->op.icu.set_mask(icu, 0, XICU_CNTR_TYPE, 0);
	}
	else
	{
		icu->op.icu.set_mask(icu, XICU_CNTR_MASK, XICU_CNTR_TYPE, 0);
		icu->op.icu.set_mask(icu, 0, XICU_CNTR_TYPE, 1);
	}
  
	icu->op.icu.set_mask(icu, 0, XICU_CNTR_TYPE, 2);
	icu->op.icu.set_mask(icu, 0, XICU_CNTR_TYPE, 3);
#endif	/* ARCH_HAS_BARRIERS */

	icu->op.icu.set_mask(icu, 1, XICU_PTI_TYPE, 0);
	icu->op.icu.set_mask(icu, 2, XICU_PTI_TYPE, 1);
	icu->op.icu.set_mask(icu, 4, XICU_PTI_TYPE, 2);
	icu->op.icu.set_mask(icu, 8, XICU_PTI_TYPE, 3);
  
	icu->op.icu.set_mask(icu, 1, XICU_WTI_TYPE, 0);
	icu->op.icu.set_mask(icu, 2, XICU_WTI_TYPE, 1);
	icu->op.icu.set_mask(icu, 4, XICU_WTI_TYPE, 2);
	icu->op.icu.set_mask(icu, 8, XICU_WTI_TYPE, 3);
}
