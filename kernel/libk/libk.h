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
  
   UPMC / LIP6 / SOC (c) 2007
   Copyright Ghassan Almaless <ghassan.almaless@gmail.com>
   Copyright Franck Wajsbürt <franck.wajsburt@lip6.fr>
*/

#ifndef _LIBK_H_
#define _LIBK_H_

#include <stdarg.h>
#include <types.h>
#include <string.h>
#include <ctype.h>

int iprintk (char *addr, int inc, const char *fmt, va_list ap);
int sprintk (char *s, char *fmt, ...);
void srand(unsigned int seed);
uint_t rand(void);
int atoi(const char *nptr);

struct task_s;
error_t elf_load_task(char *pathname, struct task_s *task);

#endif
