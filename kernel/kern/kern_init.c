/*
 * kern/kern_init.c - kernel parallel initialization
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

#include <almOS-date.h>
#include <types.h>
#include <system.h>
#include <kdmsg.h>
#include <cpu.h>
#include <atomic.h>
#include <mcs_sync.h>
#include <list.h>
#include <thread.h>
#include <scheduler.h>
#include <kmem.h>
#include <config.h>
#include <task.h>
#include <cluster.h>
#include <devfs.h>
#include <sysfs.h>
#include <string.h>
#include <ppm.h>
#include <page.h>
#include <device.h>
#include <boot-info.h>
#include <dqdt.h>

#define BOOT_SIGNAL  0xA5A5B5B5
#define die(args...) do {boot_dmsg(args); while(1);} while(0)

mcs_barrier_t boot_sync;
static mcs_barrier_t init_sync;
static mcs_barrier_t kmem_sync;
static kthread_args_t idle_args;

static void print_boot_banner(void);

void kern_init (boot_info_t *info)
{
	register struct cpu_s *cpu;
	register struct cluster_s *cluster;
	register uint_t cpu_lid;
	register uint_t cpu_gid;
	register uint_t cluster_id;
	struct task_s *task0;
	struct thread_s *thread;
	struct thread_s thread0;
	error_t err;
	uint_t retval;
	uint_t tm_start;
	uint_t i;

	cpu_gid    = cpu_get_id();  
	cluster_id = info->local_cluster_id;
	cpu_lid    = info->local_cpu_id;
	task0      = task_lookup(0);

	memset(&thread0, 0, sizeof(thread0));
	cpu_set_current_thread(&thread0);
	thread0.type  = TH_IDLE;
	thread0.state = S_KERNEL;
	thread0.task  = task0;
 
	if(cpu_gid == info->boot_cpu_id)
	{
		mcs_lock_init(&boot_lock, "Boot-DMSG");
		mcs_barrier_init(&boot_sync, "BOOT", info->onln_cpu_nr);
		mcs_barrier_init(&init_sync, "INIT", info->onln_clstr_nr);
		mcs_barrier_init(&kmem_sync, "KMEM", info->onln_clstr_nr);
		clusters_init();
		task_bootstrap_init(info);
		arch_memory_init(info);
		task_manager_init();  
		devfs_root_init();
		sysfs_root_init();
		idle_args.val[0] = info->reserved_start;
		idle_args.val[1] = info->reserved_end;
		idle_args.val[2] = cpu_gid;
		info->boot_signal(info, info->onln_cpu_nr);
	}
  
	retval = atomic_add(&clusters_tbl[cluster_id].cntr, 1);

	if(retval != 0)
	{
		/* Wait for boot-signal from bootstrap CPU of local cluster */
		while(clusters_tbl[cluster_id].boot_signal != BOOT_SIGNAL);
		retval = cpu_time_stamp() + 2000;
		while(cpu_time_stamp() < retval)
			;
	}
	else
	{
		arch_init(info);
		mcs_barrier_wait(&init_sync);

		if(cluster_id == info->boot_cluster_id)
		{
			task_bootstrap_finalize(info);
			clusters_sysfs_register();
			kdmsg_init();
			printk(INFO, "All clusters have been Initialized [ %d ]\n", cpu_time_stamp());
			print_boot_banner();
		}

		mcs_barrier_wait(&kmem_sync);

		if(cluster_id == info->boot_cluster_id)
		{
			printk(INFO, "INFO: Building Distributed Quaternary Decision Tree (DQDT)\n");
      
			tm_start = cpu_time_stamp();

			err = arch_dqdt_build(info);
      
			if(err)
				PANIC("Failed to build DQDT, err %d\n", err);

			printk(INFO, "INFO: DQDT has been built [%d]\n", cpu_time_stamp() - tm_start);
		}

		mcs_barrier_wait(&kmem_sync);

		dqdt_init(info);

		cluster = clusters_tbl[cluster_id].cluster;

#if CONFIG_KERNEL_REPLICATE
		tm_start = cpu_time_stamp();
		printk(INFO, "INFO: kernel replication started on cid %d [%d]\n", cluster_id, tm_start);
#endif

		err = task_bootstrap_replicate(info);

#if CONFIG_KERNEL_REPLICATE
		if(err)
		{
			printk(INFO, "WARNING: %s: cid %d, cpu %d, replication failed, err %d [%d]\n", 
			       __FUNCTION__,
			       cluster->id,
			       cpu_get_id(),
			       err,
			       cpu_time_stamp());
		}
		else
		{
			printk(INFO,
			       "INFO: kernel replication done on cid %d [%d]\n",
			       cluster_id, cpu_time_stamp() - tm_start);
		}
#endif	/* CONFIG_KERNEL_REPLICATE */

		mcs_barrier_wait(&kmem_sync);

		///////////////////////////////////
		for(i = 0; i < cluster->cpu_nr; i++)
		{
			thread = kthread_create(cluster->task, &thread_idle, &idle_args, cluster_id, i);

			thread->state = S_KERNEL;
			thread->type  = TH_IDLE;
			cpu = &cluster->cpu_tbl[i];
			cpu_set_thread_idle(cpu,thread);
		}
		///////////////////////////////////

		clusters_tbl[cluster_id].boot_signal = BOOT_SIGNAL;
	}
  
	cluster = clusters_tbl[cluster_id].cluster;
	cpu = &cluster->cpu_tbl[cpu_lid];
	thread = cpu_get_thread_idle(cpu);
  
	cpu_context_load(&thread->pws);
}


#if CONFIG_SHOW_BOOT_BANNER
static void print_boot_banner(void)
{ 
	printk(BOOT,"\n           ____        ___        ___       ___    _______    ________     \n");
	printk(BOOT,"          /    \\      |   |      |   \\     /   |  /  ___  \\  /  ____  |   \n");
	printk(BOOT,"         /  __  \\      | |        |   \\___/   |  |  /   \\  | | /    |_/   \n");
	printk(BOOT,"        /  /  \\  \\     | |        |  _     _  |  | |     | | | |______     \n");
	printk(BOOT,"       /  /____\\  \\    | |        | | \\   / | |  | |     | | \\______  \\ \n");
	printk(BOOT,"      /   ______   \\   | |     _  | |  \\_/  | |  | |     | |  _     | |    \n");
	printk(BOOT,"     /   /      \\   \\  | |____/ | | |       | |  |  \\___/  | | \\____/ |  \n");
	printk(BOOT,"    /_____/    \\_____\\|_________/|___|     |___|  \\_______/  |________/   \n");
 
	printk(BOOT,"\n\n\t\t Advanced Locality Management Operating System\n");
	printk(BOOT,"\t\t   UPMC/LIP6/SoC (%s)\n\n\n",ALMOS_DATE);
	//printk(BOOT,"\t\t\tUPMC/LIP6/SoC (%s)\n\n\n",ALMOS_DATE);
}
#else
static void print_boot_banner(void)
{
}
#endif
