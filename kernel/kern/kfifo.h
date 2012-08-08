/*
 * kern/lffb.h - Lock-Free Flexible Buffer interface
 *
 * This interface allows to have 
 *   - Single-Writer Single-Reader buffer
 *   - Multiple-Writers Single-Reader buffer
 *   - Single-Writer Multiple-Readers buffer
 *   - Multiple-Writers Multiple-Readers buffer
 * The access to buffer is implemented using 
 * a lock-free algorithm.
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

#ifndef _LFFB_H_
#define _LFFB_H_

#include <errno.h>
#include <types.h>

/////////////////////////////////////////////////////////////
//                     Public Section                      //
/////////////////////////////////////////////////////////////

/* Single Writer Single Reader */
#define LFFB_NA

/* Multiple Writer Single Reader */
#define LFFB_MW

/* Single Writer Multiple Reader */
#define LFFB_MR

/* Muliple Writer Multiple Reader */	
#define LFFB_MWMR

/* Lock-Free Flexible Buffer type */
struct lffb_s;

/**
 * Get one element from @lffb Lock-Free Flexible Buffer
 *  
 * @param   lffb      : Lock-Free Flexible Buffer descriptor
 *
 * @return  NULL if buffer is empty or contention has been detected
 */
void* lffb_get(struct lffb_s *lffb);

/**
 * Put one element into @lffb Lock-Free Flexible Buffer
 *  
 * @param    lffb     : Lock-Free Flexible Buffer descriptor
 * @param    value    : Any non-null value
 * 
 * @return   EBUSY in case of contention 
 *           EINVAL if value is not null
 * 	     0 on success
 */
error_t lffb_put(struct lffb_s *lffb, void *value);

/**
 * Query if @lffb is empty
 *
 * @param    lffb     : Lock-Free Flexible Buffer descriptor
 * @return   true if buffer is empty, false otherway
 */
static inline bool_t lffb_isEmpty(struct lffb_s *lffb);

/**
 * Query if @lffb is full
 *
 * @param    lffb     : Lock-Free Flexible Buffer descriptor
 * @return   true if buffer is full, false otherway
 */
static inline bool_t lffb_isFull(struct lffb_s *lffb);

/**
 * Initialize Lock-Free Flexible Buffer @lffb 
 * 
 * @param     lffb    : Lock-free Flexible buffer descriptor
 * @param     size    : Buffer size in number of element
 * @param     mode    : Buffer access patern as defined by LFFB_XX modes
 *
 * @return    ENOMEM in case of no memory ressources, 0 on success
 */
error_t lffb_init(struct lffb_s *lffb, size_t size, uint_t mode);

/**
 * Destroy Lock-Free Flexible Buffer @lffb
 * It will free all used internal ressourcess
 *
 * @param     lffb    : Buffer descriptor
 */
void lffb_destroy(struct lffb_s *llfb);


/////////////////////////////////////////////////////////////
//                    Private Section                      //
/////////////////////////////////////////////////////////////

/* Single Writer Single Reader */
#undef LFFB_NA
#define LFFB_NA       0x0	

#undef LFFB_MW
#define LFFB_MW       0x1	

#undef LFFB_MR
#define LFFB_MR       0x2

#undef LFFB_MWMR
#define LFFB_MWMR     0x3

#define LFFB_MASK     0x3

struct lffb_s
{
	union 
	{
		volatile uint_t wridx;
		cacheline_t padding1;
	};

	union 
	{
		volatile uint_t rdidx;
		cacheline_t padding2;
	};
  
	size_t size;
	uint_t flags;
	void **tbl;
};


static inline bool_t lffb_isEmpty(struct lffb_s *lffb)
{
	return ((lffb->rdidx % lffb->size) == (lffb->wridx % lffb->size)) ? true : false;
}


static inline bool_t lffb_isFull(struct lffb_s *lffb)
{
	return ((lffb->rdidx % lffb->size) == ((lffb->wridx % lffb->size) + 1)) ? true : false;
}

#if CONFIG_LFFB_DEBUG
void lffb_print(struct lffb_s *lffb);
#endif

#endif	/* _LFFB_H_ */
