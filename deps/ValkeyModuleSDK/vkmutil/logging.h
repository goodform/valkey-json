#ifndef __VKMUTIL_LOGGING_H__
#define __VKMUTIL_LOGGING_H__

/* Convenience macros for valkey logging */

#define VKM_LOG_DEBUG(ctx, ...) ValkeyModule_Log(ctx, "debug", __VA_ARGS__)
#define VKM_LOG_VERBOSE(ctx, ...) ValkeyModule_Log(ctx, "verbose", __VA_ARGS__)
#define VKM_LOG_NOTICE(ctx, ...) ValkeyModule_Log(ctx, "notice", __VA_ARGS__)
#define VKM_LOG_WARNING(ctx, ...) ValkeyModule_Log(ctx, "warning", __VA_ARGS__)

#endif
