#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <ctype.h>
#include <sys/time.h>
#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include <strings.h>
#include <valkeymodule.h>
#include "util.h"

/**
Check if an argument exists in an argument list (argv,argc), starting at offset.
@return 0 if it doesn't exist, otherwise the offset it exists in
*/
int VKMUtil_ArgExists(const char *arg, ValkeyModuleString **argv, int argc, int offset) {

  size_t larg = strlen(arg);
  for (; offset < argc; offset++) {
    size_t l;
    const char *carg = ValkeyModule_StringPtrLen(argv[offset], &l);
    if (l != larg) continue;
    if (carg != NULL && strncasecmp(carg, arg, larg) == 0) {
      return offset;
    }
  }
  return 0;
}

/**
Check if an argument exists in an argument list (argv,argc)
@return -1 if it doesn't exist, otherwise the offset it exists in
*/
int VKMUtil_ArgIndex(const char *arg, ValkeyModuleString **argv, int argc) {

  size_t larg = strlen(arg);
  for (int offset = 0; offset < argc; offset++) {
    size_t l;
    const char *carg = ValkeyModule_StringPtrLen(argv[offset], &l);
    if (l != larg) continue;
    if (carg != NULL && strncasecmp(carg, arg, larg) == 0) {
      return offset;
    }
  }
  return -1;
}

VKMUtilInfo *VKMUtil_GetValkeyInfo(ValkeyModuleCtx *ctx) {

  ValkeyModuleCallReply *r = ValkeyModule_Call(ctx, "INFO", "c", "all");
  if (r == NULL || ValkeyModule_CallReplyType(r) == VALKEYMODULE_REPLY_ERROR) {
    return NULL;
  }

  int cap = 100;  // rough estimate of info lines
  VKMUtilInfo *info = malloc(sizeof(VKMUtilInfo));
  info->entries = calloc(cap, sizeof(VKMUtilInfoEntry));

  int i = 0;
  size_t sz;
  char *text = (char *)ValkeyModule_CallReplyStringPtr(r, &sz);

  char *line = text;
  while (line && line < text + sz) {
    char *line = strsep(&text, "\r\n");
    if (line == NULL) break;

    if (!(*line >= 'a' && *line <= 'z')) {  // skip non entry lines
      continue;
    }

    char *key = strsep(&line, ":");
    info->entries[i].key = key;
    info->entries[i].val = line;
    i++;
    if (i >= cap) {
      cap *= 2;
      info->entries = realloc(info->entries, cap * sizeof(VKMUtilInfoEntry));
    }
  }
  info->numEntries = i;
  ValkeyModule_FreeCallReply(r);
  return info;
}

void VKMUtilValkeyInfo_Free(VKMUtilInfo *info) {
  for (int i = 0; i < info->numEntries; i++) {
    free(info->entries[i].key);
    free(info->entries[i].val);
  }
  free(info->entries);
  free(info);
}

int VKMUtilInfo_GetInt(VKMUtilInfo *info, const char *key, long long *val) {

  const char *p = NULL;
  if (!VKMUtilInfo_GetString(info, key, &p)) {
    return 0;
  }

  *val = strtoll(p, NULL, 10);
  if ((errno == ERANGE && (*val == LONG_MAX || *val == LONG_MIN)) || (errno != 0 && *val == 0)) {
    *val = -1;
    return 0;
  }

  return 1;
}

int VKMUtilInfo_GetString(VKMUtilInfo *info, const char *key, const char **str) {
  int i;
  for (i = 0; i < info->numEntries; i++) {
    if (!strcmp(key, info->entries[i].key)) {
      *str = info->entries[i].val;
      return 1;
    }
  }
  return 0;
}

int VKMUtilInfo_GetDouble(VKMUtilInfo *info, const char *key, double *d) {
  const char *p = NULL;
  if (!VKMUtilInfo_GetString(info, key, &p)) {
    printf("not found %s\n", key);
    return 0;
  }

  *d = strtod(p, NULL);
  if ((errno == ERANGE && (*d == HUGE_VAL || *d == -HUGE_VAL)) || (errno != 0 && *d == 0)) {
    return 0;
  }

  return 1;
}

/*
c -- pointer to a Null terminated C string pointer.
b -- pointer to a C buffer, followed by pointer to a size_t for its length
s -- pointer to a ValkeyModuleString
l -- pointer to Long long integer.
d -- pointer to a Double
* -- do not parse this argument at all
*/
int VKMUtil_ParseArgs(ValkeyModuleString **argv, int argc, int offset, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int rc = vkmutil_vparseArgs(argv, argc, offset, fmt, ap);
  va_end(ap);
  return rc;
}

// Internal function that parses arguments based on the format described above
int vkmutil_vparseArgs(ValkeyModuleString **argv, int argc, int offset, const char *fmt, va_list ap) {

  int i = offset;
  char *c = (char *)fmt;
  while (*c && i < argc) {

    // read c string
    if (*c == 'c') {
      char **p = va_arg(ap, char **);
      *p = (char *)ValkeyModule_StringPtrLen(argv[i], NULL);
    } else if (*c == 'b') {
      char **p = va_arg(ap, char **);
      size_t *len = va_arg(ap, size_t *);
      *p = (char *)ValkeyModule_StringPtrLen(argv[i], len);
    } else if (*c == 's') {  // read valkey string

      ValkeyModuleString **s = va_arg(ap, void *);
      *s = argv[i];

    } else if (*c == 'l') {  // read long
      long long *l = va_arg(ap, long long *);

      if (ValkeyModule_StringToLongLong(argv[i], l) != VALKEYMODULE_OK) {
        return VALKEYMODULE_ERR;
      }
    } else if (*c == 'd') {  // read double
      double *d = va_arg(ap, double *);
      if (ValkeyModule_StringToDouble(argv[i], d) != VALKEYMODULE_OK) {
        return VALKEYMODULE_ERR;
      }
    } else if (*c == '*') {  // skip current arg
      // do nothing
    } else {
      return VALKEYMODULE_ERR;  // WAT?
    }
    c++;
    i++;
  }
  // if the format is longer than argc, retun an error
  if (*c != 0) {
    return VALKEYMODULE_ERR;
  }
  return VALKEYMODULE_OK;
}

int VKMUtil_ParseArgsAfter(const char *token, ValkeyModuleString **argv, int argc, const char *fmt, ...) {

  int pos = VKMUtil_ArgIndex(token, argv, argc);
  if (pos < 0) {
    return VALKEYMODULE_ERR;
  }

  va_list ap;
  va_start(ap, fmt);
  int rc = vkmutil_vparseArgs(argv, argc, pos + 1, fmt, ap);
  va_end(ap);
  return rc;
}

ValkeyModuleCallReply *ValkeyModule_CallReplyArrayElementByPath(ValkeyModuleCallReply *rep, const char *path) {
  if (rep == NULL) return NULL;

  ValkeyModuleCallReply *ele = rep;
  const char *s = path;
  char *e;
  long idx;
  do {
    errno = 0;
    idx = strtol(s, &e, 10);

    if ((errno == ERANGE && (idx == LONG_MAX || idx == LONG_MIN)) || (errno != 0 && idx == 0) ||
        (VALKEYMODULE_REPLY_ARRAY != ValkeyModule_CallReplyType(ele)) || (s == e)) {
      ele = NULL;
      break;
    }
    s = e;
    ele = ValkeyModule_CallReplyArrayElement(ele, idx - 1);

  } while ((ele != NULL) && (*e != '\0'));

  return ele;
}

int ValkeyModule_TryGetValue(ValkeyModuleKey *key, const ValkeyModuleType *type, void **out) {
  if (key == NULL) {
    return VKMUTIL_VALUE_MISSING;
  }
  int keytype = ValkeyModule_KeyType(key);
  if (keytype == VALKEYMODULE_KEYTYPE_EMPTY) {
    return VKMUTIL_VALUE_EMPTY;
  } else if (keytype == VALKEYMODULE_KEYTYPE_MODULE && ValkeyModule_ModuleTypeGetType(key) == type) {
    *out = ValkeyModule_ModuleTypeGetValue(key);
    return VKMUTIL_VALUE_OK;
  } else {
    return VKMUTIL_VALUE_MISMATCH;
  }
}

ValkeyModuleString **VKMUtil_ParseVarArgs(ValkeyModuleString **argv, int argc, int offset, const char *keyword, size_t *nargs) {
  if (offset > argc) {
    return NULL;
  }

  argv += offset;
  argc -= offset;

  int ix = VKMUtil_ArgIndex(keyword, argv, argc);
  if (ix < 0) {
    return NULL;
  } else if (ix >= argc - 1) {
    *nargs = VKMUTIL_VARARGS_BADARG;
    return argv;
  }

  argv += (ix + 1);
  argc -= (ix + 1);

  long long n = 0;
  VKMUtil_ParseArgs(argv, argc, 0, "l", &n);
  if (n > argc - 1 || n < 0) {
    *nargs = VKMUTIL_VARARGS_BADARG;
    return argv;
  }

  *nargs = n;
  return argv + 1;
}

void VKMUtil_DefaultAofRewrite(ValkeyModuleIO *aof, ValkeyModuleString *key, void *value) {
  ValkeyModuleCtx *ctx = ValkeyModule_GetThreadSafeContext(NULL);
  ValkeyModuleCallReply *rep = ValkeyModule_Call(ctx, "DUMP", "s", key);
  if (rep != NULL && ValkeyModule_CallReplyType(rep) == VALKEYMODULE_REPLY_STRING) {
    size_t n;
    const char *s = ValkeyModule_CallReplyStringPtr(rep, &n);
    ValkeyModule_EmitAOF(aof, "RESTORE", "slb", key, 0, s, n);
  } else {
    ValkeyModule_Log(ValkeyModule_GetContextFromIO(aof), "warning", "Failed to emit AOF");
  }
  if (rep != NULL) {
    ValkeyModule_FreeCallReply(rep);
  }
  ValkeyModule_FreeThreadSafeContext(ctx);
}
