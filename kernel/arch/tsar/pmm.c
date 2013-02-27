/*
 * pmm.c - architecture related implementation of pmm.h interface
 * (see mm/pmm.h)
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

#include <errno.h>
#include <bits.h>
#include <task.h>
#include <kmem.h>
#include <thread.h>
#include <cluster.h>
#include <ppm.h>
#include <page.h>
#include <pmm.h>
#include <system.h>

#define _PMM_INIT      0x001
#define _PMM_VALID     0x002

#define PMM_DEBUG no

struct page_s* pmm_alloc_pages(struct cluster_s *cluster, int order, ppn_t *ppn, uint_t **vma)
{
	kmem_req_t req;

	req.type  = KMEM_PAGE;
	req.size  = order;
	req.flags = AF_KERNEL | AF_ZERO | AF_REMOTE;
	req.ptr   = cluster;

	struct page_s *page;

	page = (struct page_s*) kmem_alloc(&req);

	if(page == NULL) 
		return NULL;

	if(ppn != NULL)
		*ppn = ppm_page2ppn(page);
  
	if(vma != NULL)
		*vma = ppm_page2addr(page);
  
	return page; 
}

struct ppm_s* pmm_ppn2ppm(ppn_t ppn)
{
	register uint_t clstr_id;
	register uint_t clstr_nr;

	clstr_nr = current_cluster->clstr_nr;//arch_onln_cluster_nr(); /* Must be fixed !! */
	clstr_nr = (clstr_nr != 1) ? clstr_nr - 1 : clstr_nr;
	clstr_id = ppn >> (CONFIG_PPN_BITS_NR - bits_nr(clstr_nr));
	assert(clstr_id <= clstr_nr);

	//boot_dmsg("%s: ppn %x -> cid %d\n", __FUNCTION__, ppn, clstr_id);

	return &clusters_tbl[clstr_id].cluster->ppm;
}

static inline vma_t* pmm_ppn2vma(ppn_t ppn)
{
	return ppm_ppn2vma(pmm_ppn2ppm(ppn), ppn);
}


/* Init-task, one-shut initialization function */
error_t pmm_bootstrap_init(struct pmm_s *init, uint_t arg)
{
	init->pgdir = (uint_t*)arg;
	init->flags = _PMM_INIT;
	return 0;
}

/* Physical Memory Manager Initialization */
error_t pmm_init(struct pmm_s *pmm, struct cluster_s *cluster)
{
	struct page_s *page;
	ppn_t ppn;
	uint_t *pgdir;

	pmm->cluster = cluster;

	if((page = pmm_alloc_pages(pmm->cluster, 2, &ppn, &pgdir)) == NULL)
		return ENOMEM;

	if(pmm->flags & _PMM_INIT)
		memcpy((char*)pgdir+0x1000, (char*)pmm->pgdir+0x1000, 0x1000);

	//page_state_set(page, PGVALID);
	pmm->pgdir     = pgdir;
	pmm->flags     = 0;
	pmm->pgdir_ppn = ppn;
	pmm->page      = page;
	return 0;
}

/* Physical Memory Mananger Duplication */
error_t pmm_dup(struct pmm_s *dst, struct pmm_s *src)
{
	memcpy(((char*)dst->pgdir) + 0x1000, ((char*)src->pgdir)+0x1000, 0x1000);
	return 0;
}

/* Physical Memory Manager Destruction */
error_t pmm_destroy(struct pmm_s *pmm)
{
	kmem_req_t req;

	req.type = KMEM_PAGE;
	req.ptr  = pmm->page;
	kmem_free(&req);
	return 0;
}

/* Physical Memory Manager Release userland ressources */
error_t pmm_release(struct pmm_s *pmm)
{
	kmem_req_t req;
	uint_t *pgdir;
	uint_t val;
	uint_t entries_nr;
	uint_t i;
  
	req.type   = KMEM_PAGE;
	pgdir      = pmm->pgdir;
	entries_nr = CONFIG_KERNEL_OFFSET >> PMM_HUGE_PAGE_SHIFT;

	for(i=0; i < entries_nr; i++)
	{
		val = pgdir[i];
		if((val != 0) && (val & PMM_PRESENT))
		{
			pgdir[i] = 0;

			if(val & PMM_HUGE)
				val = val & MMU_PPN_MASK;
			else
				val = (val & MMU_PDE_PPN_MASK) << (PMM_HUGE_PAGE_SHIFT - PMM_PAGE_SHIFT);

			req.ptr  = ppm_ppn2page(pmm_ppn2ppm(val), val);
			kmem_free(&req);
		}
	}

	return 0;
}

error_t pmm_set_page(struct pmm_s *pmm, vma_t vaddr, pmm_page_info_t *info)
{
	volatile uint_t *pde;
	struct page_s *page;
	struct cluster_s *cluster;
	uint_t pde_val;
	uint_t *pte;
	ppn_t pte_ppn;
	uint_t isHuge;
	uint_t attr;
	ppn_t ppn;
	bool_t isAtomic;
	uint_t cpu_id;

	cpu_id = cpu_get_id();
	pde = &pmm->pgdir[MMU_PDE(vaddr)];
	attr = info->attr;
	ppn  = info->ppn;
	isHuge = (attr & PMM_HUGE);

	if(info->attr & PMM_CLEAR)
	{
		attr = (isHuge) ? PMM_HUGE : 0;
		ppn  = 0;
	}

	pde_val = *pde;
	pte_ppn = 0;
	pte = NULL;
  
	if(isHuge)
	{
		if((ppn != 0) && ((pde_val != 0) || (attr & PMM_COW)))
		{
			boot_dmsg("ERROR: %s: Unexpected pde_val %x\n", __FUNCTION__, pde_val);
			return EINVAL;
		}
      
		*pde = (attr ^ PMM_HUGE) | (ppn >> 9);
		cpu_wbflush();

#if CONFIG_MONO_CPU
		pmm_tlb_flush_vaddr(vaddr, PMM_UNKNOWN);
#endif

		return 0;
	}

	if(pde_val == 0)
	{
		cluster = (info->cluster == NULL) ? current_cluster : info->cluster;

		if((page = pmm_alloc_pages(cluster, 0, &pte_ppn, &pte)) == NULL)
			return ENOMEM;

		do 
		{
			isAtomic = cpu_atomic_cas((void*)pde, 0, PMM_PRESENT | MMU_PTD1 | pte_ppn);
		}while((isAtomic == false) && (*pde == 0));

		if(isAtomic == false)
		{
			ppm_free_pages(page);
			pde_val = *pde;
			pte_ppn = pde_val & MMU_PPN_MASK;
			pte     = pmm_ppn2vma(pte_ppn);
			current_thread->info.spurious_pgfault_cntr ++;
		}
	}
	else
	{
		pte_ppn = pde_val & MMU_PPN_MASK;
		pte     = pmm_ppn2vma(pte_ppn);
	}

	pte    = (uint_t*)((char*)pte + MMU_PTE(vaddr));
	pte[0] = attr;
	pte[1] = ppn;
	cpu_wbflush();

#if CONFIG_MONO_CPU
	pmm_tlb_flush_vaddr(vaddr, PMM_UNKNOWN);
#endif
	return 0;
}

error_t pmm_get_page(struct pmm_s *pmm, vma_t vaddr, pmm_page_info_t *info)
{
	uint_t *pte;
	uint_t val;

	val = pmm->pgdir[MMU_PDE(vaddr)];

	if(!(val & PMM_PRESENT))
	{
		info->attr = 0;
		info->ppn = 0;
		return 0;
	}

	if(!(val & PMM_HUGE))
	{
		info->attr = (val & MMU_PDE_ATTR_MASK) | PMM_HUGE;
		info->ppn  = (((val & MMU_PDE_PPN_MASK) << PMM_HUGE_PAGE_SHIFT) | 
			      (vaddr & PMM_HUGE_PAGE_MASK)) >> PMM_PAGE_SHIFT;
		return 0;
	}

	pte = pmm_ppn2vma(val & MMU_PPN_MASK);
	pte = (uint_t*)((char*)pte + MMU_PTE(vaddr));

	info->attr = pte[0];
	info->ppn  = pte[1] & MMU_PPN_MASK;
	return 0;
}


error_t pmm_lock_page(struct pmm_s *pmm, vma_t vaddr, pmm_page_info_t *info)
{
	volatile uint_t *pde;
	volatile uint_t *pte_ptr;
	struct cluster_s *cluster;
	struct page_s *page;
	uint_t *pte;
	uint_t pde_val;
	uint_t attr;
	ppn_t  pte_ppn;
	bool_t isAtomic;

	pde      = &pmm->pgdir[MMU_PDE(vaddr)];
	pde_val  = *pde;
	pte      = NULL;
	pte_ppn  = 0;
  
	if((pde_val != 0) && !(pde_val & PMM_HUGE))
		return EINVAL;

	if(pde_val == 0)
	{
		cluster = (pmm->cluster == NULL) ? current_cluster : pmm->cluster;

		if((page = pmm_alloc_pages(cluster, 0, &pte_ppn, &pte)) == NULL)
			return ENOMEM;

		do 
		{
			isAtomic = cpu_atomic_cas((void*)pde, 0, PMM_PRESENT | MMU_PTD1 | pte_ppn);
		}while((isAtomic == false) && (*pde == 0));

		if(isAtomic == false)
		{
			/* TODO: diffre this free operation by adding it to a propre queue, 
			 * Events system doesn't help here cause we don't want to preempt 
			 * the user thread */
			ppm_free_pages(page);	
			pde_val = *pde;
			pte_ppn = pde_val & MMU_PPN_MASK;
			pte     = pmm_ppn2vma(pte_ppn);
			current_thread->info.spurious_pgfault_cntr ++;
		}
	}
	else
	{
		pte_ppn = pde_val & MMU_PPN_MASK;
		pte     = pmm_ppn2vma(pte_ppn);
	}

	pte_ptr  = (volatile uint_t*)((char*)pte + MMU_PTE(vaddr));
	isAtomic = false;
	attr     = pte_ptr[0];

	while(attr == 0)
	{
		isAtomic = cpu_atomic_cas((void*)pte_ptr, attr, attr | PMM_LOCKED);
		attr = pte_ptr[0];
	}

	info->attr     = attr;
	info->ppn      = pte_ptr[1] & MMU_PPN_MASK;
	info->isAtomic = isAtomic;
	info->data     = (void*)pte_ptr;

	return 0;
}

error_t pmm_unlock_page(struct pmm_s *pmm, vma_t vaddr, pmm_page_info_t *info)
{
	volatile uint_t *pte;
	bool_t isAtomic;
	uint_t attr;

	pte      = info->data;
	attr     = pte[0];
	isAtomic = false;

	while((isAtomic == false) && (attr & PMM_LOCKED))
	{
		isAtomic = cpu_atomic_cas((void*)pte, attr, attr & ~PMM_LOCKED);
		attr     = pte[0];
	}

	info->attr     = attr;
	info->ppn      = pte[1];
	info->isAtomic = isAtomic;
	return 0;
}



/* Region related operations */
error_t pmm_region_dup(struct pmm_s *dst, struct pmm_s *src, vma_t vaddr, vma_t limit, uint_t flags)
{
#if PMM_DEBUG
	printk(DEBUG,"DEBUG: %s: dst %x, src %x, dst->pgdir %x, src->pgdir %x, vaddr %x, limit %x, Size %d\n",
	       __FUNCTION__, 
	       dst, src, 
	       &dst->pgdir[MMU_PDE(vaddr)], 
	       &src->pgdir[MMU_PDE(vaddr)], 
	       vaddr ,limit,
	       0x2000 - ((MMU_PDE(vaddr)) << 2));
#endif

	memcpy(&dst->pgdir[MMU_PDE(vaddr)], &src->pgdir[MMU_PDE(vaddr)], 0x2000 - ((MMU_PDE(vaddr)) << 2));
	return 0;
}

error_t pmm_region_map(struct pmm_s *pmm, vma_t vaddr, uint_t pages_nr, pmm_page_info_t *info)
{
	uint_t page_size;
	uint_t ppn;
	uint_t order;
	uint_t count;
	error_t err;
	struct page_s *page;
	struct cluster_s *cluster;

	if(info->ppn == 0) 
		return 0;

	cluster = (info->cluster == NULL) ? current_cluster : info->cluster;
  
	if(info->attr & PMM_HUGE)
	{
		page_size = PMM_HUGE_PAGE_SIZE;
		order     = 9;
	}
	else
	{
		page_size = PMM_PAGE_SIZE;
		order = 0;
	}

	count = pages_nr;

	while(count)
	{
		if((page = pmm_alloc_pages(cluster, order, &ppn, NULL)) == NULL)
		{
			pmm_region_unmap(pmm, vaddr, pages_nr, PMM_UNKNOWN);
			return ENOMEM;
		}

		page->mapper = 0;
		//page_shared_init(page);
		info->ppn = ppn;
    
#if PMM_DEBUG
		printk(DEBUG,"DEBUG: %s: cid %d, info->cluster %d, info->attr %x, info->ppn %x, vaddr %x, page_size %d, count %d\n",
		       __FUNCTION__, 
		       cluster->id, 
		       info->cluster->id, 
		       info->attr, 
		       info->ppn, 
		       vaddr, 
		       page_size, 
		       count);
#endif

		if((err = pmm_set_page(pmm, vaddr, info)))
			return err;
    
		vaddr += page_size;
		count --;
	}
 
	return 0;
}

error_t pmm_region_unmap(struct pmm_s *pmm, vma_t vaddr, uint_t pages_nr, uint_t flags)
{
	return 0;
}

error_t pmm_region_attr_set(struct pmm_s *pmm, vma_t vaddr, uint_t pages_nr, uint_t attr)
{
	error_t err;
	uint_t count;
	uint_t page_size;
	pmm_page_info_t info;
  
	count     = pages_nr;
	page_size = (attr & PMM_HUGE) ? PMM_HUGE_PAGE_SIZE : PMM_PAGE_SIZE;
  
	while(count)
	{
		if((err = pmm_get_page(pmm, vaddr, &info)))
			return err;

		if((info.ppn != 0) && (info.attr & PMM_PRESENT))
		{
			info.attr = attr;
			pmm_set_page(pmm, vaddr, &info);
		}

		vaddr += page_size;
		count --;
	}

	return 0;
}


#define _USE_MMU_INFO_H_
#include <mmu-info.h>

/* TLB enable/disable */
void pmm_tlb_enable(uint_t flags)
{
	uint_t mode;

	if(flags & PMM_TEXT)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mode = mips_get_cp2(1,0);
		mode = (mode & 0x0F) | 0x8;
		mips_set_cp2(1,mode,0);
#endif
	}

	if(flags & PMM_DATA)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mode = mips_get_cp2(1,0);
		mode = (mode & 0x0f) | 0x4;
		mips_set_cp2(1,mode,0);
#endif
	}
}

void pmm_tlb_disable(uint_t flags)
{
	uint_t mode;

	if(flags & PMM_TEXT)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mode  = mips_get_cp2(MMU_MODE,0);
		mode &= 0x7;
		mips_set_cp2(MMU_MODE,mode,0);
#endif
	}

	if(flags & PMM_DATA)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mode = mips_get_cp2(MMU_MODE,0);
		mode &= 3;
		mips_set_cp2(MMU_MODE,mode,0);
#endif
	}
}

void pmm_tlb_flush_vaddr(vma_t vaddr, uint_t flags)
{

	if(flags & PMM_TEXT)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mips_set_cp2(MMU_ITLB_INVAL,vaddr,0);
#endif
	}

	if(flags & PMM_DATA)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mips_set_cp2(MMU_DTLB_INVAL,vaddr,0);
#endif
	}
}

/* CPU cache flush */
void pmm_cache_flush_vaddr(vma_t vaddr, uint_t flags)
{
	if(flags & PMM_TEXT)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mips_set_cp2(MMU_ICACHE_INVAL,vaddr,0);
#endif
	}

	if(flags & PMM_DATA)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mips_set_cp2(MMU_DCACHE_INVAL,vaddr,0);
#endif
	}
}

void pmm_cache_flush(uint_t flags)
{
	if(flags & PMM_TEXT)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mips_set_cp2(MMU_ICACHE_FLUSH,0,0);
#endif
	}

	if(flags & PMM_DATA)
	{
#if (CONFIG_CPU_TYPE == MIPS32)
		mips_set_cp2(MMU_DCACHE_FLUSH,0,0);
#endif
	}
  
}

void pmm_print(struct pmm_s *pmm)
{
	uint_t i;
	uint_t *pgdir = pmm->pgdir;
	uint_t cpu = cpu_get_id();

	for(i=0; i < 2048; i++)
	{
		if(pgdir[i] != 0)
			printk(INFO, "cpu %d: pgdir[%d] = %x\n", cpu, i, pgdir[i]);
	}
}
