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

#define yes 1
#define no  0

/////////////////////////////////////////////////
//         CPU Related Configurations          //
/////////////////////////////////////////////////
#include <cpu-config.h>
/////////////////////////////////////////////////


/////////////////////////////////////////////////
//           General Configurations            //
/////////////////////////////////////////////////
#define CONFIG_BOOT_SIGNAL_SIGNATURE  0xA5A5A5A5
#define CONFIG_BOOT_USE_DMA           yes
#define CONFIG_BSB_OFFSET             512
/////////////////////////////////////////////////


/////////////////////////////////////////////////
//           DEBUG Configurations              //
/////////////////////////////////////////////////
#define  CONFIG_MEM_DEBUG             no
/////////////////////////////////////////////////


#endif	/* _CONFIG_H_ */
