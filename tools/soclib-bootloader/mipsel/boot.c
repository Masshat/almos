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


///////////////////////////////////////////
//            SYSTEM STARTUP             //
///////////////////////////////////////////

#include <config.h>
#include <segmentation.h>

#define ARCH_BIB_BASE    (BOOT_INFO_BLOCK)

#define TO_STR(s) str(s)
#define str(s) #s

void __attribute__ ((section(".boot"))) reset(void)
{
  __asm__ volatile (
    ".set noreorder                    \n"
    "li     $8,    0                   \n"
    "mtc0   $8,    $12	               \n"                    // Init. status Register
    "mfc0   $4,    $0	               \n"                    // $4  <->  CPU ID
    "lui    $8,    ("TO_STR(ARCH_BIB_BASE)") >> 16        \n"      
    "ori    $8,    $8, ("TO_STR(ARCH_BIB_BASE)") & 0xFFFF \n" // $8  <->  arch_info
    "lw     $5,    0($8)               \n"                    // $5  <->  BSP CPU_ID
    "lw     $9,    4($8)               \n"                    // $9  <->  rsrvd_limit
    "la     $31,   boot_entry          \n"	              // $31 <->  boot_entry
    "bne    $4,    $5,     1f          \n"
    "subu   $10,   $9,     0x200       \n"                    // $10 <->  BSP base addr
    "or     $29,   $0,     $10         \n"                    // Setup boot_stack
    "jr     $31                        \n"                    // BSP: goto boot_entry function
    "subu   $29,   $29,    4           \n"
    "1:                                \n"
    "lui    $11,   ("TO_STR(CONFIG_BOOT_SIGNAL_SIGNATURE)") >> 16         \n"
    "ori    $11,   $11, ("TO_STR(CONFIG_BOOT_SIGNAL_SIGNATURE)") & 0xFFFF \n"
    "lw     $12,   0($10)              \n"                    // read boot_signal
    "beq    $12,   $11,    2f          \n"
    "nop                               \n"
    "j      1b                         \n"
    "nop                               \n"
    "2:                                \n"
    "lw     $11,   17*4($10)           \n"                   // $11 <-> boot_tbl
    "lw     $12,   8($8)               \n"                   // $12 <-> per-cluster cpu_nr
    "or     $26,    $0,    $4          \n"
    "divu   $26,    $12                \n"
    "mflo   $13                        \n"                   // $13 <-> cid
    "mfhi   $5                         \n"                   // $5  <-> cpu_lid
    "li     $14,   12                  \n"                   // sizeof(boot_tbl_entry_t)
    "multu  $13,   $14                 \n"
    "mflo   $14                        \n"
    "addu   $14,   $14,     $11        \n"                   // $14 <-> boot_tbl_entry
    "lw     $15,   0($14)              \n"                   // $15 <-> pgdir
    "lw     $16,   4($14)              \n"                   // $16 <-> stack_top
    "li     $17,   0x1000              \n"                   // stack_size
    "multu  $5,    $17                 \n"
    "mflo   $17                        \n"
    "subu   $29,   $16,     $17        \n"                   // Setup stack ptr
    "srl    $15,   $15,     13         \n"
    "mtc2   $15,   $0                  \n"                   // Setup pgdir
    "ori    $26,   $0,      0xF        \n"
    "mtc2   $26,   $1                  \n"                   // Setup Cache & TLB
    "jr     $31                        \n"
    "subu   $29,   $29,     4          \n"
    ".set reorder                      \n" ::);
}
