/*
 * kern/dqdt.c - Distributed Quaternary Decision Tree
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
#include <thread.h>
#include <task.h>
#include <cluster.h>
#include <pmm.h>
#include <cpu.h>
#include <bits.h>

#include <dqdt.h>

#define DQDT_MGR_PERIOD      CONFIG_DQDT_MGR_PERIOD
#define DQDT_DIST_MANHATTAN  1
#define DQDT_DIST_RANDOM     2

#if 0
#define down_dmsg(x,...)    printk(INFO, __VA_ARGS__)
#define up_dmsg(x,...)      printk(INFO, __VA_ARGS__)
#define select_dmsg(x,...)  printk(INFO, __VA_ARGS__)
#define update_dmsg(x,...)  printk(INFO, __VA_ARGS__)
#else
#define down_dmsg(x,...)    dqdt_dmsg(x,__VA_ARGS__)
#define up_dmsg(x,...)      dqdt_dmsg(x,__VA_ARGS__)
#define select_dmsg(x,...)  dqdt_dmsg(x,__VA_ARGS__)
#define update_dmsg(x,...)  dqdt_dmsg(x,__VA_ARGS__)
#endif

struct dqdt_cluster_s *dqdt_root;
static spinlock_t dqdt_lock;
static struct wait_queue_s dqdt_task_queue;
static uint_t dqdt_count;
static uint_t dqdt_threshold;
static uint_t dqdt_last_pid;

void dqdt_init()
{
	spinlock_init(&dqdt_lock, "dqdt lock");
	wait_queue_init(&dqdt_task_queue, "dqdt task");
	dqdt_count     = 0;
	dqdt_threshold = 100;
	dqdt_last_pid  = 0;
	cpu_wbflush();
}

void dqdt_wait_for_update()
{
	bool_t dontWait = false;
	struct thread_s *this;

	this = current_thread;

	spinlock_lock(&dqdt_lock);

	if(((dqdt_last_pid == this->task->pid) && (dqdt_count < dqdt_threshold)) || 
	   ((dqdt_count == 0) && (dqdt_last_pid == 0)))
	{
		dontWait      = true;
		dqdt_last_pid = this->task->pid;
		dqdt_count   += 1;
	}
	else
	{
		wait_on(&dqdt_task_queue, WAIT_LAST);
		dqdt_count += 3;
	}

	spinlock_unlock(&dqdt_lock);
	
	if(dontWait)
		return;

	sched_sleep(current_thread);
}

void dqdt_update_done()
{
	struct thread_s *thread;

	spinlock_lock(&dqdt_lock);

	thread         = wakeup_one(&dqdt_task_queue, WAIT_FIRST);
	dqdt_count     = 0;
	dqdt_last_pid  = (thread == NULL) ? 0 : thread->task->pid;
	dqdt_threshold = (dqdt_root->info.summary.U < 60) ?  100 : 10;

	spinlock_unlock(&dqdt_lock);
}

static inline uint_t dqdt_distance(struct dqdt_cluster_s *c1, struct dqdt_cluster_s *c2, struct dqdt_attr_s *attr)
{
	register sint_t x1,y1,x2,y2,d;
  
	switch(attr->d_type)
	{
	case DQDT_DIST_MANHATTAN:
		x1 = c1->home->x_coord;
		y1 = c1->home->y_coord;
		x2 = c2->home->x_coord;
		y2 = c2->home->y_coord;
		d = ABS((x1 - x2)) + ABS((y1 - y2));
		break;

	case DQDT_DIST_RANDOM:
		//srand(cpu_time_stamp());
		d = rand();
		break;

	default:
		d = 1;
		break;
	}

	return d;
}

void dqdt_print_summary(struct dqdt_cluster_s *cluster)
{
	uint_t i;

	printk(INFO,"cpu %d, cid %d, level %d, index %d, Indicators Summary:\n",
	       cpu_get_id(),
	       current_cluster->id,
	       cluster->level,
	       cluster->index);

	printk(INFO,"   M %d, T %d, U %d\n", 
	       cluster->info.summary.M, 
	       cluster->info.summary.T, 
	       cluster->info.summary.U);
  
	printk(INFO,"   Pages_tbl [");
  
	for(i = 0; i < PPM_MAX_ORDER; i++)
		printk(INFO, "%d, ", cluster->info.summary.pages_tbl[i]);
	
	printk(INFO, "\b\b]\n");
}

void dqdt_print(struct dqdt_cluster_s *cluster)
{
	uint_t i;
 
	printk(INFO,"Entring Level %d, Index %d\n", 
	       cluster->level, 
	       cluster->index);
    
	for(i = 0; i < 4; i++)
	{
		if(cluster->children[i] == NULL)
		{
			printk(INFO,"  Level %d: Child %d is Down\n", 
			       cluster->level, i);

			continue;
		}

		if(cluster->level == 1)
		{
			printk(INFO,"  Level %d: Child %d is Physical-Cluster of cid %d\n", 
			       1, i, 
			       cluster->children[i]->home->id);

			continue;
		}

		dqdt_print(cluster->children[i]);
	}

	printk(INFO, "Leaving Level %d, Index %d\n", 
	       cluster->level, 
	       cluster->index);
}

error_t dqdt_update(void)
{
	struct dqdt_cluster_s *logical;
	struct dqdt_cluster_s *parent;
	struct cluster_s *cluster;
	register uint_t i,j,p;
	register uint_t usage;
	register uint_t threads;
	register uint_t free_pages;
	uint_t pages_tbl[PPM_MAX_ORDER];
  
	cluster = current_cluster;
	usage   = 0;
	threads = 0;

	update_dmsg(1, "%s: cluster %d, started, onln_cpu_nr %d\n", 
		    __FUNCTION__, 
		    cluster->id, 
		    cluster->onln_cpu_nr);

	for(i = 0; i < cluster->onln_cpu_nr; i++)
	{
		cpu_compute_stats(&cluster->cpu_tbl[i], CONFIG_DQDT_MGR_PERIOD);
		usage   += cluster->cpu_tbl[i].busy_percent;//cluster->cpu_tbl[i].usage;
		threads += cluster->cpu_tbl[i].scheduler.u_runnable;
	}

	usage /= cluster->onln_cpu_nr;
  
	update_dmsg(1, "%s: cluster %d, usage %d, T %d\n", 
		    __FUNCTION__, 
		    cluster->id, 
		    usage, 
		    threads);
 
	logical = cluster->levels_tbl[0];

	logical->info.summary.M = cluster->ppm.free_pages_nr;
	logical->info.summary.T = threads;
	logical->info.summary.U = usage;
  
	for(i = 0; i < PPM_MAX_ORDER; i++)
		logical->info.summary.pages_tbl[i] = cluster->ppm.free_pages[i].pages_nr;

	parent = logical->parent;

	memcpy(&parent->info.tbl[logical->index], 
	       &logical->info.summary, 
	       sizeof(logical->info.summary));

	for(i = 1; i < DQDT_LEVELS_NR; i++)
	{
		logical = cluster->levels_tbl[i];
    
		if(logical == NULL)
			continue;  /* TODO: verify that we can break here instead continue */
   
		update_dmsg(1, "%s: cluster %d, level %d, logical level %d\n", 
			    __FUNCTION__, 
			    cluster->id, 
			    i,logical->level);
    
		free_pages = 0;
		threads    = 0;
		usage      = 0;
		memset(&pages_tbl[0], 0, sizeof(pages_tbl));

		for(j = 0; j < 4; j++)
		{
			if(logical->children[j] != NULL)
			{
				free_pages += logical->info.tbl[j].M;
				threads    += logical->info.tbl[j].T;
				usage      += logical->info.tbl[j].U;
      
				for(p = 0; p < PPM_MAX_ORDER; p++)
					pages_tbl[p] += logical->info.tbl[j].pages_tbl[p];
			}
		}
    
		update_dmsg(1, "%s: cluster %d, level %d, usage %d, T %d\n", 
			    __FUNCTION__,
			    cluster->id,
			    i, 
			    usage/logical->childs_nr, 
			    threads);

		logical->info.summary.M = free_pages;
		logical->info.summary.T = threads;
		logical->info.summary.U = usage / logical->childs_nr;
		memcpy(&logical->info.summary.pages_tbl[0], &pages_tbl[0], sizeof(pages_tbl));

#if CONFIG_DQDT_DEBUG == 2
		update_dmsg(1, "%s: cluster %d, level %d summary pages_tbl [", 
			    __FUNCTION__, 
			    cluster->id, i);
    
		for(p=0; p < PPM_MAX_ORDER; p++)
			update_dmsg(1, "%d, ", logical->info.summary.pages_tbl[p]);
    
		update_dmsg(1, "\b\b]\n");
#endif	/* CONFIG_DQDT_DEBUG */

		parent = logical->parent;
    
		if(parent != NULL)
		{
			memcpy(&parent->info.tbl[logical->index], 
			       &logical->info.summary, 
			       sizeof(logical->info.summary));

			update_dmsg(1, "%s: cluster %d, level %d to level %d, propagated\n", 
				    __FUNCTION__, 
				    cluster->id, 
				    i, i+1);
		}
		else
		{
			dqdt_update_done();

#if CONFIG_DQDT_DEBUG == 2
			dqdt_print_summary(logical);
#endif
		}
	}
  
	return 0;
}

bool_t dqdt_down_traversal(struct dqdt_cluster_s *logical, 
			   struct dqdt_attr_s *attr,
			   dqdt_select_t *select, 
			   dqdt_select_t *request,
			   uint_t index)
{
	register uint_t i, j,val;
	register bool_t found, done;
	uint_t distance_tbl[4] = {100,100,100,100};
 
	down_dmsg(1, "%s: cpu %d, current level %d, tid [0x%x - 0x%x] SP 0x%x\n", 
		  __FUNCTION__, 
		  cpu_get_id(),
		  logical->level,
		  current_thread,
		  &current_thread->signature,
		  cpu_get_stack());

	if(logical->level == 0)
		return request(logical,attr,-1);
	
	for(i = 0; i < 4; i++)
	{
		if((logical->children[i] == NULL) || (i == index))
			continue;
    
		down_dmsg(1, "%s: looking for child %d\n", __FUNCTION__, i);
	
		found = select(logical,attr,i);
		if(found)
		{
			val =  dqdt_distance(attr->origin, logical->children[i], attr);
			val = (i << 16) | (val % 101);
			distance_tbl[i] = val;
		}
	}

	for(i = 0; i < 4; i++)
	{
		val = distance_tbl[i];
    
		for(j = i + 1; j < 4; j++)
		{
			if(distance_tbl[j] < (val & 0xFFFF))
			{
				distance_tbl[i] = distance_tbl[j];
				distance_tbl[j] = val;
			}
		}
	}

	down_dmsg(1,
		  "%s: D-Tbl: [%x,%x,%x,%x]\n", 
		  __FUNCTION__,
		  distance_tbl[0],
		  distance_tbl[1],
		  distance_tbl[2],
		  distance_tbl[3]);

	for(i = 0; i < 4; i++)
	{
		val = distance_tbl[i];

		if((val & 0xFFFF) != 100)
		{
			j = val >> 16;
			assert(logical->children[j] != NULL);

			done = dqdt_down_traversal(logical->children[j], 
						   attr, 
						   select, 
						   request, 5);
			
			if(done) return true;
      
			down_dmsg(1, "%s: child %d is busy\n", __FUNCTION__, j);
		}
	}

	down_dmsg(1, "%s: current level %d do not mach request\n", 
		  __FUNCTION__, 
		  logical->level);

	return false;
}

error_t dqdt_up_traversal(struct dqdt_cluster_s *logical,
			  struct dqdt_attr_s *attr,
			  dqdt_select_t child_select,
			  dqdt_select_t clstr_select,
			  dqdt_select_t request,
			  uint_t limit,
			  uint_t index)
{
	bool_t found;
	bool_t done;

	if(logical == NULL)
		return EAGAIN;
  
	if(logical->level > limit)
		return ERANGE;
 
	up_dmsg(1, "%s: current level %d\n", __FUNCTION__, logical->level);
    
	found = clstr_select(logical,attr,-1);
  
	if(found)
	{
		done = dqdt_down_traversal(logical, 
					   attr, 
					   child_select, 
					   request, 
					   index);

		if(done) return 0;
	}

	if(logical->childs_nr == 1)
		return EAGAIN;

	return dqdt_up_traversal(logical->parent, 
				 attr, 
				 child_select, 
				 clstr_select, 
				 request, 
				 limit, 
				 logical->index);
}

DQDT_SELECT_HELPER(dqdt_placement_child_select)
{
	//return (logical->info.tbl[child_index].U <= attr->u_threshold) ? true : false;
	return (logical->info.tbl[child_index].U < 100) ? true : false;
}

DQDT_SELECT_HELPER(dqdt_placement_clstr_select)
{
	//return (logical->info.summary.U <= attr->u_threshold) ? true : false;
	return (logical->info.summary.U <= 90) ? true : false;
}

DQDT_SELECT_HELPER(dqdt_migrate_clstr_select)
{
	//return (logical->info.summary.U <= attr->u_threshold) ? true : false;
	return (logical->info.summary.U < 100) ? true : false;
}

DQDT_SELECT_HELPER(dqdt_cpu_min_usage_select)
{
	register struct cluster_s *cluster;
	register struct cpu_s *cpu;
	register uint_t i;
	register uint_t min;
	register uint_t usage;

	cluster = logical->home;
  
	assert(cluster != NULL);

	select_dmsg(1, "%s: cluster %d, usage %d\n", 
		    __FUNCTION__, 
		    cluster->id, 
		    cluster->levels_tbl[0]->info.summary.U);
  
	for(min = 0, usage = 1000, i = 0; i < cluster->onln_cpu_nr; i++)
	{
		select_dmsg(1, "%s: cpu lid %d, usage %d, current min %d, lid %d\n", 
			    __FUNCTION__, 
			    i, cluster->cpu_tbl[i].usage,
			    usage, 
			    min);
    
		if(cluster->cpu_tbl[i].usage < usage)
		{
			usage = cluster->cpu_tbl[i].usage;
			min = i;
		}
	}

	cpu = &cluster->cpu_tbl[min];
	
	if((usage <= attr->u_threshold) && (cpu->scheduler.u_runnable <= attr->t_threshold))
	{
		cluster->cpu_tbl[min].usage = 100;
		cpu_wbflush();
		pmm_cache_flush_vaddr((vma_t)&cluster->cpu_tbl[min].usage, PMM_DATA);
		attr->cluster               = cluster;
		attr->cpu                   = cpu;
		return true;
	}

	current_thread->info.u_err_nr ++;
	select_dmsg(1, "%s: cluster %d is busy\n", __FUNCTION__, cluster->id);
	return false;
}

static bool_t dqdt_cpu_isSelectable(struct cpu_s *cpu, struct dqdt_attr_s *attr)
{
	if((cpu->scheduler.user_nr == 0) && (cpu->busy_percent <= 80))
		return true;
	else
		return false;
}

DQDT_SELECT_HELPER(dqdt_cpu_free_select)
{
	register struct cluster_s *cluster;
	register uint_t i;
	register bool_t found;

	cluster = logical->home;
  
	assert(cluster != NULL);

	select_dmsg(1, "%s: cluster %d, T %d\n", 
		    __FUNCTION__,
		    cluster->id, 
		    cluster->levels_tbl[0]->info.summary.T);
  
	for(i = 0; i < cluster->onln_cpu_nr; i++)
	{
		select_dmsg(1, "%s: cpu lid %d, U %d\n",
			    __FUNCTION__, 
			    i, 
			    cluster->cpu_tbl[i].usage);
    
		found = dqdt_cpu_isSelectable(&cluster->cpu_tbl[i], attr);
    
		if(found == true)
		{
			attr->cluster = cluster;
			attr->cpu = &cluster->cpu_tbl[i];
			cluster->cpu_tbl[i].usage = 100; /* fake weight */
			cpu_wbflush();
			pmm_cache_flush_vaddr((vma_t)&cluster->cpu_tbl[i].usage, PMM_DATA);
			//sched_wakeup(cluster->manager);
			return true;
		}
	}

	current_thread->info.u_err_nr ++;
	select_dmsg(1, "%s: cluster %d is busy\n", __FUNCTION__, cluster->id);
	return false;
}


error_t dqdt_thread_placement(struct dqdt_cluster_s *logical, struct dqdt_attr_s *attr)
{
	register uint_t threshold;
	error_t err;

	select_dmsg(1, "%s: started, logical level %d\n", __FUNCTION__, logical->level);
  
	attr->origin      = current_cluster->levels_tbl[0];
	attr->t_threshold = 2;
	attr->u_threshold = 98;
	attr->d_type      = DQDT_DIST_MANHATTAN;

	dqdt_wait_for_update();

	err = dqdt_up_traversal(logical,
				attr,
				dqdt_placement_child_select,
				dqdt_placement_clstr_select,
				dqdt_cpu_free_select,
				DQDT_MAX_DEPTH,
				logical->index);

	if(err == 0) goto found;

	err = EAGAIN;

	for(threshold = 10; ((threshold < 100) && (err != 0)); threshold += 20)
	{
		attr->u_threshold = threshold;
		
		err = dqdt_up_traversal(logical,
					attr,
					dqdt_placement_child_select,
					dqdt_placement_clstr_select,
					dqdt_cpu_min_usage_select,
					DQDT_MAX_DEPTH,
					logical->index);
	}

found:
	select_dmsg(1, "%s: ended, found %s\n", __FUNCTION__, (err == 0) ? "(Yes)" : "(No)");
	return err;
}

error_t dqdt_thread_migrate(struct dqdt_cluster_s *logical, struct dqdt_attr_s *attr)
{
	register uint_t threshold;
	struct thread_s *this;
	error_t err;
	bool_t isFailThreshold;
	bool_t isSuccessThreshold;

	select_dmsg(1, "%s: started, logical level %d\n", __FUNCTION__, logical->level);
  
	attr->origin      = current_cluster->levels_tbl[0];
	attr->t_threshold = current_cluster->onln_cpu_nr;
	attr->d_type      = DQDT_DIST_MANHATTAN;

	for(threshold = 20; threshold < 100; threshold += 20)
	{
		attr->u_threshold = threshold;

		switch(threshold)
		{
		case 20:
		case 70:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_migrate_clstr_select,
						dqdt_cpu_free_select,
						3,
						logical->index);
			break;

		default:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_migrate_clstr_select,
						dqdt_cpu_min_usage_select,
						3,
						logical->index);
		}
		
		if(err == 0) break;
	}

#if 1
	/* TODO: move these thresholds to per-task variables */
	this               = current_thread;
       	isFailThreshold    = (this->info.migration_fail_cntr > 5) ? true : false;
	isSuccessThreshold = (this->info.migration_cntr > 3) ? true : false; 
	
	if(isFailThreshold || isSuccessThreshold)
	{
		err = dqdt_up_traversal(dqdt_root,
					attr,
					dqdt_placement_child_select,
					dqdt_migrate_clstr_select,
					dqdt_cpu_min_usage_select,
					10,
					10);

		if(err == 0)
			this->info.migration_fail_cntr = (isFailThreshold) ? 0 : this->info.migration_fail_cntr;
	}
#endif
	select_dmsg(1, "%s: ended, found %s\n", __FUNCTION__, (err == 0) ? "(Yes)" : "(No)");
	return err;
}


DQDT_SELECT_HELPER(dqdt_task_placement_child_select)
{
	register bool_t found = false;

	select_dmsg(1, "%s: level %d, child indx %d, U %d, M %d\n",
		    __FUNCTION__,
		    logical->level,
		    child_index,
		    logical->info.tbl[child_index].U,
		    logical->info.tbl[child_index].M);

	if((logical->info.tbl[child_index].U <= attr->u_threshold) &&
	   (logical->info.tbl[child_index].M >= attr->m_threshold))
	{
		found = true;
	}

	return found;
}

DQDT_SELECT_HELPER(dqdt_task_placement_clstr_select)
{
	select_dmsg(1, "%s: level %d, u_threshold %d, summary [U:%d, M:%d]\n",
		    __FUNCTION__,
		    logical->level,
		    attr->u_threshold,
		    logical->info.summary.U,
		    logical->info.summary.M);

	return (logical->info.summary.U <= attr->u_threshold) ? true : false;
}

struct dqdt_cluster_s* dqdt_logical_lookup(uint_t level)
{
	register struct dqdt_cluster_s *current;

	current = current_cluster->levels_tbl[0];
  
	while((current != NULL) && (current->level != level))
		current = current->parent;

	return (current == NULL) ? dqdt_root : current;
}

error_t dqdt_task_placement(struct dqdt_cluster_s *logical, struct dqdt_attr_s *attr)
{
	register uint_t threshold;
	error_t err;

	select_dmsg(1, "%s: started, logical level %d\n", __FUNCTION__, logical->level);
	attr->m_threshold = current_cluster->ppm.uprio_pages_min;
	attr->t_threshold = 5;
	attr->d_type      = DQDT_DIST_RANDOM;
	attr->origin      = current_cluster->levels_tbl[0];
	err               = EAGAIN;

	dqdt_wait_for_update();

	for(threshold = 10; ((threshold <= 100) && (err != 0)); threshold += 15)
	{
		attr->u_threshold = threshold;

		switch(threshold)
		{
		case 55:
			attr->m_threshold >>= 1;
		case 10:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_task_placement_child_select,
						dqdt_task_placement_clstr_select,
						dqdt_cpu_free_select,
						DQDT_MAX_DEPTH,
						5);
			break;
    
		case 70:
			attr->m_threshold >>= 1;
			
		case 40:
		case 25:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_task_placement_child_select,
						dqdt_task_placement_clstr_select,
						dqdt_cpu_min_usage_select,
						DQDT_MAX_DEPTH,
						5);
			break;

		case 100:
			attr->t_threshold = 10;
		case 85:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_placement_clstr_select,
						dqdt_cpu_min_usage_select,
						DQDT_MAX_DEPTH,
						5);
      
		}
	}

	select_dmsg(1, "%s: ended, found %s [%d]\n", 
		    __FUNCTION__, 
		    (err == 0) ? "(Yes)" : "(No)",
		    err);
	return err;
}

DQDT_SELECT_HELPER(dqdt_mem_clstr_select)
{
	register struct ppm_dqdt_req_s *req;
	register bool_t found;
	register uint_t i;

	req = attr->data;

	select_dmsg(1, 
		    "%s: level %d, threshold %d, order %d, summary [U:%d, M:%d]\n",
		    __FUNCTION__,
		    logical->level,
		    req->threshold,
		    req->order,
		    logical->info.summary.U,
		    logical->info.summary.M);
  
	for(found = false, i = req->order; ((i < CONFIG_PPM_MAX_ORDER) && (found == false)); i++)
	{
		found = (logical->info.summary.pages_tbl[i] != 0) ? true : false;
	}

	return (found && (logical->info.summary.M > req->threshold)) ? true : false;
}

DQDT_SELECT_HELPER(dqdt_mem_child_select)
{
	register struct ppm_dqdt_req_s *req;
	register bool_t found;
	register uint_t i;

	req = attr->data;

	select_dmsg(1, 
		    "%s: level %d, child indx %d, threshold %d, order %d, U %d, M %d\n",
		    __FUNCTION__,
		    logical->level,
		    child_index,
		    req->threshold,
		    req->order,
		    logical->info.tbl[child_index].U,
		    logical->info.tbl[child_index].M);

	for(found = false, i = req->order; ((i < CONFIG_PPM_MAX_ORDER) && (found == false)); i++)
	{
		found = (logical->info.tbl[child_index].pages_tbl[i] != 0) ? true : false;
	}

	return (found && (logical->info.tbl[child_index].M > req->threshold)) ? true : false;
}

DQDT_SELECT_HELPER(dqdt_mem_select)
{
	register struct ppm_dqdt_req_s *req;
	register struct ppm_s *ppm;
	register bool_t found;
	register uint_t i;

	req = attr->data;

	select_dmsg(1,
		    "%s: level %d, cid %d, threshold %d, order %d, summary [U:%d, M:%d]\n",
		    __FUNCTION__,
		    logical->level,
		    logical->home->id,
		    req->threshold,
		    req->order,
		    logical->info.summary.U,
		    logical->info.summary.M);
  
	ppm = &logical->home->ppm;
  
	for(found = false, i = req->order; ((i < CONFIG_PPM_MAX_ORDER) && (found == false)); i++)
	{
		found = (ppm->free_pages[i].pages_nr != 0) ? true : false;
	}

	found = (found && (ppm->free_pages_nr > req->threshold)) ? true : false;

	if(found)
	{
		attr->cluster = logical->home;
		attr->cpu = NULL;
		return true;
	}
  
	current_thread->info.m_err_nr ++;
  
	select_dmsg(1,
		    "%s: cluster %d is busy\n", 
		    __FUNCTION__, 
		    logical->home->id);
	return false;
}


error_t dqdt_mem_request(struct dqdt_cluster_s *logical, struct dqdt_attr_s *attr)
{
	error_t err;

	select_dmsg(1, 
		    "%s: cid %d, logical level %d, home cid %d, started\n", 
		    __FUNCTION__,
		    current_cluster->id,
		    logical->level,
		    logical->home->id);

	attr->d_type = DQDT_DIST_MANHATTAN;
	attr->origin = current_cluster->levels_tbl[0];

	err = dqdt_up_traversal(logical,
				attr,
				dqdt_mem_child_select,
				dqdt_mem_clstr_select,
				dqdt_mem_select,
				DQDT_MAX_DEPTH,
				logical->index);

	select_dmsg(1, 
		    "%s: cid %d, logical level %d, cid %d, found %s, ended\n",
		    __FUNCTION__,
		    current_cluster->id,
		    logical->level,
		    logical->home->id,
		    (err == 0) ? "(Yes)" : "(No)");

	return err;
}
