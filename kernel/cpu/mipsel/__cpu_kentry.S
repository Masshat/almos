/*
 * __cpu_kentry.S - unified kernel entry point
 * 
 * Copyright (c) 2007,2008,2009,2010,2011,2012 Ghassan Almaless
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

#include <cpu-regs.h>
#define _ALMOS_ASM_
#include <boot-info.h>

#-------------------------------------------------------------------------------
# Kernel entry point (Exception/Interrupt/System call) for MIPS32 ISA compliant 
# processors. The base address of the segment containing this code
#-------------------------------------------------------------------------------
	.section  .kheader, "ax",@progbits
	
	.extern __ktext_start
	.extern __ktext_end
	.extern __kdata_start
	.extern __kdata_end
	.extern kern_init
	.org 0x00
	.ent kheader
	.global kheader

kheader:	
	header:	.word KERNEL_SIGNATURE, __ktext_start, __ktext_end, __kdata_start, __kdata_end, kern_init
	
	.end kheader
	
	.section   .kentry,"ax",@progbits
	.extern cpu_do_interrupt
	.extern cpu_do_exception
	.extern cpu_do_syscall
	.org 0x168
	.ent  kentry
	.global kentry
	.set noat
	.set noreorder

#define SAVE_SIZE      REGS_NR*4
	
# Kernel Entry point
#-------------------------------------------------------------------------------
kentry:
	mfc0    $26,    $12                     # read SR
	andi    $26,    $26,       0x10         # KSU bitmask
	beq     $26,    $0,        KERNEL_MODE
	mfc0    $26,    $4,        2	        # read current thread pointer
	
LOAD_KERNEL_STACK:
	sw      $27,    (TLS_K1*4)($26)		# save user value
	sw      $29,    (SP*4)($26)     	# save user stack
	lw	$29,	(KSP*4)($26)		# read kernel stack
	j	UNIFIED_MODE
	or	$27,	$0,	$26		# pointer to uzone
	
KERNEL_MODE:
	addiu   $27,    $29,	-(SAVE_SIZE)
	sw	$29,	(SP*4)($27)
	or	$29,	$27,	$0
	
UNIFIED_MODE:	
	sw	$1,	(AT*4)($27)		
	sw	$2,	(V0*4)($27)
	sw	$3,	(V1*4)($27)
	sw	$4,	(A0*4)($27)
	sw	$5,	(A1*4)($27)
	sw	$6,	(A2*4)($27)
	sw	$7,	(A3*4)($27)
	sw	$8,	(T0*4)($27)
	sw	$9,	(T1*4)($27)
	sw	$10,	(T2*4)($27)
	sw	$11,	(T3*4)($27)
	sw	$12,	(T4*4)($27)
	sw	$13,	(T5*4)($27)
	sw	$14,	(T6*4)($27)
	sw	$15,	(T7*4)($27)
	sw	$24,	(T8*4)($27)
	sw	$25,	(T9*4)($27)
	sw	$16,	(S0*4)($27)
	mfc0	$16,    $14			# Read EPC
	sw	$17,	(S1*4)($27)
	mfc0    $17,    $13            		# read CR (used later)
	sw	$18,	(S2*4)($27)
	mfc0	$18,	$12			# Read current SR (used later)
	sw	$19,	(S3*4)($27)
	sw	$20,	(S4*4)($27)
	sw	$21,	(S5*4)($27)
	sw	$22,	(S6*4)($27)
	sw	$23,	(S7*4)($27)
	sw	$30,	(S8*4)($27)
	sw	$28,	(GP*4)($27)
	mflo	$14				# read LO
	andi    $1,     $17,	0x3F		# apply exception cause mask (used later)
	mfhi	$15				# read HI
	srl	$3,	$18,	5
	ori	$8,     $0,     0x20		# cause syscall
	sll	$3,	$3,	5		# put SR in kernel mode, IRQ disabled, clear exl
	or	$19,	$0,	$27
	sw	$31,	(RA*4)($27)		# save RA
	sw	$16,	(EPC*4)($27)		# Save EPC
	sw	$17,	(CR*4)($27)		# Save CR
	sw	$18,	(SR*4)($27)		# Save SR
	mtc0	$3,	$12			# Set new SR
	sw	$14,	(LO*4)($27)		# save LO
	beq	$8,	$1,	cause_sys	
	sw	$15,	(HI*4)($27)		# save HI
	mfc0	$5,	$15,	1		# second arg is cpu_id	
	or	$4,	$0,	$26		# first arg is this
	andi    $5,     $5,     0x1FF           # mask all but CPUnum
	beq     $1,    	$0,	cause_int
	or	$6,	$0,	$27		# 3th arg is regs_tbl
	
# Exceptions Handler
# ---------------------------------------------	
	la	$1,	cpu_do_exception
	jalr	$1	
	addiu	$29,	$29,   -3*4	
	j	kentry_exit
	addiu	$29,	$29,	3*4

# System Call Handler
# ------------------------------------------------
cause_sys:
	la	$14,	cpu_do_syscall
	addiu	$29,	$29,	-4
	jalr	$14
	or	$4,	$0,	$27
	j	kentry_exit
	or	$19,	$0,	$26
	
# Interrupts Handler
# ----------------------------------------------
cause_int:
	la	$1,	cpu_do_interrupt
	srl	$7,	$17,	10		# extract IP state, 4th arg
	addiu	$29,	$29,	-4*4		# cpu_interrupt has 4 arg
	jal	$1
	andi    $7,     $7,	0x3F		# 6 HW IRQ LINES, 2th arg is irq_state

# Kentry exit
# ----------------------------------------------
kentry_exit:
	or	$27,	$0,	$19
	lw	$1,	(AT*4)($27)		
	lw	$2,	(V0*4)($27)
	lw	$3,	(V1*4)($27)
	lw	$4,	(A0*4)($27)
	lw	$5,	(A1*4)($27)
	lw	$6,	(A2*4)($27)
	lw	$7,	(A3*4)($27)
	lw	$8,	(T0*4)($27)
	lw	$9,	(T1*4)($27)
	lw	$10,	(T2*4)($27)
	lw	$11,	(T3*4)($27)
	lw	$12,	(T4*4)($27)
	lw	$13,	(T5*4)($27)
	lw	$14,	(T6*4)($27)
	lw	$15,	(T7*4)($27)
	lw	$24,	(T8*4)($27)
	lw	$25,	(T9*4)($27)	
	lw	$19,	(S3*4)($27)
	lw	$20,	(S4*4)($27)
	lw	$21,	(S5*4)($27)
	lw	$22,	(S6*4)($27)
	lw	$23,	(S7*4)($27)
	lw	$30,	(S8*4)($27)
	lw	$28,	(GP*4)($27)
	lw	$31,	(RA*4)($27)
	lw	$16,	(EPC*4)($27)		# load EPC
	lw	$29,	(SP*4)($27)		# restore SP
	lw	$17,	(SR*4)($27)		# load SR
	mfc0	$18,	$12			# Read current SR
	andi	$17,	$17,	0x1F
	mtc0	$16,	$14			# restore EPC
	or	$18,	$18,	$17
	lw	$16,	(LO*4)($27)		# load LO
	mtc0	$18,	$12			# setup new SR
	lw	$17,	(HI*4)($27)		# load HI
	mtlo	$16				# restore LO
	mthi	$17				# restore HI	
	lw	$16,	(S0*4)($27)
	lw	$17,	(S1*4)($27)
	lw	$18,	(S2*4)($27)
	lw      $27,    (TLS_K1*4)($27)
	eret

  .set reorder
  .set at
  .end kentry
#-------------------------------------------------------------------------------

