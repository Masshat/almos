/*
 * kern/lffb.c - Lock-Free Flexible Buffer
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

#include <cpu.h>
#include <kmem.h>
#include <kdmsg.h>
#include <lffb.h>

/* Access function definitions */
#if CONFIG_LFFB_DEBUG
#define LFFB_PUT(n)  error_t (n) (struct lffb_s *lffb, void *val)
#define LFFB_GET(n)  void*   (n) (struct lffb_s *lffb)
#else
#define LFFB_PUT(n)  static error_t (n) (struct lffb_s *lffb, void *val)
#define LFFB_GET(n)  static void*   (n) (struct lffb_s *lffb)
#endif

#if CONFIG_LFFB_DEBUG
void lffb_print(struct lffb_s *lffb)
{
	uint_t i;
  
	printk(DEBUG, "%s: size %d, rdidx %d, wridx %d : [", 
	       __FUNCTION__, 
	       lffb->size,
	       lffb->rdidx,
	       lffb->wridx);

	for(i = 0; i < lffb->size; i++)
		printk(DEBUG, "%d:%d, ", i, (int)lffb->tbl[i]);
  
	printk(DEBUG, "\b\b]\n");
}
#endif

LFFB_PUT(single_put)
{
	size_t idx;
	size_t wridx;
	size_t rdidx;
	size_t size;
	void *old;

	size = lffb->size;

	wridx = lffb->wridx % size;
	rdidx = lffb->rdidx % size;
	old = lffb->tbl[wridx];

	lffb_dmsg(1, "%s: rdidx %d, wridx %d, old %x\n", 
		  __FUNCTION__, 
		  rdidx, 
		  wridx, 
		  old);

	if(((wridx + 1) == rdidx) || (old == NULL))
		return EBUSY;

	idx = lffb->wridx ++;
	idx = idx % lffb->size;
	cpu_wbflush();

	if(lffb->tbl[idx] != NULL)
		return EBUSY;

	lffb->tbl[idx] = val;
	cpu_wbflush();

	return 0;
}

LFFB_PUT(multi_put)
{
	size_t idx;
	size_t wridx;
	size_t rdidx;
	size_t size;
	void *old;

	size = lffb->size;

	wridx = lffb->wridx % size;
	rdidx = lffb->rdidx % size;
  
	old = lffb->tbl[wridx];

	if(((wridx + 1) == rdidx) || (old != NULL))
		return EBUSY;

	/* Speculatif behavior */
	idx = cpu_atomic_add((void*)&lffb->wridx, 1);
	idx = idx % lffb->size;
  
	old = lffb->tbl[idx];

	if(old != NULL)
		return EBUSY;

	lffb->tbl[idx] = val;
	cpu_wbflush();

	return 0;
}


LFFB_GET(single_get)
{
	size_t idx;
	void *val;

	idx = lffb->rdidx;

	if(lffb->tbl[idx] == NULL)
		return NULL;
  
	lffb->rdidx    = (lffb->rdidx + 1) % lffb->size;
	cpu_wbflush();

	val            = lffb->tbl[idx];
	lffb->tbl[idx] = NULL;
	cpu_wbflush();

	return val;
}

LFFB_GET(multi_get)
{
	size_t idx;
	void *val;
	bool_t isAtomic;

	do
	{
		cpu_wbflush();
		idx = lffb->rdidx;
		val = lffb->tbl[idx];

		if(val == NULL) 
			return NULL;

		isAtomic = cpu_atomic_cas((void*)&lffb->rdidx, 
					  idx, 
					  (idx + 1) % lffb->size);
	}while(isAtomic == false);


	lffb->tbl[idx] = NULL;
	cpu_wbflush();

	return val;
}

error_t lffb_init(struct lffb_s *lffb, size_t size, uint_t mode)
{
	kmem_req_t req;

	req.type  = KMEM_GENERIC;
	req.flags = AF_KERNEL | AF_ZERO;
	req.size  = sizeof(void*) * size;

	lffb->tbl = kmem_alloc(&req);

	if(lffb->tbl == NULL)
		return ENOMEM;
  
	lffb->wridx = 0;
	lffb->rdidx = 0;
	lffb->size  = size;
	lffb->flags = mode & LFFB_MASK;

	return 0;
}

void lffb_destroy(struct lffb_s *lffb)
{
	kmem_req_t req;
  
	req.type  = KMEM_GENERIC;
	req.flags = AF_KERNEL;
	req.size  = sizeof(void*) * lffb->size;
	req.ptr   = lffb->tbl;

	kmem_free(&req);
}

void* lffb_get(struct lffb_s *lffb)
{
	if(lffb->flags & LFFB_MR)
		return multi_get(lffb);
 
	return single_get(lffb);
}

error_t lffb_put(struct lffb_s *lffb, void *value)
{
	assert(value != NULL);

	if(lffb->flags & LFFB_MW)
		return multi_put(lffb,value);

	return single_put(lffb,value);
}
