/*
   This file is part of MutekP.
  
   MutekP is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
  
   MutekP is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with MutekP; if not, write to the Free Software Foundation,
   Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
  
   UPMC / LIP6 / SOC (c) 2008
   Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
*/

#include <list.h>
#include <thread.h>
#include <task.h>
#include <cpu.h>
#include <vfs.h>
#include <rwlock.h>
#include <system.h>
#include <wait_queue.h>

#define ksh_print(x, ...) printk(INFO,x, __VA_ARGS__)

void ps_print_task(struct task_s *task, uint_t *usr_nr, uint_t *sys_nr, uint_t *tasks_nr)
{
  struct thread_s *thread;
  struct list_entry *iter;
  struct list_entry *th_root;
  struct vfs_node_s *node;
  char *cmd;
  uint_t ppid;

  if(task != NULL)
  {
    th_root = &task->th_root;
    
    ppid = 0;
    cmd = "N/A";

    if(task->pid != 0)
      ppid = task->parent->pid;
    
    if(task->state == TASK_CREATE)
      cmd = "Under-Creation";
    else
      if(task->bin != NULL)
      {
	node = task->bin->f_node;
	cmd = node->n_name;
      }

    printk(INFO,"\n[PID] %d [PPID] %d [Children] %d [Command] %s\n",
	   task->pid, 
	   ppid,
	   atomic_get(&task->childs_nr),
	   cmd);

    if(task->state == TASK_CREATE)
      return;

    spinlock_lock(&task->th_lock);

    list_foreach_forward(th_root, iter)
    {
      thread = list_element(iter, struct thread_s, rope);
      assert(thread->signature == THREAD_ID);

      ksh_print("  |__ [TID] %x [ORD] %d [%s] [CPU] %d [TICKs] %d [Sched] %d [tm_sys] %u [tm_usr] %u [tm_sleep] %u %s [ %s ]\n",
		thread,
		thread->info.order,
		thread_type_name[thread->type], 
		thread_current_cpu(thread)->gid, 
		thread->ticks_nr,
		thread->info.sched_nr,
		thread->info.tm_sys,
		thread->info.tm_usr,
		thread->info.tm_sleep,
		thread_state_name[thread->state], 
		(thread->info.queue == NULL) ? "N/A" : thread->info.queue->name);

      if(thread->type == PTHREAD)
	*usr_nr = *usr_nr + 1;
      else
	*sys_nr = *sys_nr + 1;
    }

    if(!(list_empty(th_root)))
      *tasks_nr = *tasks_nr + 1;

    spinlock_unlock(&task->th_lock);
  }
}

error_t ps_func(void *param)
{
  uint_t usr_nr;
  uint_t sys_nr;
  uint_t tasks_nr;
  uint_t pid;
  struct task_s *task;

  usr_nr = 0;
  sys_nr = 0;
  tasks_nr = 0;

  for(pid=0; pid < CONFIG_TASK_MAX_NR; pid ++)
  {
    task = task_lookup(pid);
    ps_print_task(task, &usr_nr, &sys_nr, &tasks_nr);
  }

  ksh_print("\nTotal Active        Tasks   : %d\n", tasks_nr);
  ksh_print("Total Active User   Threads : %d\n", usr_nr);
  
  return 0;
}
