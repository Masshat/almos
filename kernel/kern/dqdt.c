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
#include <cluster.h>
#include <cpu.h>
#include <bits.h>
#include <dqdt.h>
#include <ppm.h>

#define DQDT_MGR_PERIOD      CONFIG_DQDT_MGR_PERIOD

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

static inline uint_t dqdt_distance(struct dqdt_cluster_s *c1, struct dqdt_cluster_s *c2)
{
	register sint_t x1,y1,x2,y2,d;
  
	x1 = c1->home->x_coord;
	y1 = c1->home->y_coord;
	x2 = c2->home->x_coord;
	y2 = c2->home->y_coord;
  
	d = ABS((x1 - x2)) + ABS((y1 - y2));
	return d;
}

struct dqdt_cluster_s *dqdt_root;

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
  
	update_dmsg(1, "%s: cluster %d, usage %d\n", __FUNCTION__, cluster->id, usage);
 
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
    
		update_dmsg(1, "%s: cluster %d, level %d, usage %d\n", 
			    __FUNCTION__,
			    cluster->id,
			    i, usage/4);

		logical->info.summary.M = free_pages;
		logical->info.summary.T = threads;
		logical->info.summary.U = (logical->childs_nr == 1) ? usage : usage / 4;
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
#if CONFIG_DQDT_DEBUG == 2
		else
			dqdt_print_summary(logical);
#endif
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
 
	down_dmsg(1, "%s: current level %d\n", __FUNCTION__, logical->level);

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
			val =  dqdt_distance(attr->origin, logical->children[i]);
			val = (i << 16) | val;
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

	down_dmsg(1, "%s: D-Tbl: [%x,%x,%x,%x]\n", 
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

DQDT_SELECT_HELPER(dqdt_cpu_min_usage_select)
{
	register struct cluster_s *cluster;
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

	if(usage <= attr->u_threshold)
	{
		attr->cluster = cluster;
		attr->cpu = &cluster->cpu_tbl[min];
		cluster->cpu_tbl[min].usage = 100;
		//sched_wakeup(cluster->manager);
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
  
	attr->origin  = current_cluster->levels_tbl[0];

	for(threshold = 20; threshold <= 100; threshold += 20)
	{
		attr->u_threshold = threshold;
		
		switch(threshold)
		{
		case 20:
		case 60:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_placement_clstr_select,
						dqdt_cpu_free_select,
						DQDT_MAX_DEPTH,
						logical->index);
			break;
		default:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_placement_clstr_select,
						dqdt_cpu_min_usage_select,
						DQDT_MAX_DEPTH,
						logical->index);
		}

		if(err == 0) break;
	}

	select_dmsg(1, "%s: ended, found %s\n", __FUNCTION__, (err == 0) ? "(Yes)" : "(No)");
	return err;
}

error_t dqdt_thread_migrate(struct dqdt_cluster_s *logical, struct dqdt_attr_s *attr)
{
	register uint_t threshold;
	error_t err;

	select_dmsg(1, "%s: started, logical level %d\n", __FUNCTION__, logical->level);
  
	attr->origin = current_cluster->levels_tbl[0];

	for(threshold = 10; threshold <= 80; threshold += 10)
	{
		attr->u_threshold = threshold;

		switch(threshold)
		{
		case 10:
		case 20:
		case 30:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_placement_clstr_select,
						dqdt_cpu_free_select,
						3,
						logical->index);
			break;
		default:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_placement_clstr_select,
						dqdt_cpu_min_usage_select,
						3,
						logical->index);
		}

		if(err == 0) break;
	}

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
	attr->m_threshold = 1024;
	attr->origin = current_cluster->levels_tbl[0];

	for(threshold = 10; threshold <= 110; threshold += 20)
	{
		attr->u_threshold = threshold;

		switch(threshold)
		{
		case 10:
		case 70:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_task_placement_child_select,
						dqdt_task_placement_clstr_select,
						dqdt_cpu_free_select,
						DQDT_MAX_DEPTH,
						5);
			break;
    
		case 30:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_task_placement_child_select,
						dqdt_task_placement_clstr_select,
						dqdt_cpu_min_usage_select,
						DQDT_MAX_DEPTH,
						5);
			break;

		default:
			err = dqdt_up_traversal(logical,
						attr,
						dqdt_placement_child_select,
						dqdt_placement_clstr_select,
						dqdt_cpu_min_usage_select,
						DQDT_MAX_DEPTH,
						5);
      
		}

		if(err == 0) break;
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
