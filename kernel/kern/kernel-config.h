/*
 * kern/kernel-config.h - global kernel configurations
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

#ifndef _KERNEL_CONFIG_H_
#define _KERNEL_CONFIG_H_

#ifndef _CONFIG_H_
#error This config-file is not to be included directely, use config.h instead
#endif


////////////////////////////////////////////////////
//             KERNEL REVISION INFO               //
////////////////////////////////////////////////////
#define CONFIG_ALMOS_VERSION  "AlmOS-v2-2011"
////////////////////////////////////////////////////

////////////////////////////////////////////////////
//        KERNEL SUBSYSTEMS CONFIGURATIONS        //
////////////////////////////////////////////////////
#define CONFIG_BOOT_USE_DMA              yes
#define CONFIG_FIFO_SUBSYSTEM            no
#define CONFIG_ROOTFS_IS_EXT2            no
#define CONFIG_ROOTFS_IS_VFAT            yes

////////////////////////////////////////////////////

////////////////////////////////////////////////////
//         TASK MANAGEMENT CONFIGURATIONS         //
////////////////////////////////////////////////////
#define CONFIG_TASK_MAX_NR               32
#define CONFIG_TASK_FILE_MAX_NR          128
#define CONFIG_TASK_CHILDS_MAX_NR        32
#define CONFIG_TASK_ARGS_PAGES_MAX_NR    32
#define CONFIG_TASK_HEAP_MAX_SIZE        0x10000000

////////////////////////////////////////////////////
//          KERNEL GENERAL CONFIGURATIONS         //
////////////////////////////////////////////////////
#define CONFIG_DEV_VERSION               yes
#define CONFIG_KERNEL_REPLICATE          yes
#define CONFIG_REMOTE_FORK               yes
#define CONFIG_REMOTE_THREAD_CREATE      yes
#define CONFIG_SHOW_BOOT_BANNER          yes
#define CONFIG_MAX_CLUSTER_NR            256
#define CONFIG_MAX_CLUSTER_ROOT          16
#define CONFIG_MAX_CPU_PER_CLUSTER_NR    4
#define CONFIG_MAX_DQDT_DEPTH            4
#define CONFIG_DQDT_LEVELS_NR            5
#define CONFIG_DQDT_MGR_PERIOD           3
#define CONFIG_DQDT_ROOTMGR_PERIOD       3
#define CONFIG_CLUSTER_KEYS_NR           8
#define CONFIG_REL_LFFB_SIZE             32
#define CONFIG_VFS_NODES_PER_CLUSTER     40
#define CONFIG_SCHED_THREADS_NR          32
#define CONFIG_BARRIER_WQDB_NR           4
#define CONFIG_BARRIER_ACTIVE_WAIT       no
#define CONFIG_BARRIER_BORADCAST_UREAD   no
#define CONFIG_MONO_CPU                  no
#define CONFIG_PPM_USE_PRIO              yes
#define CONFIG_PPM_USE_SEQ_NEXT_CID      no
#define CONFIG_AUTO_NEXT_TOUCH           yes

////////////////////////////////////////////////////
//          KERNEL DEBUG CONFIGURATIONS           //
////////////////////////////////////////////////////
#define CONFIG_DMSG_LEVEL                DMSG_DEBUG
#define CONFIG_XICU_USR_ACCESS           no
#define CONFIG_SPINLOCK_CHECK            no
#define CONFIG_SPINLOCK_TIMEOUT          100
#define CONFIG_SHOW_SYSCALL_NR           no
#define CONFIG_SHOW_PAGEFAULT            no
#define CONFIG_SHOW_CPU_USAGE            no
#define CONFIG_SHOW_THREAD_DESTROY_MSG   yes
#define CONFIG_SHOW_PPM_PGALLOC_MSG      no
#define CONFIG_SHOW_VMM_LOOKUP_TM        no
#define CONFIG_SHOW_VMM_ERROR_MSG        no
#define CONFIG_SHOW_SPURIOUS_PGFAULT     no
#define CONFIG_SHOW_MIGRATE_MSG          no
#define CONFIG_SHOW_REMOTE_PGALLOC       no
#define CONFIG_SHOW_LOCAL_EVENTS         no
#define CONFIG_SHOW_EPC_CPU0             no
#define CONFIG_CPU_TRACE                 no
#define CONFIG_DQDT_DEBUG                no
#define CONFIG_LFFB_DEBUG                no
#define CONFIG_KHM_DEBUG                 no
#define CONFIG_KCM_DEBUG                 no
#define CONFIG_KMEM_DEBUG                no
#define CONFIG_MAPPER_DEBUG              no
#define CONFIG_SHOW_KMEM_INIT            no
#define CONFIG_MEM_CHECK                 no
#define CONFIG_THREAD_TIME_STAT          yes
#define CONFIG_SCHED_RR_CHECK            yes
#define CONFIG_FORK_DEBUG                no
#define CONFIG_VMM_DEBUG                 no
#define CONFIG_VMM_REGION_DEBUG          no
#define CONFIG_ELF_DEBUG                 no
#define CONFIG_VFAT_PGWRITE_ENABLE       no
#define CONFIG_VFAT_DEBUG                no
#define CONFIG_VFAT_INSTRUMENT           no
#define CONFIG_EXT2_DEBUG                no
#define CONFIG_METAFS_DEBUG              no
#define CONFIG_VFS_DEBUG                 no
#define CONFIG_DEVFS_DEBUG               no
#define CONFIG_SYSFS_DEBUG               no
#define CONFIG_BC_DEBUG                  no
#define CONFIG_BC_INSTRUMENT             no
#define CONFIG_LOCKS_DEBUG               no
#define CONFIG_SCHED_DEBUG               no
#define CONFIG_VERBOSE_LOCK              no
//////////////////////////////////////////////

//////////////////////////////////////////////
//      USER APPLICATION
//////////////////////////////////////////////
#define CONFIG_DEV_STDIN           "/dev/tty1"
#define CONFIG_DEV_STDOUT          "/dev/tty1"
#define CONFIG_DEV_STDERR          "/dev/tty2"
//////////////////////////////////////////////

#endif	/* _KERNEL_CONFIG_H_ */
