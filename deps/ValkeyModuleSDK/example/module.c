#include "../valkeymodule.h"
#include "../vkmutil/util.h"
#include "../vkmutil/strings.h"
#include "../vkmutil/test_util.h"

/* EXAMPLE.PARSE [SUM <x> <y>] | [PROD <x> <y>]
*  Demonstrates the automatic arg parsing utility.
*  If the command receives "SUM <x> <y>" it returns their sum
*  If it receives "PROD <x> <y>" it returns their product
*/
int ParseCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {

  // we must have at least 4 args
  if (argc < 4) {
    return ValkeyModule_WrongArity(ctx);
  }

  // init auto memory for created strings
  ValkeyModule_AutoMemory(ctx);
  long long x, y;

  // If we got SUM - return the sum of 2 consecutive arguments
  if (VKMUtil_ParseArgsAfter("SUM", argv, argc, "ll", &x, &y) ==
      VALKEYMODULE_OK) {
    ValkeyModule_ReplyWithLongLong(ctx, x + y);
    return VALKEYMODULE_OK;
  }

  // If we got PROD - return the product of 2 consecutive arguments
  if (VKMUtil_ParseArgsAfter("PROD", argv, argc, "ll", &x, &y) ==
      VALKEYMODULE_OK) {
    ValkeyModule_ReplyWithLongLong(ctx, x * y);
    return VALKEYMODULE_OK;
  }

  // something is fishy...
  ValkeyModule_ReplyWithError(ctx, "Invalid arguments");

  return VALKEYMODULE_ERR;
}

/*
* example.HGETSET <key> <element> <value>
* Atomically set a value in a HASH key to <value> and return its value before
* the HSET.
*
* Basically atomic HGET + HSET
*/
int HGetSetCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {

  // we need EXACTLY 4 arguments
  if (argc != 4) {
    return ValkeyModule_WrongArity(ctx);
  }
  ValkeyModule_AutoMemory(ctx);

  // open the key and make sure it's indeed a HASH and not empty
  ValkeyModuleKey *key =
      ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ | VALKEYMODULE_WRITE);
  if (ValkeyModule_KeyType(key) != VALKEYMODULE_KEYTYPE_HASH &&
      ValkeyModule_KeyType(key) != VALKEYMODULE_KEYTYPE_EMPTY) {
    return ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
  }

  // get the current value of the hash element
  ValkeyModuleCallReply *rep =
      ValkeyModule_Call(ctx, "HGET", "ss", argv[1], argv[2]);
  VKMUTIL_ASSERT_NOERROR(ctx, rep);

  // set the new value of the element
  ValkeyModuleCallReply *srep =
      ValkeyModule_Call(ctx, "HSET", "sss", argv[1], argv[2], argv[3]);
  VKMUTIL_ASSERT_NOERROR(ctx, srep);

  // if the value was null before - we just return null
  if (ValkeyModule_CallReplyType(rep) == VALKEYMODULE_REPLY_NULL) {
    ValkeyModule_ReplyWithNull(ctx);
    return VALKEYMODULE_OK;
  }

  // forward the HGET reply to the client
  ValkeyModule_ReplyWithCallReply(ctx, rep);
  return VALKEYMODULE_OK;
}

// Test the the PARSE command
int testParse(ValkeyModuleCtx *ctx) {

  ValkeyModuleCallReply *r =
      ValkeyModule_Call(ctx, "example.parse", "ccc", "SUM", "5", "2");
  VKMUtil_Assert(ValkeyModule_CallReplyType(r) == VALKEYMODULE_REPLY_INTEGER);
  VKMUtil_AssertReplyEquals(r, "7");

  r = ValkeyModule_Call(ctx, "example.parse", "ccc", "PROD", "5", "2");
  VKMUtil_Assert(ValkeyModule_CallReplyType(r) == VALKEYMODULE_REPLY_INTEGER);
  VKMUtil_AssertReplyEquals(r, "10");
  return 0;
}

// test the HGETSET command
int testHgetSet(ValkeyModuleCtx *ctx) {
  ValkeyModuleCallReply *r =
      ValkeyModule_Call(ctx, "example.hgetset", "ccc", "foo", "bar", "baz");
  VKMUtil_Assert(ValkeyModule_CallReplyType(r) != VALKEYMODULE_REPLY_ERROR);

  r = ValkeyModule_Call(ctx, "example.hgetset", "ccc", "foo", "bar", "bag");
  VKMUtil_Assert(ValkeyModule_CallReplyType(r) == VALKEYMODULE_REPLY_STRING);
  VKMUtil_AssertReplyEquals(r, "baz");
  r = ValkeyModule_Call(ctx, "example.hgetset", "ccc", "foo", "bar", "bang");
  VKMUtil_AssertReplyEquals(r, "bag");
  return 0;
}

// Unit test entry point for the module
int TestModule(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
  ValkeyModule_AutoMemory(ctx);

  VKMUtil_Test(testParse);
  VKMUtil_Test(testHgetSet);

  ValkeyModule_ReplyWithSimpleString(ctx, "PASS");
  return VALKEYMODULE_OK;
}

int ValkeyModule_OnLoad(ValkeyModuleCtx *ctx) {

  // Register the module itself
  if (ValkeyModule_Init(ctx, "example", 1, VALKEYMODULE_APIVER_1) ==
      VALKEYMODULE_ERR) {
    return VALKEYMODULE_ERR;
  }

  // register example.parse - the default registration syntax
  if (ValkeyModule_CreateCommand(ctx, "example.parse", ParseCommand, "readonly",
                                1, 1, 1) == VALKEYMODULE_ERR) {
    return VALKEYMODULE_ERR;
  }

  // register example.hgetset - using the shortened utility registration macro
  VKMUtil_RegisterWriteCmd(ctx, "example.hgetset", HGetSetCommand);

  // register the unit test
  VKMUtil_RegisterWriteCmd(ctx, "example.test", TestModule);

  return VALKEYMODULE_OK;
}
