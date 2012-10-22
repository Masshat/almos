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


#include <segmentation.h>
#include <boot-info.h>
#include <config.h>
#include <string.h>
#include <mmu.h>
#include <dmsg.h>
#include <bits.h>

#define _ARCH_BIB_SIGNATURE_
#include <arch-bib.h>

#define ARCH_BIB_BASE    (BOOT_INFO_BLOCK)
#define KERNEL_HEADER    (KERNEL_BIN_IMG)

#define TO_STR(s) str(s)
#define str(s) #s

static void * boot_memcpy (uint_t *dma, void *dest, void *src, unsigned long size);

/* DMA mapped registers offset */
#define DMA_SRC_REG          0
#define DMA_DST_REG          1
#define DMA_LEN_REG          2
#define DMA_RESET_REG        3
#define DMA_IRQ_DISABLED     4

/* Boot Frozen Orders & Size */
struct boot_signal_block_s
{
	volatile uint_t boot_signal;
	uint_t pading1[15];
	volatile uint_t cpu_count;
	uint_t boot_tbl;
	uint_t pading2[CONFIG_CACHE_LINE_LENGTH -2];
	boot_info_t info;
}__attribute__((packed));

typedef struct boot_signal_block_s bsb_t;
typedef void (kernel_entry_t)(boot_info_t *info);
typedef struct arch_bib_header_s  header_info_t;
typedef struct arch_bib_cluster_s cluster_info_t;
typedef struct arch_bib_device_s  dev_info_t;

static void boot_signal_op(struct boot_info_s *info, uint_t cpu_nr)
{
	bsb_t *bsb;
	struct arch_bib_header_s *header;
	uint_t *tty;

	header = (struct arch_bib_header_s*) info->arch_info;
	tty = (uint_t*) header->bootstrap_tty;
  
	if((info == NULL) || (cpu_nr > info->onln_cpu_nr))
	{
		boot_dmsg(tty, "ERROR: %s: Invalid Arguments [0x%x, %d]\n", info, cpu_nr);
		while(1);
	}
  
	bsb = (bsb_t*) info->data;
	bsb->cpu_count = cpu_nr;
	bsb->boot_signal = CONFIG_BOOT_SIGNAL_SIGNATURE;
	cpu_wbflush();
}

static void boot_loader(int);

void boot_entry(int cpu_id)
{
	boot_loader(cpu_id);
}

static void boot_loader(int cpu_id)
{
	uint_t *src;
	uint_t *dst;
	uint_t size;
	boot_info_t boot_info;
	boot_info_t *info;
	boot_tbl_entry_t *boot_tbl;
	struct arch_bib_header_s *header;
	volatile bsb_t *bsb;
	kernel_info_t *kinfo;
	uint_t cpu_nr;
	uint_t *tty;
	uint_t *dma_base;
	uint_t cpu_lid;
	uint_t cid;
	uint_t local_cpu_nr;
	uint_t local_onln_cpu_nr;
	cluster_info_t *clusters;

	header = (struct arch_bib_header_s*) ARCH_BIB_BASE;
	kinfo  = (kernel_info_t*) KERNEL_HEADER;

	if(strcmp(header->signature, arch_bib_signature))
		while(1);			/* no other way to die !! */
  
	tty = (uint_t*) header->bootstrap_tty;

	bsb = (volatile bsb_t *)(header->rsrvd_limit - CONFIG_BSB_OFFSET);
  
	local_cpu_nr = header->cpu_nr;
	cid = cpu_id / local_cpu_nr;
	cpu_lid = cpu_id % local_cpu_nr;

	clusters = (cluster_info_t*) ((uint_t)header + sizeof(header_info_t));
	local_onln_cpu_nr = clusters[cid].cpu_nr;

	if(cpu_id == header->bootstrap_cpu)
	{    
		if(kinfo->signature != KERNEL_SIGNATURE)
			while(1);

		cpu_nr = header->onln_cpu_nr;
    
		boot_dmsg(tty, "\nAlmOS Bootloader\t\t\t[ STARTED ]\n\n");
		memset((void*)bsb, 0, sizeof(*bsb));

		src  = (uint_t *) kinfo;
		dst  = (uint_t *) header->rsrvd_start;
		size = kinfo->data_end - kinfo->text_start;
		dma_base = (uint_t*) header->bootstrap_dma;

		boot_dmsg(tty, "Loading Kernel Image");
		boot_memcpy(dma_base, dst, src, size);
    
		boot_dmsg(tty, "\t\t\t[ OK ]\nLoading Boot Information Block");
		info = &((bsb_t*)bsb)->info;
		size = ARROUND_UP(header->size, 0x1000);
		info->reserved_start = header->rsrvd_limit - 0x1000 - size;
		info->arch_info  = info->reserved_start;
		boot_memcpy(dma_base, (uint_t*)info->arch_info, header, header->size);

		boot_dmsg(tty, "\t\t[ OK ]\nSetup Kernel Boot Information");
		size = (sizeof(boot_tbl_entry_t) * header->onln_clstr_nr);
		size = ARROUND_UP(size, 0x1000);
		info->reserved_start -= size;
		info->boot_tbl = info->reserved_start;
		bsb->boot_tbl = info->boot_tbl;

		info->text_start = header->rsrvd_start;
		info->text_end = (kinfo->text_end - kinfo->text_start) + info->text_start;
		info->data_start = (kinfo->data_start - kinfo->text_start) + info->text_start;
		info->data_end = (kinfo->data_end - kinfo->text_start) + info->text_start;
		info->brom_start = CONFIG_BROM_START;
		info->brom_end = CONFIG_BROM_END;
		info->reserved_end = header->rsrvd_limit;
		info->onln_cpu_nr = cpu_nr;
		info->onln_clstr_nr = header->onln_clstr_nr;

		info->local_cpu_nr = local_cpu_nr;
		info->local_onln_cpu_nr = local_onln_cpu_nr;
		info->local_cpu_id = cpu_lid;
		info->local_cluster_id = cid;

		info->boot_cluster_id = cid;
		info->boot_cpu_id = cpu_id;
		info->boot_signal = &boot_signal_op;
		info->data = (void*)bsb;
   
		boot_dmsg(tty, "\t\t[ OK ]\nPreparing Kernel's Memory Layout");
		memory_init(tty, info, kinfo);

		boot_dmsg(tty, "\t[ OK ]\n\nBooting AlmOS Kernel ...\n\n", info->boot_cluster_id, info->boot_cpu_id);
		boot_dmsg(tty, "Boot Info:\n\tArch Info %x\n\tText <0x%x : 0x%x>\n\tData <0x%x : 0x%x>\n\tReserved <0x%x : 0x%x>\n\tBSC %d, BSP %d, CPU NR %d, MMU ON\n",
			  info->arch_info,
			  info->text_start, info->text_end,
			  info->data_start, info->data_end,
			  info->reserved_start, info->reserved_end,
			  info->boot_cluster_id, info->boot_cpu_id, info->onln_cpu_nr);
    
		boot_dmsg(tty, "\tKernel Entry Point @0x%x\n\n", kinfo->entry_addr);
    
		mmu_activate(info->boot_pgdir, kinfo->entry_addr, (uint_t)info);
		*tty = '?';
	}
	else
	{
		info = &boot_info;
		memcpy(info, &((bsb_t*)bsb)->info, sizeof(*info));
 
		boot_tbl = (boot_tbl_entry_t*)info->boot_tbl;
		info->reserved_end = boot_tbl[cid].reserved_end;
		info->reserved_start = boot_tbl[cid].reserved_start;
		info->local_cpu_nr = local_cpu_nr;
		info->local_onln_cpu_nr = local_onln_cpu_nr;
		info->local_cpu_id = cpu_lid;
		info->local_cluster_id = cid;

		if(bsb->cpu_count <= cpu_id)
			cpu_shutdown();

		cpu_goto_entry(kinfo->entry_addr, (uint_t)info);
		*tty = '!';
	}
}

static void * boot_memcpy (uint_t *dma, void *dest, void *src, unsigned long size)
{
	register uint_t i = 0;
	volatile uint_t *dma_base = (volatile uint_t *)dma;

#if CONFIG_BOOT_USE_DMA
	volatile uint_t cntr = 0;
   
	*(dma_base + DMA_RESET_REG) = 0;
	*(dma_base + DMA_SRC_REG) = (uint_t) src;
	*(dma_base + DMA_DST_REG) = (uint_t) dest;
	*(dma_base + DMA_IRQ_DISABLED) = 1;
	*(dma_base + DMA_LEN_REG) = size;

	do
	{      
		for(i=0; i < size/CONFIG_CACHE_LINE_SIZE; i++)
			cntr++;	/* wait */
		
	}while( *(dma_base + DMA_LEN_REG) != 0 );
 
#else
	register uint_t isize;
	isize = size >> 2;

	for(i=0; i< isize; i++)
		*((uint_t*) dest + i) = *((uint_t*) src + i);
   
	for(i= isize << 2; i< size; i++)
		*((char*) dest + i) = *((char*) src + i);

#endif	/* CONFIG_BOOT_USE_DMA */

	return dest;
}
