/*
   This file is part of AlmOS.
  
   AlmOS is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
  
   AlmOS is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with AlmOS; if not, write to the Free Software Foundation,
   Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
  
   UPMC / LIP6 / SOC (c) 2008
   Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
*/

#include <errno.h>
#include <sys/syscall.h>
#include <cpu-syscall.h>
#include <unistd.h>
#include <sys/mman.h>

int madvise(void *start, size_t length, int advice)
{
  if((start == NULL) || (advice < 0) || (advice > MADV_MIGRATE))
    return EINVAL;

  return (int) cpu_syscall(start, (void*)length, (void*)advice, NULL, SYS_MADVISE);
}
