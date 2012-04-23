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
   Copyright Ghassan Almaless <ghassan.almaless@lip6.fr>
*/

#ifndef _SOCLIB_TTY_H_
#define _SOCLIB_TTY_H_

#include <driver.h>
#include <scheduler.h>
#include <rwlock.h>

/* TTY mapped registers offset */
#define TTY_WRITE_REG      0
#define TTY_STATUS_REG     1
#define TTY_READ_REG       2

struct device_s;
struct fifomwmr_s;

struct thread_s;

struct tty_context_s
{
  struct rwlock_s in_rwlock;
  struct rwlock_s out_rwlock;
  struct task_s *rd_owner;
  struct task_s *wr_owner;
  uint_t id;
  unsigned int eol;		/* End Of Line */
  dev_request_t *pending_rq;
  struct wait_queue_s wait_queue;
  struct fifomwmr_s *tty_buffer;
};

extern driver_t soclib_tty_driver;

#endif
