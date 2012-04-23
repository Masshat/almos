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
  
   UPMC / LIP6 / SOC (c) 2009
   Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
*/

#ifndef _MMU_H_
#define _MMU_H_
#include <boot-info.h>

void memory_init(uint_t *tty, boot_info_t *binfo, kernel_info_t *kinfo);
void mmu_activate(unsigned long pgdir, unsigned long kentry, unsigned long arg);
uint_t cpu_get_id(void);
void cpu_invalid_dcache_line(void *ptr);
void cpu_wbflush(void);
void cpu_power_idle(void);
void cpu_shutdown(void);
void cpu_goto_entry(uint_t entry, uint_t arg);
#endif	/* _MMU_H_ */
