#include "periodic.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>

typedef struct VKMUtilTimer {
  VKMUtilTimerFunc cb;
  VKMUtilTimerTerminationFunc onTerm;
  void *privdata;
  struct timespec interval;
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} VKMUtilTimer;

static struct timespec timespecAdd(struct timespec *a, struct timespec *b) {
  struct timespec ret;
  ret.tv_sec = a->tv_sec + b->tv_sec;

  long long ns = a->tv_nsec + b->tv_nsec;
  ret.tv_sec += ns / 1000000000;
  ret.tv_nsec = ns % 1000000000;
  return ret;
}

static void *vkmutilTimer_Loop(void *ctx) {
  VKMUtilTimer *tm = ctx;

  int rc = ETIMEDOUT;
  struct timespec ts;

  pthread_mutex_lock(&tm->lock);
  while (rc != 0) {
    clock_gettime(CLOCK_REALTIME, &ts);
    struct timespec timeout = timespecAdd(&ts, &tm->interval);
    if ((rc = pthread_cond_timedwait(&tm->cond, &tm->lock, &timeout)) == ETIMEDOUT) {

      // Create a thread safe context if we're running inside valkey
      ValkeyModuleCtx *rctx = NULL;
      if (ValkeyModule_GetThreadSafeContext) rctx = ValkeyModule_GetThreadSafeContext(NULL);

      // call our callback...
      if (!tm->cb(rctx, tm->privdata)) {
        break;
      }

      // If needed - free the thread safe context.
      // It's up to the user to decide whether automemory is active there
      if (rctx) ValkeyModule_FreeThreadSafeContext(rctx);
    }
    if (rc == EINVAL) {
      perror("Error waiting for condition");
      break;
    }
  }

  // call the termination callback if needed
  if (tm->onTerm != NULL) {
    tm->onTerm(tm->privdata);
  }

  // free resources associated with the timer
  pthread_cond_destroy(&tm->cond);
  free(tm);

  return NULL;
}

/* set a new frequency for the timer. This will take effect AFTER the next trigger */
void VKMUtilTimer_SetInterval(struct VKMUtilTimer *t, struct timespec newInterval) {
  t->interval = newInterval;
}

VKMUtilTimer *VKMUtil_NewPeriodicTimer(VKMUtilTimerFunc cb, VKMUtilTimerTerminationFunc onTerm, void *privdata, struct timespec interval) {
  VKMUtilTimer *ret = malloc(sizeof(*ret));
  *ret = (VKMUtilTimer){
      .privdata = privdata, .interval = interval, .cb = cb, .onTerm = onTerm,
  };
  pthread_cond_init(&ret->cond, NULL);
  pthread_mutex_init(&ret->lock, NULL);

  pthread_create(&ret->thread, NULL, vkmutilTimer_Loop, ret);
  return ret;
}

void VKMUtilTimer_Free(struct VKMUtilTimer *t) {
  pthread_mutex_destroy(&t->lock);
  pthread_cond_destroy(&t->cond);
  free(t);
}

int VKMUtilTimer_Terminate(struct VKMUtilTimer *t) {
  return pthread_cond_signal(&t->cond);
}
