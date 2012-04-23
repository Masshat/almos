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
  
   UPMC / LIP6 / SOC (c) 2007, 2008, 2009
   Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
*/

#ifndef _CONFIG_H_
#define _CONFIG_H_

#define no    0
#define yes   1

#define DMSG_DEBUG         6
#define DMSG_INFO          5
#define DMSG_WARNING       4
#define DMSG_ASSERT        3
#define DMSG_BOOT          2
#define DMSG_ERROR         1
#define DMSG_NONE          0

//////////////////////////////////////////////
//       CPU RELATED CONFIGURATIONS         //
//////////////////////////////////////////////
#include <cpu-config.h>


//////////////////////////////////////////////
//      ARCH RELATED CONFIGURATIONS         //
//////////////////////////////////////////////
#include <arch-config.h>


//////////////////////////////////////////////
//     MEMORY MANAGEMENT CONFIGURATIONS     // 
//////////////////////////////////////////////
#include <mm-config.h>


//////////////////////////////////////////////
//      KERNEL CONFIGURATIONS               //
//////////////////////////////////////////////
#include <kernel-config.h>

//////////////////////////////////////////////
#endif	/* _CONFIG_H_ */
