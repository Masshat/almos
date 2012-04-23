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

#ifndef _STRING_H_
#define _STRING_H_

#include <types.h>

void *memcpy (void *dest, void *src, unsigned long size);
void *memset (void *s, int c, unsigned int size);

int strlen(const char *s);
int strnlen (const char *str, int count);
int strcmp (const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char* strcpy(char *dest, char *src);
char* strncpy(char *dest, char *src, size_t n);
char *strchr(const char *s, int c);

#endif
