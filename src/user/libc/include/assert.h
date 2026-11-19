#ifndef _LIBC_ASSERT_H
#define _LIBC_ASSERT_H

#include <stdlib.h>

#define assert(expr) ((expr) ? (void) 0 : abort())

#endif
