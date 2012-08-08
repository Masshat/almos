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
  
   UPMC / LIP6 / SOC (c) 2009
   Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
*/

#define _CONFIG_H_
#include <arch-config.h>

vaddr = CONFIG_USR_OFFSET;

ENTRY(_start)

SECTIONS
{
  .text vaddr : { 
    *(.init) 
    *(.text) 
    . = ALIGN(16*4096); 
    __text_end = .;
  }
 
 . = ALIGN(2*1024*1024);
 
 .data : { 
    *(.rodata*) 
      *(.eh_frame) 
      *(.data) 
      *(.sdata) 
      . = ALIGN(4); 
    __bss_start = .; 
    *(.sbss) 
      *(.scommon) 
      *(.bss) 
      *(COMMON) 
      . = ALIGN(16*4096);
    __bss_end = .;
  }
}

INPUT(crt0.o)
