/*
 * kern/spinlock.c - kernel spinlock synchronization
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

#include <system.h>
#include <cpu.h>
#include <thread.h>
#include <scheduler.h>
#include <libk.h>
#include <spinlock.h>

#define SL_WRITER    0x80000000

spinlock_t __exception_lock;

inline void spinlock_init(spinlock_t *lock, char *name)
{  
	cpu_spinlock_init(&lock->val, 0);
	lock->info.name = name;

#if CONFIG_SPINLOCK_CHECK 
	lock->info.owner = NULL;
#endif
}

inline void spin_init(slock_t *lock)
{
	cpu_spinlock_init(lock, 0);
}

inline void spinlock_destroy(spinlock_t *lock)
{
	cpu_spinlock_destroy(&lock->val);
}

static void __attribute__((noinline)) spinlock_check(struct thread_s *this, uint_t *mode)
{
	if((this->locks_count == 0) && (this->flags & TH_NEED_TO_SCHED))
		sched_yield(this);
   
	cpu_restore_irq(*mode);
	cpu_disable_all_irq(mode);
}

void spin_lock(slock_t *lock)
{
	volatile sint_t *ptr;
	register sint_t val;
	register bool_t isAtomic;
	register struct thread_s *this;
	uint_t mode;

	ptr = (volatile sint_t*)lock;
	this = current_thread;
	isAtomic = false;

	cpu_disable_all_irq(&mode);

	while(isAtomic == false)
	{
		val = *ptr;
    
		if(val != 0)
		{
			spinlock_check(this,&mode);
			continue;
		}
   
		isAtomic = cpu_atomic_cas((void*)ptr, 0, -1);
	}

	this->locks_count ++;
	cpu_restore_irq(mode);
}

void spinlock_lock(spinlock_t *lock)
{
	volatile sint_t *ptr;
	register struct thread_s *this;
	register bool_t isAtomic;
	register sint_t val;
	uint_t mode;

	ptr      = &lock->val;
	this     = current_thread;
	isAtomic = false;

	cpu_disable_all_irq(&mode);
  
	while(isAtomic == false)
	{
		val = *ptr;

		if(val > 0)
		{
			(void)cpu_atomic_cas((void*)ptr, val, val | SL_WRITER);
			continue;
		}

		if(val != 0)
		{
			spinlock_check(this,&mode);
			continue;
		}
    
		isAtomic = cpu_atomic_cas((void*)ptr, 0, -1);
	}

#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = this;
#endif
 
	this->locks_count ++;
	cpu_restore_irq(mode);
}

void spinlock_rdlock(spinlock_t *lock)
{
	register volatile sint_t *ptr;
	register struct thread_s *this;
	register bool_t isAtomic;
	register sint_t val;
	uint_t mode;

	ptr       = &lock->val;
	this      = current_thread;
	isAtomic  = false;
  
	cpu_disable_all_irq(&mode);
  
	while(isAtomic == false)
	{
		val = *ptr;
    
		if(val < 0)
		{
			spinlock_check(this,&mode);
			continue;
		}
 
		isAtomic = cpu_atomic_cas((void*)ptr, val, val + 1);
	}

#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = this;
#endif

	this->locks_count ++;
	cpu_restore_irq(mode);
}


static void __spinlock_unlock(void *val, bool_t canYield)
{
	volatile sint_t *ptr;
	register struct thread_s *this;
	uint_t mode;
  
	ptr = val;
 
	cpu_disable_all_irq(&mode);
   
	if(*ptr < 0)
	{
		cpu_invalid_dcache_line(val);
		*ptr = 0;
		cpu_wbflush();
	}
	else
		(void)cpu_atomic_add((void*)ptr, -1);
 
	this = current_thread;
	this->locks_count -= 1;

	if((canYield == true) && (this->locks_count == 0) && thread_sched_isActivated(this))
		sched_yield(this);

	cpu_restore_irq(mode);
}

void spinlock_unlock(spinlock_t *lock)
{
#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = NULL;
#endif

	__spinlock_unlock(&lock->val, true); 
}

void spinlock_unlock_nosched(spinlock_t *lock)
{
#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = NULL;
#endif

	__spinlock_unlock(&lock->val, false);
}

void spin_unlock(slock_t *lock)
{
	__spinlock_unlock(lock, true); 
}

void spin_unlock_nosched(slock_t *lock)
{
	__spinlock_unlock(lock, false); 
}


uint_t spinlock_trylock(spinlock_t *lock)
{ 
	uint_t mode;
	register bool_t isAtomic;
	register struct thread_s *this;

	this     = current_thread;
	isAtomic = false;

	cpu_disable_all_irq(&mode);

	if(lock->val == 0)
		isAtomic = cpu_atomic_cas(&lock->val, 0, -1);
  
	if(isAtomic == false)
	{
		cpu_restore_irq(mode);
		return 1;
	}
  
	this->locks_count ++;

#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = this;
#endif

	cpu_restore_irq(mode);  
	return 0;
}

uint_t spinlock_tryrdlock(spinlock_t *lock)
{ 
	uint_t mode;
	register bool_t isAtomic;
	register struct thread_s *this;
	register sint_t val;

	this     = current_thread;
	isAtomic = false;
  
	cpu_disable_all_irq(&mode);

	val = lock->val;

	if(val >= 0)
		isAtomic = cpu_atomic_cas(&lock->val, val, val + 1);
  
	if(isAtomic == false)
	{
		cpu_restore_irq(mode);
		return 1;
	}
  
	this->locks_count ++;

#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = this;
#endif

	cpu_restore_irq(mode);  
	return 0;
}


void spinlock_lock_noirq(spinlock_t *lock, uint_t *irq_state)
{
	register volatile sint_t *ptr;
	register struct thread_s *this;
	sint_t val;
	bool_t isAtomic;
 
	ptr      = &lock->val;
	this     = current_thread;
	isAtomic = false;

	cpu_disable_all_irq(irq_state);
  
	while(isAtomic == false)
	{
		val = *ptr;

		if(val == 0)
			isAtomic = cpu_atomic_cas((void*)ptr, 0, -1);
	}
 
	this->locks_count ++;

#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = this;
#endif
}

void spinlock_unlock_noirq(spinlock_t *lock, uint_t irq_state)
{
	volatile sint_t *ptr = &lock->val;
  
	if(*ptr < 0)
	{
		cpu_invalid_dcache_line(lock);    
		*ptr = 0;
		cpu_wbflush();
	}
	else
		(void)cpu_atomic_add((void*)ptr, -1);
  
	current_thread->locks_count --;

#if CONFIG_SPINLOCK_CHECK
	lock->info.owner = NULL;
#endif
    
	cpu_restore_irq(irq_state);
}
