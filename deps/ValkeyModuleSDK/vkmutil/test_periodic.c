#include <stdio.h>
#include <valkeymodule.h>
#include <unistd.h>
#include "periodic.h"
#include "assert.h"
#include "test.h"

int timerCb(ValkeyModuleCtx *ctx, void *p) {
  int *x = p;
  (*x)++;
  return 0;
}

int testPeriodic() {
  int x = 0;
  struct VKMUtilTimer *tm =
      VKMUtil_NewPeriodicTimer(timerCb, NULL, &x, (struct timespec){.tv_sec = 0, .tv_nsec = 10000000});

  sleep(1);

  ASSERT_EQUAL(0, VKMUtilTimer_Terminate(tm));
  ASSERT(x > 0);
  ASSERT(x <= 100);
  VKMUtilTimer_Free(tm);
  return 0;
}

TEST_MAIN({ TESTFUNC(testPeriodic); });
