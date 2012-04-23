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
  
   UPMC / LIP6 / SOC (c) 2008
   Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
*/

#include <stdint.h>
#include <vfs.h>
#include <libk.h>
#include <kminiShell.h>
 
error_t pwd_func(void *param)
{
  register struct vfs_node_s *current;
  register ssize_t count, len;
  register error_t err;
  char buff[128];

  err = 0;
  count = 128 - 1;
  len = 0;
   
  current = ms_n_cwd;
  buff[count] = 0;

  while((current->n_parent != NULL) && (count > 0))
  {
    len = strlen(current->n_name);
    count -= len;
    if(count < 1) break;
    strncpy(&buff[count], current->n_name, len);
    buff[--count] = '/';
    current = current->n_parent;
  }

  if(current->n_parent != NULL)
    return ERANGE;
   
  if(len == 0)
  {
    buff[0]='/';
    buff[1] = 0;
    count = 0;
  }

  ksh_print("%s\n",&buff[count]);
  return 0;
}

