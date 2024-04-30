#ifndef __UTIL_H__
#define __UTIL_H__

#include <valkeymodule.h>
#include <stdarg.h>

/// make sure the response is not NULL or an error, and if it is sends the error to the client and
/// exit the current function
#define VKMUTIL_ASSERT_NOERROR(ctx, r) \
  if (r == NULL) { \
    return ValkeyModule_ReplyWithError(ctx, "ERR reply is NULL"); \
  } else if (ValkeyModule_CallReplyType(r) == VALKEYMODULE_REPLY_ERROR) { \
    ValkeyModule_ReplyWithCallReply(ctx, r); \
    return VALKEYMODULE_ERR; \
  }

#define __vkmutil_register_cmd(ctx, cmd, f, mode) \
  if (ValkeyModule_CreateCommand(ctx, cmd, f, mode, 1, 1, 1) == VALKEYMODULE_ERR) \
    return VALKEYMODULE_ERR;

#define VKMUtil_RegisterReadCmd(ctx, cmd, f) \
  __vkmutil_register_cmd(ctx, cmd, f, "readonly") \
  }

#define VKMUtil_RegisterWriteCmd(ctx, cmd, f) __vkmutil_register_cmd(ctx, cmd, f, "write")

/* ValkeyModule utilities. */

/** DEPRECATED: Return the offset of an arg if it exists in the arg list, or 0 if it's not there */
extern int VKMUtil_ArgExists(const char *arg, ValkeyModuleString **argv, int argc, int offset);

/* Same as argExists but returns -1 if not found. Use this, VKMUtil_ArgExists is kept for backwards
compatibility. */
extern int VKMUtil_ArgIndex(const char *arg, ValkeyModuleString **argv, int argc);

/**
Automatically conver the arg list to corresponding variable pointers according to a given format.
You pass it the command arg list and count, the starting offset, a parsing format, and pointers to
the variables.
The format is a string consisting of the following identifiers:

    c -- pointer to a Null terminated C string pointer.
    s -- pointer to a ValkeyModuleString
    l -- pointer to Long long integer.
    d -- pointer to a Double
    * -- do not parse this argument at all

Example: If I want to parse args[1], args[2] as a long long and double, I do:
    double d;
    long long l;
    VKMUtil_ParseArgs(argv, argc, 1, "ld", &l, &d);
*/
extern int VKMUtil_ParseArgs(ValkeyModuleString **argv, int argc, int offset, const char *fmt, ...);

/**
Same as VKMUtil_ParseArgs, but only parses the arguments after `token`, if it was found.
This is useful for optional stuff like [LIMIT [offset] [limit]]
*/
extern int VKMUtil_ParseArgsAfter(const char *token, ValkeyModuleString **argv, int argc, const char *fmt,
                          ...);

extern int vkmutil_vparseArgs(ValkeyModuleString **argv, int argc, int offset, const char *fmt, va_list ap);

#define VKMUTIL_VARARGS_BADARG ((size_t)-1)
/**
 * Parse arguments in the form of KEYWORD {len} {arg} .. {arg}_len.
 * If keyword is present, returns the position within `argv` containing the arguments.
 * Returns NULL if the keyword is not found.
 * If a parse error has occurred, `nargs` is set to VKMUTIL_VARARGS_BADARG, but
 * the return value is not NULL.
 */
ValkeyModuleString **VKMUtil_ParseVarArgs(ValkeyModuleString **argv, int argc, int offset, const char *keyword, size_t *nargs);

/**
 * Default implementation of an AoF rewrite function that simply calls DUMP/RESTORE
 * internally. To use this function, pass it as the .aof_rewrite value in
 * ValkeyModuleTypeMethods
 */
void VKMUtil_DefaultAofRewrite(ValkeyModuleIO *aof, ValkeyModuleString *key, void *value);

// A single key/value entry in a valkey info map
typedef struct {
  char *key;
  char *val;
} VKMUtilInfoEntry;

// Representation of INFO command response, as a list of k/v pairs
typedef struct {
  VKMUtilInfoEntry *entries;
  int numEntries;
} VKMUtilInfo;

/**
* Get valkey INFO result and parse it as VKMUtilInfo.
* Returns NULL if something goes wrong.
* The resulting object needs to be freed with VKMUtilValkeyInfo_Free
*/
extern VKMUtilInfo *VKMUtil_GetValkeyInfo(ValkeyModuleCtx *ctx);

/**
* Free an VKMUtilInfo object and its entries
*/
extern void VKMUtilValkeyInfo_Free(VKMUtilInfo *info);

/**
* Get an integer value from an info object. Returns 1 if the value was found and
* is an integer, 0 otherwise. the value is placed in 'val'
*/
extern int VKMUtilInfo_GetInt(VKMUtilInfo *info, const char *key, long long *val);

/**
* Get a string value from an info object. The value is placed in str.
* Returns 1 if the key was found, 0 if not
*/
extern int VKMUtilInfo_GetString(VKMUtilInfo *info, const char *key, const char **str);

/**
* Get a double value from an info object. Returns 1 if the value was found and is
* a correctly formatted double, 0 otherwise. the value is placed in 'd'
*/
extern int VKMUtilInfo_GetDouble(VKMUtilInfo *info, const char *key, double *d);

/*
* Returns a call reply array's element given by a space-delimited path. E.g.,
* the path "1 2 3" will return the 3rd element from the 2 element of the 1st
* element from an array (or NULL if not found)
*/
extern ValkeyModuleCallReply *ValkeyModule_CallReplyArrayElementByPath(ValkeyModuleCallReply *rep, const char *path);

/*
* Returns a call reply array's element given by a space-delimited path. E.g.,
* the path "1 2 3" will return the 3rd element from the 2 element of the 1st
* element from an array (or NULL if not found)
*/
ValkeyModuleCallReply *ValkeyModule_CallReplyArrayElementByPath(ValkeyModuleCallReply *rep, const char *path);

/**
 * Extract the module type from an opened key.
 */
typedef enum {
  VKMUTIL_VALUE_OK = 0,
  VKMUTIL_VALUE_MISSING,
  VKMUTIL_VALUE_EMPTY,
  VKMUTIL_VALUE_MISMATCH
} VKMUtil_TryGetValueStatus;

/**
 * Tries to extract the module-specific type from the value.
 * @param key an opened key (may be null)
 * @param type the pointer to the type to match to
 * @param[out] out if the value is present, will be set to it.
 * @return a value in the @ref VKMUtil_TryGetValueStatus enum.
 */
int ValkeyModule_TryGetValue(ValkeyModuleKey *key, const ValkeyModuleType *type, void **out);

#endif
