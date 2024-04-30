#ifndef VKMUTIL_PERIODIC_H_
#define VKMUTIL_PERIODIC_H_
#include <time.h>
#include <valkeymodule.h>

/** periodic.h - Utility periodic timer running a task repeatedly every given time interval */

/* VKMUtilTimer - opaque context for the timer */
struct VKMUtilTimer;

/* VKMUtilTimerFunc - callback type for timer tasks. The ctx is a thread-safe valkey module context
 * that should be locked/unlocked by the callback when running stuff against valkey. privdata is
 * pre-existing private data */
typedef int (*VKMUtilTimerFunc)(ValkeyModuleCtx *ctx, void *privdata);

typedef void (*VKMUtilTimerTerminationFunc)(void *privdata);

/* Create and start a new periodic timer. Each timer has its own thread and can only be run and
 * stopped once. The timer runs `cb` every `interval` with `privdata` passed to the callback. */
struct VKMUtilTimer *VKMUtil_NewPeriodicTimer(VKMUtilTimerFunc cb, VKMUtilTimerTerminationFunc onTerm, void *privdata, struct timespec interval);

void VKMUtilTimer_Free(struct VKMUtilTimer *);

/* set a new frequency for the timer. This will take effect AFTER the next trigger */
void VKMUtilTimer_SetInterval(struct VKMUtilTimer *t, struct timespec newInterval);

/* Stop the timer loop, call the termination callbck to free up any resources linked to the timer,
 * and free the timer after stopping.
 *
 * This function doesn't wait for the thread to terminate, as it may cause a race condition if the
 * timer's callback is waiting for the valkey global lock.
 * Instead you should make sure any resources are freed by the callback after the thread loop is
 * finished.
 *
 * The timer is freed automatically, so the callback doesn't need to do anything about it.
 * The callback gets the timer's associated privdata as its argument.
 *
 * If no callback is specified we do not free up privdata. If privdata is NULL we still call the
 * callback, as it may log stuff or free global resources.
 */
int VKMUtilTimer_Terminate(struct VKMUtilTimer *t);

#endif
