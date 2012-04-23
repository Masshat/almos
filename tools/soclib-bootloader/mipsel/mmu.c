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

#include <mmu.h>

uint_t cpu_get_id(void)
{
  register unsigned int proc_id;

  asm volatile ("mfc0    %0,  $0" : "=r" (proc_id));
  
  return proc_id;
}

void cpu_invalid_dcache_line(void *ptr)
{
  __asm__ volatile
    ("cache    %0,     (%1)              \n"
     : : "i" (0x11) , "r" (ptr)
    );
}

void cpu_wbflush(void)
{
  __asm__ volatile
    ("sync                               \n"::);
}

void cpu_power_idle(void)
{
  __asm__ volatile
    ("wait                               \n"::);
}

void cpu_shutdown(void)
{
  __asm__ volatile
    ("1:                                 \n"
     "wait                               \n"
     "nop                                \n"
     "j      1b                          \n"
     "nop                                \n"
     ::);
}

void cpu_goto_entry(uint_t entry, uint_t arg)
{
  asm volatile
    (".set noreorder          \n"
     "or     $31,  $0,  %0    \n"
     "or     $4,   $0,  %1    \n"
     "addiu  $29,  $29, -4    \n"
     "jr     $31              \n"
     "sw     $4,  0($29)      \n"
     ".set reorder            \n"
     :: "r" (entry), "r" (arg));
}

void mmu_activate(unsigned long pgdir, unsigned long kentry, unsigned long arg)
{
  pgdir = pgdir >> 13;

  asm volatile 
    (".set noreorder          \n"
     "or     $26,  $0,  %0    \n"
     "or     $31,  $0,  %1    \n"
     "addiu  $29,  $29, -4    \n"
     "or     $4,   $0,  %2    \n"
     "sw     $4,   0($29)     \n"
     "mtc2   $26,  $0         \n"
     "nop                     \n"
     "ori    $27,  $0,  0xF   \n"
     "mtc2   $27,  $1         \n"
     "jr     $31              \n"
     "nop                     \n"
     ".set reorder            \n"
     :: "r"(pgdir), "r"(kentry), "r" (arg));
}
