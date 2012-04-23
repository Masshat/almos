/*
 * kern/mcs_sync.h - ticket-based barriers and locks synchronization
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

#ifndef _MCS_SYNC_H_
#define _MCS_SYNC_H_

#include <types.h>

///////////////////////////////////////////////
//             Public Section                //
///////////////////////////////////////////////
struct mcs_sync_s;
typedef struct mcs_sync_s mcs_sync_t;

void mcs_barrier_init(mcs_sync_t *ptr, char *name, uint_t count);
void mcs_barrier_wait(mcs_sync_t *ptr);

void mcs_lock_init(mcs_sync_t *ptr, char *name);

void mcs_lock(mcs_sync_t *ptr, uint_t *irq_state);
void mcs_unlock(mcs_sync_t *ptr, uint_t irq_state);

//////////////////////////////////////////////
//             Private Section              //
//////////////////////////////////////////////

struct mcs_sync_s
{
	uint_t val;
	char *name;
	uint_t phase;
	uint_t cntr CACHELINE;
	uint_t ticket CACHELINE;
	uint_t ticket2 CACHELINE;
};



#endif	/* _MCS_SYNC_H_ */
