#ifndef _DOLPHIN_TYPES_H_
#define _DOLPHIN_TYPES_H_
/* Port replacement for include/dolphin/types.h: exact widths, platform C library. */
#include <types.h>
typedef char *Ptr;
#define FALSE 0
#define TRUE 1
#define AT_ADDRESS(addr)
#define ATTRIBUTE_ALIGN(num) __attribute__((aligned(num)))
#include <limits.h>
#ifndef NULL
#define NULL 0
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
/* libm's fabsf is renamed out of the way (math_sub.h of the game defines its
 * own inline fabsf). libstdc++'s <cmath> would want the global name back, so
 * the C library's <math.h> is read directly; re4_whole_prelude.h (forced
 * included before every game unit) keeps the macro defined for the whole
 * unit. */
#define fabsf fabsf_libm
#include <math.h>
#undef fabsf
#endif
