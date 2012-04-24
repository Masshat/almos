/*
 * boot-info.h - definition of boot-time informations passed to the kernel
 *
 * Copyright (c) 2008,2009,2010,2011,2012 Ghassan Almaless
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

#ifndef _BOOT_INFO_H_
#define _BOOT_INFO_H_

#define KERNEL_SIGNATURE   0xA5A5A5A5

#ifndef _ALMOS_ASM_
#include <types.h>

typedef struct kernel_info_s
{
	uint_t signature;
	uint_t text_start;
	uint_t text_end;
	uint_t data_start;
	uint_t data_end;
	uint_t entry_addr;
}kernel_info_t;

struct boot_info_s;

typedef void (boot_signal_t)(struct boot_info_s*, uint_t);

struct boot_info_s
{
	/* Loaded Physical Address */
	uint_t text_start;
	uint_t text_end;
	uint_t data_start;
	uint_t data_end;
	uint_t brom_start;
	uint_t brom_end;
	uint_t reserved_start;
	uint_t reserved_end;
	uint_t boot_pgdir;
	uint_t boot_tbl;

	/* Virtual Address of BIB info */
	uint_t arch_info;
  
	/* Total online Cluster & CPU numbers */
	uint_t onln_cpu_nr;
	uint_t onln_clstr_nr;

	/* Current Cluster Infor */
	uint_t local_cpu_nr;
	uint_t local_onln_cpu_nr;
	uint_t local_cpu_id;
	uint_t local_cluster_id;

	/* Bootstrap cluster & CPU ids */
	uint_t boot_cluster_id;
	uint_t boot_cpu_id;

	/* Function to call to boot ohter CPUs */
	boot_signal_t *boot_signal;

	/* Private Data */
	void *data;
};

typedef struct boot_info_s boot_info_t;

/* Boot Frozen Orders & Size */
struct boot_tbl_entry_s
{
	uint_t pgdir;
	uint_t reserved_end;
	uint_t reserved_start;
};

typedef struct boot_tbl_entry_s boot_tbl_entry_t;

#endif  /* _ALMOS_ASM_ */

#endif	/* _BOOT_INFO_H_ */
