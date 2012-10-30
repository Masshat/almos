/*
 * pthread.c -  conditions variables related functions
 * 
 * Copyright (c) 2008,2009,2010,2011,2012 Ghassan Almaless
 * Copyright (c) 2011,2012 UPMC Sorbonne Universites
 *
 * This file is part of ALMOS.
 *
 * ALMOS is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2.0 of the License.
 *
 * ALMOS is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ALMOS; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <sys/types.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <cpu-syscall.h>

typedef enum
{
	CV_INIT,
	CV_WAIT,
	CV_SIGNAL,
	CV_BROADCAST,
	CV_DESTROY
} cv_operation_t;

static int __sys_cond_var(pthread_cond_t *cv, int operation, sem_t *sem)
{
	register int retval;
  
	retval = (int)cpu_syscall((void*)cv,(void*)operation,(void*)sem,NULL,SYS_COND_VAR);
	return retval;
}

int pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *cond_attr)
{
	int err;

	if(cond == NULL) return EINVAL;

	err = pthread_spin_init(&cond->lock, 0);

	if(err) return err;

	cond->count = 0;
	return 0;
}


int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	register __pthread_tls_t *tls;
	register struct __shared_s *shared;
	int err;

	if(cond == NULL) return EINVAL;

#if 0
	err = pthread_spin_lock(&cond->lock);
  
	if(err) return err;

	err = pthread_mutex_unlock(mutex);

	if(err)
	{
		(void)pthread_spin_unlock(&cond->lock);
		return err;
	}
#endif
	if(cond->count == 0)
		list_root_init(&cond->queue);
  
	cond->count ++;

	tls = cpu_get_tls();
	shared = (struct __shared_s*)__pthread_tls_get(tls, __PT_TLS_SHARED);

	list_add_last(&cond->queue, &shared->list);
#if 0
	(void)pthread_spin_unlock(&cond->lock);
#else
	(void)pthread_mutex_unlock(mutex);
#endif

	/* TODO: compute syscall return value (signals treatment) */
	(void)cpu_syscall(NULL, NULL, NULL, NULL, SYS_SLEEP);
  
	err = pthread_mutex_lock(mutex);
	return err;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
	struct __shared_s *next;
	int err;

	if(cond == NULL) return EINVAL;

#if 0
	err = pthread_spin_lock(&cond->lock);

	if(err) return err;
#endif

	if(cond->count == 0)
		return 0;//pthread_spin_unlock(&cond->lock);

	next = list_first(&cond->queue, struct __shared_s, list);
	list_unlink(&next->list);
	cond->count --;

	//err = pthread_spin_unlock(&cond->lock);
	(void)cpu_syscall((void*)next->tid, NULL, NULL, NULL, SYS_WAKEUP);
	return 0;//err;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
	struct list_entry root;
	struct list_entry *list;
	struct __shared_s *next;
	uint_t count;
	uint_t i;
	int err = 0;
  
	if(cond == NULL) return EINVAL;

	//err = pthread_spin_lock(&cond->lock);

	//if(err) return err;
  
	if(cond->count == 0)
	{
		return 0;//pthread_spin_unlock(&cond->lock);
	}

	count       = cond->count;
	cond->count = 0;
	list_replace(&cond->queue, &root);
	//err         = pthread_spin_unlock(&cond->lock);

	uint_t tbl[100];

	i = 0;
  
	while(count)
	{
		list_foreach_forward(&root, list)
		{
			next   = list_element(list, struct __shared_s, list);
			list_unlink(&next->list);
			tbl[i] = next->tid;
			count --;
			i++;

			if(i == 100)
			{
				i = 0;
				(void)cpu_syscall((void*)tbl[0], (void*)&tbl[0], (void*)100, NULL, SYS_WAKEUP);
			}
		}
	}

	if(i != 0)
		(void)cpu_syscall((void*)tbl[0], (void*)&tbl[0], (void*)i, NULL, SYS_WAKEUP);
    
	return err;
}


int pthread_cond_destroy(pthread_cond_t *cond)
{
	int err = 0;
  
	if(cond == NULL)
		return EINVAL;

	//err = pthread_spin_lock(&cond->lock);
	//if(err) return err;

	if(cond->count != 0)
		err = EBUSY;

	//(void)pthread_spin_unlock(&cond->lock);
	return err;
}
