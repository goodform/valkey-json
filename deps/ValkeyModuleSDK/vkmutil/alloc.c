#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "alloc.h"

/* A patched implementation of strdup that will use our patched calloc */
char *vkmalloc_strndup(const char *s, size_t n) {
  char *ret = calloc(n + 1, sizeof(char));
  if (ret)
    memcpy(ret, s, n);
  return ret;
}

/*
 * Re-patching ValkeyModule_Alloc and friends to the original malloc functions
 *
 * This function shold be called if you are working with malloc-patched code
 * ouside of valkey, usually for unit tests. Call it once when entering your unit
 * tests' main().
 *
 * Since including "alloc.h" while defining VALKEY_MODULE_TARGET
 * replaces all malloc functions in valkey with the VKM_Alloc family of functions,
 * when running that code outside of valkey, your app will crash. This function
 * patches the VKM_Alloc functions back to the original mallocs. */
void VKMUtil_InitAlloc() {

  ValkeyModule_Alloc = malloc;
  ValkeyModule_Realloc = realloc;
  ValkeyModule_Calloc = calloc;
  ValkeyModule_Free = free;
  ValkeyModule_Strdup = strdup;
}
