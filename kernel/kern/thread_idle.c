/*
 * kern/thread_idle.c - Default thread executed in the absence of any ready-thread
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

#include <types.h>
#include <cpu.h>
#include <cluster.h>
#include <atomic.h>
#include <mcs_sync.h>
#include <rt_timer.h>
#include <cpu-trace.h>
#include <kmem.h>
#include <task.h>
#include <vfs.h>
#include <thread.h>
#include <event.h>
#include <ppm.h>
#include <kcm.h>
#include <page.h>
#include <dqdt.h>

extern mcs_barrier_t boot_sync;

void* thread_idle(void *arg)
{
	extern uint_t __ktext_start;
	register uint_t id;
	register uint_t cpu_nr;
	register struct thread_s *this;
	register struct cpu_s *cpu;
	struct thread_s *thread;
	register struct page_s *reserved_pg;
	register uint_t reserved;
	kthread_args_t *args;
	bool_t isBSCPU;
	uint_t tm_now;
	uint_t count;
	error_t err;

	this    = current_thread;
	cpu     = current_cpu;
	id      = cpu->gid;
	cpu_nr  = arch_onln_cpu_nr();
	args    = (kthread_args_t*) arg;
	isBSCPU = (cpu == cpu->cluster->bscpu);

	cpu_trace_write(cpu, thread_idle_func);

	if(isBSCPU)
		pmm_tlb_flush_vaddr((vma_t)&__ktext_start, PMM_UNKNOWN);

	cpu_set_state(cpu, CPU_ACTIVE);
	rt_timer_read(&tm_now);
	this->info.tm_born = tm_now;      
	this->info.tm_tmp  = tm_now;
	//// Reset stats /// 
	cpu_time_reset(cpu);
	////////////////////

	mcs_barrier_wait(&boot_sync);

	printk(INFO, "INFO: Starting Thread Idle On Core %d\tOK\n", cpu->gid);

	if(isBSCPU && (id == args->val[2]))
	{
		for(reserved = args->val[0]; reserved < args->val[1]; reserved += PMM_PAGE_SIZE)
		{
			reserved_pg = ppm_ppn2page(&cpu->cluster->ppm, reserved >> PMM_PAGE_SHIFT);
			page_state_set(reserved_pg, PGINIT);       
			ppm_free_pages(reserved_pg);
		}
	}

	thread = kthread_create(this->task, 
				&thread_event_manager, 
				NULL, 
				cpu->cluster->id, 
				cpu->lid);

	if(thread == NULL)
		PANIC("Failed to create default events handler Thread for CPU %d\n", id);

	thread->task   = this->task;
	cpu->event_mgr = thread;
	wait_queue_init(&thread->info.wait_queue, "Events");

	err = sched_register(thread);
	assert(err == 0);

	sched_add_created(thread);

	if(isBSCPU)
	{
		dqdt_update();
#if 0
		thread = kthread_create(this->task, 
					&cluster_manager_thread,
					cpu->cluster, 
					cpu->cluster->id, 
					cpu->lid);

		if(thread == NULL)
		{
			PANIC("Failed to create cluster manager thread, cid %d, cpu %d\n", 
			      cpu->cluster->id, 
			      cpu->gid);
		}

		thread->task          = this->task;
		cpu->cluster->manager = thread;
		wait_queue_init(&thread->info.wait_queue, "Cluster-Mgr");

		err = sched_register(thread);
		assert(err == 0);

		sched_add_created(thread);

#endif

		if(clusters_tbl[cpu->cluster->id].flags & CLUSTER_IO)
		{
			thread = kthread_create(this->task, 
						&kvfsd, 
						NULL, 
						cpu->cluster->id, 
						cpu->lid);
       
			if(thread == NULL)
			{
				PANIC("Failed to create KVFSD on cluster %d, cpu %d\n", 
				      cpu->cluster->id, 
				      cpu->gid);
			}

			thread->task  = this->task;
			wait_queue_init(&thread->info.wait_queue, "KVFSD");
			err           = sched_register(thread);
			assert(err == 0);
			sched_add_created(thread);
			printk(INFO,"INFO: kvfsd has been created\n");
		}
	}

	cpu_set_state(cpu,CPU_IDLE);

	while (true)
	{
		cpu_disable_all_irq(NULL);
     
		if((event_is_pending(&cpu->re_listner)) || (event_is_pending(&cpu->le_listner)))
		{
			wakeup_one(&cpu->event_mgr->info.wait_queue, WAIT_ANY);
		}
 
		sched_idle(this);

		count = sched_runnable_count(&cpu->scheduler);

		cpu_enable_all_irq(NULL);

		if(count != 0)
			sched_yield(this);
     
		//arch_set_power_state(cpu, ARCH_PWR_IDLE);
	}

	return NULL;
}

