#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void  *malloc(size_t size);
void  *calloc(size_t count, size_t size);
void  *realloc(void *ptr, size_t size);
void   free(void *ptr);
void   exit(int code) __attribute__((noreturn));
void   abort(void) __attribute__((noreturn));

#endif
