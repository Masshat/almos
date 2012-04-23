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

#include <boot-info.h>
#include <mmu.h>
#include <dmsg.h>
#include <string.h>

#define _PMM_BIT_ORDER(x)       (1 << (x))

#define PMM_PRESENT             _PMM_BIT_ORDER(31)
#define PMM_READ                _PMM_BIT_ORDER(31)
#define PMM_HUGE                _PMM_BIT_ORDER(30)
#define MMU_LACCESSED           _PMM_BIT_ORDER(29)
#define MMU_RACCESSD            _PMM_BIT_ORDER(28)
#define PMM_ACCESSED            (MMU_LACCESSED | MMU_RACCESSD)
#define PMM_CACHED              _PMM_BIT_ORDER(27)
#define PMM_WRITE               _PMM_BIT_ORDER(26)
#define PMM_EXECUTE             _PMM_BIT_ORDER(25)
#define PMM_USER                _PMM_BIT_ORDER(24)
#define PMM_GLOBAL              _PMM_BIT_ORDER(23)
#define PMM_DIRTY               _PMM_BIT_ORDER(22)

#define MMU_PDIR_SHIFT          21
#define MMU_PDIR_MASK           0xFFE00000

#define PMM_PAGE_SHIFT          12
#define PMM_HUGE_PAGE_SHIFT     21
#define PMM_HUGE_PAGE_SIZE      0x200000

#define KATTR     (PMM_HUGE | PMM_READ | PMM_WRITE | PMM_EXECUTE | PMM_CACHED | PMM_GLOBAL | PMM_DIRTY | PMM_ACCESSED)
#define KDEV_ATTR (PMM_HUGE | PMM_READ | PMM_WRITE | PMM_GLOBAL | PMM_DIRTY | PMM_ACCESSED)

#define MMU_PDE(vma)            (((vma) & MMU_PDIR_MASK) >> MMU_PDIR_SHIFT)


static void pmm_map(uint_t *tty, 
		    uint_t *pgdir, 
		    uint_t vaddr, 
		    uint_t start, 
		    uint_t end, 
		    uint_t attr)
{
#if CONFIG_MEM_DEBUG
  boot_dmsg(tty, "\t<0x%x : 0x%x> -> <0x%x : 0x%x>\n", start, end, vaddr, vaddr + (end - start));
#endif

  end = end >> PMM_HUGE_PAGE_SHIFT;
  start = start >> PMM_HUGE_PAGE_SHIFT;
  end = (end == start) ? end + 1 : end;

  while(start < end)
  {   
    pgdir[MMU_PDE(vaddr)] = (attr ^ PMM_HUGE) | start;

#if CONFIG_MEM_DEBUG
    boot_dmsg(tty, "\t\tStart 0x%x, Vaddr 0x%x, pde_idx %d, pde 0x%x\n", 
	      start, vaddr, MMU_PDE(vaddr), pgdir[MMU_PDE(vaddr)]);
#endif

    vaddr += PMM_HUGE_PAGE_SIZE;
    start ++;
  }
}

void memory_init(uint_t *tty, boot_info_t *binfo, kernel_info_t *kinfo)
{
  if(binfo->reserved_start & 0x1000)
    binfo->reserved_start -= 0x1000;
  
  binfo->reserved_start -= 0x2000;
  binfo->boot_pgdir = binfo->reserved_start;

#if CONFIG_MEM_DEBUG  
  boot_dmsg(tty, "\n\tBoot PageDir @0x%x\n", binfo->boot_pgdir);
#endif

  memset((void*)binfo->boot_pgdir, 0, 0x2000);
  
  pmm_map(tty, 
	  (uint_t*)binfo->boot_pgdir, 
	  binfo->text_start, 
	  binfo->text_start, 
	  binfo->reserved_end, 
	  KATTR);

  pmm_map(tty,
	  (uint_t*)binfo->boot_pgdir, 
	  kinfo->text_start, 
	  binfo->text_start, 
	  binfo->reserved_end, 
	  KATTR);

  pmm_map(tty, 
	  (uint_t*)binfo->boot_pgdir, 
	  0xbfc00000, 
	  0xbfc00000, 
	  0xbfd00000, 
	  KATTR);
  
  pmm_map(tty,
	  (uint_t*)binfo->boot_pgdir, 
	  (uint_t)tty, 
	  (uint_t)tty, 
	  (uint_t)tty + 0x1000, 
	  KDEV_ATTR);
}


