#ifndef __VKMUTIL_ALLOC__
#define __VKMUTIL_ALLOC__

/* Automatic Valkey Module Allocation functions monkey-patching.
 *
 * Including this file while VALKEY_MODULE_TARGET is defined, will explicitly
 * override malloc, calloc, realloc & free with ValkeyModule_Alloc,
 * ValkeyModule_Callc, etc implementations, that allow Valkey better control and
 * reporting over allocations per module.
 *
 * You should include this file in all c files AS THE LAST INCLUDED FILE
 *
 * This only has effect when when compiling with the macro VALKEY_MODULE_TARGET
 * defined. The idea is that for unit tests it will not be defined, but for the
 * module build target it will be.
 *
 */

#include <stdlib.h>
#include <valkeymodule.h>

extern char *vkmalloc_strndup(const char *s, size_t n);

#ifdef VALKEY_MODULE_TARGET /* Set this when compiling your code as a module */

#define malloc(size) ValkeyModule_Alloc(size)
#define calloc(count, size) ValkeyModule_Calloc(count, size)
#define realloc(ptr, size) ValkeyModule_Realloc(ptr, size)
#define free(ptr) ValkeyModule_Free(ptr)

#ifdef strdup
#undef strdup
#endif
#define strdup(ptr) ValkeyModule_Strdup(ptr)

/* More overriding */
// needed to avoid calling strndup->malloc
#ifdef strndup
#undef strndup
#endif
#define strndup(s, n) vkmalloc_strndup(s, n)

#else
/* This function shold be called if you are working with malloc-patched code
 * ouside of valkey, usually for unit tests. Call it once when entering your unit
 * tests' main() */
extern void VKMUtil_InitAlloc();
#endif /* VALKEY_MODULE_TARGET */

#endif /* __VKMUTIL_ALLOC__ */
