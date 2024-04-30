#ifndef __TEST_UTIL_H__
#define __TEST_UTIL_H__

#include "util.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>


#define VKMUtil_Test(f) \
                if (argc < 2 || VKMUtil_ArgExists(__STRING(f), argv, argc, 1)) { \
                    int rc = f(ctx); \
                    if (rc != VALKEYMODULE_OK) { \
                        ValkeyModule_ReplyWithError(ctx, "Test " __STRING(f) " FAILED"); \
                        return VALKEYMODULE_ERR;\
                    }\
                }
           
                
#define VKMUtil_Assert(expr) if (!(expr)) { fprintf (stderr, "Assertion '%s' Failed\n", __STRING(expr)); return VALKEYMODULE_ERR; }

#define VKMUtil_AssertReplyEquals(rep, cstr) VKMUtil_Assert( \
            VKMUtil_StringEquals(ValkeyModule_CreateStringFromCallReply(rep), ValkeyModule_CreateString(ctx, cstr, strlen(cstr))) \
            )
#            

/**
* Create an arg list to pass to a valkey command handler manually, based on the format in fmt.
* The accepted format specifiers are:
*   c - for null terminated c strings
*   s - for ValkeyModuleString* objects
*   l - for longs
*
*  Example:  VKMUtil_MakeArgs(ctx, &argc, "clc", "hello", 1337, "world");
*
*  Returns an array of ValkeyModuleString pointers. The size of the array is store in argcp
*/
ValkeyModuleString **VKMUtil_MakeArgs(ValkeyModuleCtx *ctx, int *argcp, const char *fmt, ...) {
    
    va_list ap;
    va_start(ap, fmt);
    ValkeyModuleString **argv = calloc(strlen(fmt), sizeof(ValkeyModuleString*));
    int argc = 0;
    const char *p = fmt;
    while(*p) {
        if (*p == 'c') {
            char *cstr = va_arg(ap,char*);
            argv[argc++] = ValkeyModule_CreateString(ctx, cstr, strlen(cstr));
        } else if (*p == 's') {
            argv[argc++] = va_arg(ap,void*);;
        } else if (*p == 'l') {
            long ll = va_arg(ap,long long);
            argv[argc++] = ValkeyModule_CreateStringFromLongLong(ctx, ll);
        } else {
            goto fmterr;
        }
        p++;
    }
    *argcp = argc;
    
    return argv;
fmterr:
    free(argv);
    return NULL;
}

#endif
