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

#include <kminiShell.h>
#include <list.h>
#include <thread.h>
#include <task.h>
#include <signal.h>

error_t kill_func(void *param)
{
  struct task_s *task;
  ms_args_t *args;
  uint32_t argc;
  uint_t sig;
  uint_t pid;
  error_t err;

  args  = (ms_args_t *) param;
  argc = args->argc;
  err = 0;

  if(argc != 3)
  {
    ksh_print("Missing signal number or process pid\n");
    return EINVAL;
  }

  sig = atoi(args->argv[1]);
  pid = atoi(args->argv[2]);

  if((sig == 0)  || (sig >= SIG_NR))
  {
    ksh_print("Unknown signal number: %d\n", sig);
    return EINVAL;
  }

  if((task = task_lookup(pid)) == NULL)
  {
    ksh_print("No such process: %d\n", pid);
    return ESRCH;
  }

  if((err = signal_rise(task, sig)))
    ksh_print("Failed to rise signal %d on task %d\n", sig, pid);

  return err;
}
