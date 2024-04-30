# Modules API reference

## `VKM_Alloc`

    void *VKM_Alloc(size_t bytes);

Use like malloc(). Memory allocated with this function is reported in
Valkey INFO memory, used for keys eviction according to maxmemory settings
and in general is taken into account as memory allocated by Valkey.
You should avoid using malloc().

## `VKM_Calloc`

    void *VKM_Calloc(size_t nmemb, size_t size);

Use like calloc(). Memory allocated with this function is reported in
Valkey INFO memory, used for keys eviction according to maxmemory settings
and in general is taken into account as memory allocated by Valkey.
You should avoid using calloc() directly.

## `VKM_Realloc`

    void* VKM_Realloc(void *ptr, size_t bytes);

Use like realloc() for memory obtained with `ValkeyModule_Alloc()`.

## `VKM_Free`

    void VKM_Free(void *ptr);

Use like free() for memory obtained by `ValkeyModule_Alloc()` and
`ValkeyModule_Realloc()`. However you should never try to free with
`ValkeyModule_Free()` memory allocated with malloc() inside your module.

## `VKM_Strdup`

    char *VKM_Strdup(const char *str);

Like strdup() but returns memory allocated with `ValkeyModule_Alloc()`.

## `VKM_PoolAlloc`

    void *VKM_PoolAlloc(ValkeyModuleCtx *ctx, size_t bytes);

Return heap allocated memory that will be freed automatically when the
module callback function returns. Mostly suitable for small allocations
that are short living and must be released when the callback returns
anyway. The returned memory is aligned to the architecture word size
if at least word size bytes are requested, otherwise it is just
aligned to the next power of two, so for example a 3 bytes request is
4 bytes aligned while a 2 bytes request is 2 bytes aligned.

There is no realloc style function since when this is needed to use the
pool allocator is not a good idea.

The function returns NULL if `bytes` is 0.

## `VKM_GetApi`

    int VKM_GetApi(const char *funcname, void **targetPtrPtr);

Lookup the requested module API and store the function pointer into the
target pointer. The function returns `VALKEYMODULE_ERR` if there is no such
named API, otherwise `VALKEYMODULE_OK`.

This function is not meant to be used by modules developer, it is only
used implicitly by including valkeymodule.h.

## `VKM_IsKeysPositionRequest`

    int VKM_IsKeysPositionRequest(ValkeyModuleCtx *ctx);

Return non-zero if a module command, that was declared with the
flag "getkeys-api", is called in a special way to get the keys positions
and not to get executed. Otherwise zero is returned.

## `VKM_KeyAtPos`

    void VKM_KeyAtPos(ValkeyModuleCtx *ctx, int pos);

When a module command is called in order to obtain the position of
keys, since it was flagged as "getkeys-api" during the registration,
the command implementation checks for this special call using the
`ValkeyModule_IsKeysPositionRequest()` API and uses this function in
order to report keys, like in the following example:

 if (`ValkeyModule_IsKeysPositionRequest(ctx))` {
     `ValkeyModule_KeyAtPos(ctx`,1);
     `ValkeyModule_KeyAtPos(ctx`,2);
 }

 Note: in the example below the get keys API would not be needed since
 keys are at fixed positions. This interface is only used for commands
 with a more complex structure.

## `VKM_CreateCommand`

    int VKM_CreateCommand(ValkeyModuleCtx *ctx, const char *name, ValkeyModuleCmdFunc cmdfunc, const char *strflags, int firstkey, int lastkey, int keystep);

Register a new command in the Valkey server, that will be handled by
calling the function pointer 'func' using the ValkeyModule calling
convention. The function returns `VALKEYMODULE_ERR` if the specified command
name is already busy or a set of invalid flags were passed, otherwise
`VALKEYMODULE_OK` is returned and the new command is registered.

This function must be called during the initialization of the module
inside the `ValkeyModule_OnLoad()` function. Calling this function outside
of the initialization function is not defined.

The command function type is the following:

     int MyCommand_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc);

And is supposed to always return `VALKEYMODULE_OK`.

The set of flags 'strflags' specify the behavior of the command, and should
be passed as a C string compoesd of space separated words, like for
example "write deny-oom". The set of flags are:

* **"write"**:     The command may modify the data set (it may also read
                   from it).
* **"readonly"**:  The command returns data from keys but never writes.
* **"admin"**:     The command is an administrative command (may change
                   replication or perform similar tasks).
* **"deny-oom"**:  The command may use additional memory and should be
                   denied during out of memory conditions.
* **"deny-script"**:   Don't allow this command in Lua scripts.
* **"allow-loading"**: Allow this command while the server is loading data.
                       Only commands not interacting with the data set
                       should be allowed to run in this mode. If not sure
                       don't use this flag.
* **"pubsub"**:    The command publishes things on Pub/Sub channels.
* **"random"**:    The command may have different outputs even starting
                   from the same input arguments and key values.
* **"allow-stale"**: The command is allowed to run on slaves that don't
                     serve stale data. Don't use if you don't know what
                     this means.
* **"no-monitor"**: Don't propoagate the command on monitor. Use this if
                    the command has sensible data among the arguments.
* **"fast"**:      The command time complexity is not greater
                   than O(log(N)) where N is the size of the collection or
                   anything else representing the normal scalability
                   issue with the command.
* **"getkeys-api"**: The command implements the interface to return
                     the arguments that are keys. Used when start/stop/step
                     is not enough because of the command syntax.
* **"no-cluster"**: The command should not register in Valkey Cluster
                    since is not designed to work with it because, for
                    example, is unable to report the position of the
                    keys, programmatically creates key names, or any
                    other reason.

## `VKM_SetModuleAttribs`

    void VKM_SetModuleAttribs(ValkeyModuleCtx *ctx, const char *name, int ver, int apiver);

Called by `VKM_Init()` to setup the `ctx->module` structure.

This is an internal function, Valkey modules developers don't need
to use it.

## `VKM_Milliseconds`

    long long VKM_Milliseconds(void);

Return the current UNIX time in milliseconds.

## `VKM_AutoMemory`

    void VKM_AutoMemory(ValkeyModuleCtx *ctx);

Enable automatic memory management. See API.md for more information.

The function must be called as the first function of a command implementation
that wants to use automatic memory.

## `VKM_CreateString`

    ValkeyModuleString *VKM_CreateString(ValkeyModuleCtx *ctx, const char *ptr, size_t len);

Create a new module string object. The returned string must be freed
with `ValkeyModule_FreeString()`, unless automatic memory is enabled.

The string is created by copying the `len` bytes starting
at `ptr`. No reference is retained to the passed buffer.

## `VKM_CreateStringPrintf`

    ValkeyModuleString *VKM_CreateStringPrintf(ValkeyModuleCtx *ctx, const char *fmt, ...);

Create a new module string object from a printf format and arguments.
The returned string must be freed with `ValkeyModule_FreeString()`, unless
automatic memory is enabled.

The string is created using the sds formatter function sdscatvprintf().

## `VKM_CreateStringFromLongLong`

    ValkeyModuleString *VKM_CreateStringFromLongLong(ValkeyModuleCtx *ctx, long long ll);

Like `ValkeyModule_CreatString()`, but creates a string starting from a long long
integer instead of taking a buffer and its length.

The returned string must be released with `ValkeyModule_FreeString()` or by
enabling automatic memory management.

## `VKM_CreateStringFromString`

    ValkeyModuleString *VKM_CreateStringFromString(ValkeyModuleCtx *ctx, const ValkeyModuleString *str);

Like `ValkeyModule_CreatString()`, but creates a string starting from another
ValkeyModuleString.

The returned string must be released with `ValkeyModule_FreeString()` or by
enabling automatic memory management.

## `VKM_FreeString`

    void VKM_FreeString(ValkeyModuleCtx *ctx, ValkeyModuleString *str);

Free a module string object obtained with one of the Valkey modules API calls
that return new string objects.

It is possible to call this function even when automatic memory management
is enabled. In that case the string will be released ASAP and removed
from the pool of string to release at the end.

## `VKM_RetainString`

    void VKM_RetainString(ValkeyModuleCtx *ctx, ValkeyModuleString *str);

Every call to this function, will make the string 'str' requiring
an additional call to `ValkeyModule_FreeString()` in order to really
free the string. Note that the automatic freeing of the string obtained
enabling modules automatic memory management counts for one
`ValkeyModule_FreeString()` call (it is just executed automatically).

Normally you want to call this function when, at the same time
the following conditions are true:

1) You have automatic memory management enabled.
2) You want to create string objects.
3) Those string objects you create need to live *after* the callback
   function(for example a command implementation) creating them returns.

Usually you want this in order to store the created string object
into your own data structure, for example when implementing a new data
type.

Note that when memory management is turned off, you don't need
any call to RetainString() since creating a string will always result
into a string that lives after the callback function returns, if
no FreeString() call is performed.

## `VKM_StringPtrLen`

    const char *VKM_StringPtrLen(const ValkeyModuleString *str, size_t *len);

Given a string module object, this function returns the string pointer
and length of the string. The returned pointer and length should only
be used for read only accesses and never modified.

## `VKM_StringToLongLong`

    int VKM_StringToLongLong(const ValkeyModuleString *str, long long *ll);

Convert the string into a long long integer, storing it at `*ll`.
Returns `VALKEYMODULE_OK` on success. If the string can't be parsed
as a valid, strict long long (no spaces before/after), `VALKEYMODULE_ERR`
is returned.

## `VKM_StringToDouble`

    int VKM_StringToDouble(const ValkeyModuleString *str, double *d);

Convert the string into a double, storing it at `*d`.
Returns `VALKEYMODULE_OK` on success or `VALKEYMODULE_ERR` if the string is
not a valid string representation of a double value.

## `VKM_StringCompare`

    int VKM_StringCompare(ValkeyModuleString *a, ValkeyModuleString *b);

Compare two string objects, returning -1, 0 or 1 respectively if
a < b, a == b, a > b. Strings are compared byte by byte as two
binary blobs without any encoding care / collation attempt.

## `VKM_StringAppendBuffer`

    int VKM_StringAppendBuffer(ValkeyModuleCtx *ctx, ValkeyModuleString *str, const char *buf, size_t len);

Append the specified buffere to the string 'str'. The string must be a
string created by the user that is referenced only a single time, otherwise
`VALKEYMODULE_ERR` is returend and the operation is not performed.

## `VKM_WrongArity`

    int VKM_WrongArity(ValkeyModuleCtx *ctx);

Send an error about the number of arguments given to the command,
citing the command name in the error message.

Example:

 if (argc != 3) return `ValkeyModule_WrongArity(ctx)`;

## `VKM_ReplyWithLongLong`

    int VKM_ReplyWithLongLong(ValkeyModuleCtx *ctx, long long ll);

Send an integer reply to the client, with the specified long long value.
The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplyWithError`

    int VKM_ReplyWithError(ValkeyModuleCtx *ctx, const char *err);

Reply with the error 'err'.

Note that 'err' must contain all the error, including
the initial error code. The function only provides the initial "-", so
the usage is, for example:

 `VKM_ReplyWithError(ctx`,"ERR Wrong Type");

and not just:

 `VKM_ReplyWithError(ctx`,"Wrong Type");

The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplyWithSimpleString`

    int VKM_ReplyWithSimpleString(ValkeyModuleCtx *ctx, const char *msg);

Reply with a simple string (+... \r\n in RESP protocol). This replies
are suitable only when sending a small non-binary string with small
overhead, like "OK" or similar replies.

The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplyWithArray`

    int VKM_ReplyWithArray(ValkeyModuleCtx *ctx, long len);

Reply with an array type of 'len' elements. However 'len' other calls
to `ReplyWith*` style functions must follow in order to emit the elements
of the array.

When producing arrays with a number of element that is not known beforehand
the function can be called with the special count
`VALKEYMODULE_POSTPONED_ARRAY_LEN`, and the actual number of elements can be
later set with `ValkeyModule_ReplySetArrayLength()` (which will set the
latest "open" count if there are multiple ones).

The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplySetArrayLength`

    void VKM_ReplySetArrayLength(ValkeyModuleCtx *ctx, long len);

When `ValkeyModule_ReplyWithArray()` is used with the argument
`VALKEYMODULE_POSTPONED_ARRAY_LEN`, because we don't know beforehand the number
of items we are going to output as elements of the array, this function
will take care to set the array length.

Since it is possible to have multiple array replies pending with unknown
length, this function guarantees to always set the latest array length
that was created in a postponed way.

For example in order to output an array like [1,[10,20,30]] we
could write:

 `ValkeyModule_ReplyWithArray(ctx`,`VALKEYMODULE_POSTPONED_ARRAY_LEN`);
 `ValkeyModule_ReplyWithLongLong(ctx`,1);
 `ValkeyModule_ReplyWithArray(ctx`,`VALKEYMODULE_POSTPONED_ARRAY_LEN`);
 `ValkeyModule_ReplyWithLongLong(ctx`,10);
 `ValkeyModule_ReplyWithLongLong(ctx`,20);
 `ValkeyModule_ReplyWithLongLong(ctx`,30);
 `ValkeyModule_ReplySetArrayLength(ctx`,3); // Set len of 10,20,30 array.
 `ValkeyModule_ReplySetArrayLength(ctx`,2); // Set len of top array

Note that in the above example there is no reason to postpone the array
length, since we produce a fixed number of elements, but in the practice
the code may use an interator or other ways of creating the output so
that is not easy to calculate in advance the number of elements.

## `VKM_ReplyWithStringBuffer`

    int VKM_ReplyWithStringBuffer(ValkeyModuleCtx *ctx, const char *buf, size_t len);

Reply with a bulk string, taking in input a C buffer pointer and length.

The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplyWithString`

    int VKM_ReplyWithString(ValkeyModuleCtx *ctx, ValkeyModuleString *str);

Reply with a bulk string, taking in input a ValkeyModuleString object.

The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplyWithNull`

    int VKM_ReplyWithNull(ValkeyModuleCtx *ctx);

Reply to the client with a NULL. In the RESP protocol a NULL is encoded
as the string "$-1\r\n".

The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplyWithCallReply`

    int VKM_ReplyWithCallReply(ValkeyModuleCtx *ctx, ValkeyModuleCallReply *reply);

Reply exactly what a Valkey command returned us with `ValkeyModule_Call()`.
This function is useful when we use `ValkeyModule_Call()` in order to
execute some command, as we want to reply to the client exactly the
same reply we obtained by the command.

The function always returns `VALKEYMODULE_OK`.

## `VKM_ReplyWithDouble`

    int VKM_ReplyWithDouble(ValkeyModuleCtx *ctx, double d);

Send a string reply obtained converting the double 'd' into a bulk string.
This function is basically equivalent to converting a double into
a string into a C buffer, and then calling the function
`ValkeyModule_ReplyWithStringBuffer()` with the buffer and length.

The function always returns `VALKEYMODULE_OK`.

## `VKM_Replicate`

    int VKM_Replicate(ValkeyModuleCtx *ctx, const char *cmdname, const char *fmt, ...);

Replicate the specified command and arguments to slaves and AOF, as effect
of execution of the calling command implementation.

The replicated commands are always wrapped into the MULTI/EXEC that
contains all the commands replicated in a given module command
execution. However the commands replicated with `ValkeyModule_Call()`
are the first items, the ones replicated with `ValkeyModule_Replicate()`
will all follow before the EXEC.

Modules should try to use one interface or the other.

This command follows exactly the same interface of `ValkeyModule_Call()`,
so a set of format specifiers must be passed, followed by arguments
matching the provided format specifiers.

Please refer to `ValkeyModule_Call()` for more information.

The command returns `VALKEYMODULE_ERR` if the format specifiers are invalid
or the command name does not belong to a known command.

## `VKM_ReplicateVerbatim`

    int VKM_ReplicateVerbatim(ValkeyModuleCtx *ctx);

This function will replicate the command exactly as it was invoked
by the client. Note that this function will not wrap the command into
a MULTI/EXEC stanza, so it should not be mixed with other replication
commands.

Basically this form of replication is useful when you want to propagate
the command to the slaves and AOF file exactly as it was called, since
the command can just be re-executed to deterministically re-create the
new state starting from the old one.

The function always returns `VALKEYMODULE_OK`.

## `VKM_GetClientId`

    unsigned long long VKM_GetClientId(ValkeyModuleCtx *ctx);

Return the ID of the current client calling the currently active module
command. The returned ID has a few guarantees:

1. The ID is different for each different client, so if the same client
   executes a module command multiple times, it can be recognized as
   having the same ID, otherwise the ID will be different.
2. The ID increases monotonically. Clients connecting to the server later
   are guaranteed to get IDs greater than any past ID previously seen.

Valid IDs are from 1 to 2^64-1. If 0 is returned it means there is no way
to fetch the ID in the context the function was currently called.

## `VKM_GetSelectedDb`

    int VKM_GetSelectedDb(ValkeyModuleCtx *ctx);

Return the currently selected DB.

## `VKM_SelectDb`

    int VKM_SelectDb(ValkeyModuleCtx *ctx, int newid);

Change the currently selected DB. Returns an error if the id
is out of range.

Note that the client will retain the currently selected DB even after
the Valkey command implemented by the module calling this function
returns.

If the module command wishes to change something in a different DB and
returns back to the original one, it should call `ValkeyModule_GetSelectedDb()`
before in order to restore the old DB number before returning.

## `VKM_OpenKey`

    void *VKM_OpenKey(ValkeyModuleCtx *ctx, robj *keyname, int mode);

Return an handle representing a Valkey key, so that it is possible
to call other APIs with the key handle as argument to perform
operations on the key.

The return value is the handle repesenting the key, that must be
closed with `VKM_CloseKey()`.

If the key does not exist and WRITE mode is requested, the handle
is still returned, since it is possible to perform operations on
a yet not existing key (that will be created, for example, after
a list push operation). If the mode is just READ instead, and the
key does not exist, NULL is returned. However it is still safe to
call `ValkeyModule_CloseKey()` and `ValkeyModule_KeyType()` on a NULL
value.

## `VKM_CloseKey`

    void VKM_CloseKey(ValkeyModuleKey *key);

Close a key handle.

## `VKM_KeyType`

    int VKM_KeyType(ValkeyModuleKey *key);

Return the type of the key. If the key pointer is NULL then
`VALKEYMODULE_KEYTYPE_EMPTY` is returned.

## `VKM_ValueLength`

    size_t VKM_ValueLength(ValkeyModuleKey *key);

Return the length of the value associated with the key.
For strings this is the length of the string. For all the other types
is the number of elements (just counting keys for hashes).

If the key pointer is NULL or the key is empty, zero is returned.

## `VKM_DeleteKey`

    int VKM_DeleteKey(ValkeyModuleKey *key);

If the key is open for writing, remove it, and setup the key to
accept new writes as an empty key (that will be created on demand).
On success `VALKEYMODULE_OK` is returned. If the key is not open for
writing `VALKEYMODULE_ERR` is returned.

## `VKM_GetExpire`

    mstime_t VKM_GetExpire(ValkeyModuleKey *key);

Return the key expire value, as milliseconds of remaining TTL.
If no TTL is associated with the key or if the key is empty,
`VALKEYMODULE_NO_EXPIRE` is returned.

## `VKM_SetExpire`

    int VKM_SetExpire(ValkeyModuleKey *key, mstime_t expire);

Set a new expire for the key. If the special expire
`VALKEYMODULE_NO_EXPIRE` is set, the expire is cancelled if there was
one (the same as the PERSIST command).

Note that the expire must be provided as a positive integer representing
the number of milliseconds of TTL the key should have.

The function returns `VALKEYMODULE_OK` on success or `VALKEYMODULE_ERR` if
the key was not open for writing or is an empty key.

## `VKM_StringSet`

    int VKM_StringSet(ValkeyModuleKey *key, ValkeyModuleString *str);

If the key is open for writing, set the specified string 'str' as the
value of the key, deleting the old value if any.
On success `VALKEYMODULE_OK` is returned. If the key is not open for
writing or there is an active iterator, `VALKEYMODULE_ERR` is returned.

## `VKM_StringDMA`

    char *VKM_StringDMA(ValkeyModuleKey *key, size_t *len, int mode);

Prepare the key associated string value for DMA access, and returns
a pointer and size (by reference), that the user can use to read or
modify the string in-place accessing it directly via pointer.

The 'mode' is composed by bitwise OR-ing the following flags:

`VALKEYMODULE_READ` -- Read access
`VALKEYMODULE_WRITE` -- Write access

If the DMA is not requested for writing, the pointer returned should
only be accessed in a read-only fashion.

On error (wrong type) NULL is returned.

DMA access rules:

1. No other key writing function should be called since the moment
the pointer is obtained, for all the time we want to use DMA access
to read or modify the string.

2. Each time `VKM_StringTruncate()` is called, to continue with the DMA
access, `VKM_StringDMA()` should be called again to re-obtain
a new pointer and length.

3. If the returned pointer is not NULL, but the length is zero, no
byte can be touched (the string is empty, or the key itself is empty)
so a `VKM_StringTruncate()` call should be used if there is to enlarge
the string, and later call StringDMA() again to get the pointer.

## `VKM_StringTruncate`

    int VKM_StringTruncate(ValkeyModuleKey *key, size_t newlen);

If the string is open for writing and is of string type, resize it, padding
with zero bytes if the new length is greater than the old one.

After this call, `VKM_StringDMA()` must be called again to continue
DMA access with the new pointer.

The function returns `VALKEYMODULE_OK` on success, and `VALKEYMODULE_ERR` on
error, that is, the key is not open for writing, is not a string
or resizing for more than 512 MB is requested.

If the key is empty, a string key is created with the new string value
unless the new length value requested is zero.

## `VKM_ListPush`

    int VKM_ListPush(ValkeyModuleKey *key, int where, ValkeyModuleString *ele);

Push an element into a list, on head or tail depending on 'where' argumnet.
If the key pointer is about an empty key opened for writing, the key
is created. On error (key opened for read-only operations or of the wrong
type) `VALKEYMODULE_ERR` is returned, otherwise `VALKEYMODULE_OK` is returned.

## `VKM_ListPop`

    ValkeyModuleString *VKM_ListPop(ValkeyModuleKey *key, int where);

Pop an element from the list, and returns it as a module string object
that the user should be free with `VKM_FreeString()` or by enabling
automatic memory. 'where' specifies if the element should be popped from
head or tail. The command returns NULL if:
1) The list is empty.
2) The key was not open for writing.
3) The key is not a list.

## `VKM_ZsetAddFlagsToCoreFlags`

    int VKM_ZsetAddFlagsToCoreFlags(int flags);

Conversion from/to public flags of the Modules API and our private flags,
so that we have everything decoupled.

## `VKM_ZsetAddFlagsFromCoreFlags`

    int VKM_ZsetAddFlagsFromCoreFlags(int flags);

See previous function comment.

## `VKM_ZsetAdd`

    int VKM_ZsetAdd(ValkeyModuleKey *key, double score, ValkeyModuleString *ele, int *flagsptr);

Add a new element into a sorted set, with the specified 'score'.
If the element already exists, the score is updated.

A new sorted set is created at value if the key is an empty open key
setup for writing.

Additional flags can be passed to the function via a pointer, the flags
are both used to receive input and to communicate state when the function
returns. 'flagsptr' can be NULL if no special flags are used.

The input flags are:

`VALKEYMODULE_ZADD_XX`: Element must already exist. Do nothing otherwise.
`VALKEYMODULE_ZADD_NX`: Element must not exist. Do nothing otherwise.

The output flags are:

`VALKEYMODULE_ZADD_ADDED`: The new element was added to the sorted set.
`VALKEYMODULE_ZADD_UPDATED`: The score of the element was updated.
`VALKEYMODULE_ZADD_NOP`: No operation was performed because XX or NX flags.

On success the function returns `VALKEYMODULE_OK`. On the following errors
`VALKEYMODULE_ERR` is returned:

* The key was not opened for writing.
* The key is of the wrong type.
* 'score' double value is not a number (NaN).

## `VKM_ZsetIncrby`

    int VKM_ZsetIncrby(ValkeyModuleKey *key, double score, ValkeyModuleString *ele, int *flagsptr, double *newscore);

This function works exactly like `VKM_ZsetAdd()`, but instead of setting
a new score, the score of the existing element is incremented, or if the
element does not already exist, it is added assuming the old score was
zero.

The input and output flags, and the return value, have the same exact
meaning, with the only difference that this function will return
`VALKEYMODULE_ERR` even when 'score' is a valid double number, but adding it
to the existing score resuts into a NaN (not a number) condition.

This function has an additional field 'newscore', if not NULL is filled
with the new score of the element after the increment, if no error
is returned.

## `VKM_ZsetRem`

    int VKM_ZsetRem(ValkeyModuleKey *key, ValkeyModuleString *ele, int *deleted);

Remove the specified element from the sorted set.
The function returns `VALKEYMODULE_OK` on success, and `VALKEYMODULE_ERR`
on one of the following conditions:

* The key was not opened for writing.
* The key is of the wrong type.

The return value does NOT indicate the fact the element was really
removed (since it existed) or not, just if the function was executed
with success.

In order to know if the element was removed, the additional argument
'deleted' must be passed, that populates the integer by reference
setting it to 1 or 0 depending on the outcome of the operation.
The 'deleted' argument can be NULL if the caller is not interested
to know if the element was really removed.

Empty keys will be handled correctly by doing nothing.

## `VKM_ZsetScore`

    int VKM_ZsetScore(ValkeyModuleKey *key, ValkeyModuleString *ele, double *score);

On success retrieve the double score associated at the sorted set element
'ele' and returns `VALKEYMODULE_OK`. Otherwise `VALKEYMODULE_ERR` is returned
to signal one of the following conditions:

* There is no such element 'ele' in the sorted set.
* The key is not a sorted set.
* The key is an open empty key.

## `VKM_ZsetRangeStop`

    void VKM_ZsetRangeStop(ValkeyModuleKey *key);

Stop a sorted set iteration.

## `VKM_ZsetRangeEndReached`

    int VKM_ZsetRangeEndReached(ValkeyModuleKey *key);

Return the "End of range" flag value to signal the end of the iteration.

## `VKM_ZsetFirstInScoreRange`

    int VKM_ZsetFirstInScoreRange(ValkeyModuleKey *key, double min, double max, int minex, int maxex);

Setup a sorted set iterator seeking the first element in the specified
range. Returns `VALKEYMODULE_OK` if the iterator was correctly initialized
otherwise `VALKEYMODULE_ERR` is returned in the following conditions:

1. The value stored at key is not a sorted set or the key is empty.

The range is specified according to the two double values 'min' and 'max'.
Both can be infinite using the following two macros:

`VALKEYMODULE_POSITIVE_INFINITE` for positive infinite value
`VALKEYMODULE_NEGATIVE_INFINITE` for negative infinite value

'minex' and 'maxex' parameters, if true, respectively setup a range
where the min and max value are exclusive (not included) instead of
inclusive.

## `VKM_ZsetLastInScoreRange`

    int VKM_ZsetLastInScoreRange(ValkeyModuleKey *key, double min, double max, int minex, int maxex);

Exactly like `ValkeyModule_ZsetFirstInScoreRange()` but the last element of
the range is selected for the start of the iteration instead.

## `VKM_ZsetFirstInLexRange`

    int VKM_ZsetFirstInLexRange(ValkeyModuleKey *key, ValkeyModuleString *min, ValkeyModuleString *max);

Setup a sorted set iterator seeking the first element in the specified
lexicographical range. Returns `VALKEYMODULE_OK` if the iterator was correctly
initialized otherwise `VALKEYMODULE_ERR` is returned in the
following conditions:

1. The value stored at key is not a sorted set or the key is empty.
2. The lexicographical range 'min' and 'max' format is invalid.

'min' and 'max' should be provided as two ValkeyModuleString objects
in the same format as the parameters passed to the ZRANGEBYLEX command.
The function does not take ownership of the objects, so they can be released
ASAP after the iterator is setup.

## `VKM_ZsetLastInLexRange`

    int VKM_ZsetLastInLexRange(ValkeyModuleKey *key, ValkeyModuleString *min, ValkeyModuleString *max);

Exactly like `ValkeyModule_ZsetFirstInLexRange()` but the last element of
the range is selected for the start of the iteration instead.

## `VKM_ZsetRangeCurrentElement`

    ValkeyModuleString *VKM_ZsetRangeCurrentElement(ValkeyModuleKey *key, double *score);

Return the current sorted set element of an active sorted set iterator
or NULL if the range specified in the iterator does not include any
element.

## `VKM_ZsetRangeNext`

    int VKM_ZsetRangeNext(ValkeyModuleKey *key);

Go to the next element of the sorted set iterator. Returns 1 if there was
a next element, 0 if we are already at the latest element or the range
does not include any item at all.

## `VKM_ZsetRangePrev`

    int VKM_ZsetRangePrev(ValkeyModuleKey *key);

Go to the previous element of the sorted set iterator. Returns 1 if there was
a previous element, 0 if we are already at the first element or the range
does not include any item at all.

## `VKM_HashSet`

    int VKM_HashSet(ValkeyModuleKey *key, int flags, ...);

Set the field of the specified hash field to the specified value.
If the key is an empty key open for writing, it is created with an empty
hash value, in order to set the specified field.

The function is variadic and the user must specify pairs of field
names and values, both as ValkeyModuleString pointers (unless the
CFIELD option is set, see later).

Example to set the hash argv[1] to the value argv[2]:

 `ValkeyModule_HashSet(key`,`VALKEYMODULE_HASH_NONE`,argv[1],argv[2],NULL);

The function can also be used in order to delete fields (if they exist)
by setting them to the specified value of `VALKEYMODULE_HASH_DELETE`:

 `ValkeyModule_HashSet(key`,`VALKEYMODULE_HASH_NONE`,argv[1],
                     `VALKEYMODULE_HASH_DELETE`,NULL);

The behavior of the command changes with the specified flags, that can be
set to `VALKEYMODULE_HASH_NONE` if no special behavior is needed.

`VALKEYMODULE_HASH_NX`: The operation is performed only if the field was not
                    already existing in the hash.
`VALKEYMODULE_HASH_XX`: The operation is performed only if the field was
                    already existing, so that a new value could be
                    associated to an existing filed, but no new fields
                    are created.
`VALKEYMODULE_HASH_CFIELDS`: The field names passed are null terminated C
                         strings instead of ValkeyModuleString objects.

Unless NX is specified, the command overwrites the old field value with
the new one.

When using `VALKEYMODULE_HASH_CFIELDS`, field names are reported using
normal C strings, so for example to delete the field "foo" the following
code can be used:

 `ValkeyModule_HashSet(key`,`VALKEYMODULE_HASH_CFIELDS`,"foo",
                     `VALKEYMODULE_HASH_DELETE`,NULL);

Return value:

The number of fields updated (that may be less than the number of fields
specified because of the XX or NX options).

In the following case the return value is always zero:

* The key was not open for writing.
* The key was associated with a non Hash value.

## `VKM_HashGet`

    int VKM_HashGet(ValkeyModuleKey *key, int flags, ...);

Get fields from an hash value. This function is called using a variable
number of arguments, alternating a field name (as a StringValkeyModule
pointer) with a pointer to a StringValkeyModule pointer, that is set to the
value of the field if the field exist, or NULL if the field did not exist.
At the end of the field/value-ptr pairs, NULL must be specified as last
argument to signal the end of the arguments in the variadic function.

This is an example usage:

     ValkeyModuleString *first, *second;
     `ValkeyModule_HashGet(mykey`,`VALKEYMODULE_HASH_NONE`,argv[1],&first,
                     argv[2],&second,NULL);

As with `ValkeyModule_HashSet()` the behavior of the command can be specified
passing flags different than `VALKEYMODULE_HASH_NONE`:

`VALKEYMODULE_HASH_CFIELD`: field names as null terminated C strings.

`VALKEYMODULE_HASH_EXISTS`: instead of setting the value of the field
expecting a ValkeyModuleString pointer to pointer, the function just
reports if the field esists or not and expects an integer pointer
as the second element of each pair.

Example of `VALKEYMODULE_HASH_CFIELD`:

     ValkeyModuleString *username, *hashedpass;
     `ValkeyModule_HashGet(mykey`,"username",&username,"hp",&hashedpass, NULL);

Example of `VALKEYMODULE_HASH_EXISTS`:

     int exists;
     `ValkeyModule_HashGet(mykey`,argv[1],&exists,NULL);

The function returns `VALKEYMODULE_OK` on success and `VALKEYMODULE_ERR` if
the key is not an hash value.

Memory management:

The returned ValkeyModuleString objects should be released with
`ValkeyModule_FreeString()`, or by enabling automatic memory management.

## `VKM_FreeCallReply_Rec`

    void VKM_FreeCallReply_Rec(ValkeyModuleCallReply *reply, int freenested);

Free a Call reply and all the nested replies it contains if it's an
array.

## `VKM_FreeCallReply`

    void VKM_FreeCallReply(ValkeyModuleCallReply *reply);

Wrapper for the recursive free reply function. This is needed in order
to have the first level function to return on nested replies, but only
if called by the module API.

## `VKM_CallReplyType`

    int VKM_CallReplyType(ValkeyModuleCallReply *reply);

Return the reply type.

## `VKM_CallReplyLength`

    size_t VKM_CallReplyLength(ValkeyModuleCallReply *reply);

Return the reply type length, where applicable.

## `VKM_CallReplyArrayElement`

    ValkeyModuleCallReply *VKM_CallReplyArrayElement(ValkeyModuleCallReply *reply, size_t idx);

Return the 'idx'-th nested call reply element of an array reply, or NULL
if the reply type is wrong or the index is out of range.

## `VKM_CallReplyInteger`

    long long VKM_CallReplyInteger(ValkeyModuleCallReply *reply);

Return the long long of an integer reply.

## `VKM_CallReplyStringPtr`

    const char *VKM_CallReplyStringPtr(ValkeyModuleCallReply *reply, size_t *len);

Return the pointer and length of a string or error reply.

## `VKM_CreateStringFromCallReply`

    ValkeyModuleString *VKM_CreateStringFromCallReply(ValkeyModuleCallReply *reply);

Return a new string object from a call reply of type string, error or
integer. Otherwise (wrong reply type) return NULL.

## `VKM_Call`

    ValkeyModuleCallReply *VKM_Call(ValkeyModuleCtx *ctx, const char *cmdname, const char *fmt, ...);

Exported API to call any Valkey command from modules.
On success a ValkeyModuleCallReply object is returned, otherwise
NULL is returned and errno is set to the following values:

EINVAL: command non existing, wrong arity, wrong format specifier.
EPEVKM:  operation in Cluster instance with key in non local slot.

## `VKM_CallReplyProto`

    const char *VKM_CallReplyProto(ValkeyModuleCallReply *reply, size_t *len);

Return a pointer, and a length, to the protocol returned by the command
that returned the reply object.

## `VKM_CreateDataType`

    moduleType *VKM_CreateDataType(ValkeyModuleCtx *ctx, const char *name, int encver, void *typemethods_ptr);

Register a new data type exported by the module. The parameters are the
following. Please for in depth documentation check the modules API
documentation, especially the TYPES.md file.

* **name**: A 9 characters data type name that MUST be unique in the Valkey
  Modules ecosystem. Be creative... and there will be no collisions. Use
  the charset A-Z a-z 9-0, plus the two "-_" characters. A good
  idea is to use, for example `<typename>-<vendor>`. For example
  "tree-AntZ" may mean "Tree data structure by @antirez". To use both
  lower case and upper case letters helps in order to prevent collisions.
* **encver**: Encoding version, which is, the version of the serialization
  that a module used in order to persist data. As long as the "name"
  matches, the RDB loading will be dispatched to the type callbacks
  whatever 'encver' is used, however the module can understand if
  the encoding it must load are of an older version of the module.
  For example the module "tree-AntZ" initially used encver=0. Later
  after an upgrade, it started to serialize data in a different format
  and to register the type with encver=1. However this module may
  still load old data produced by an older version if the rdb_load
  callback is able to check the encver value and act accordingly.
  The encver must be a positive value between 0 and 1023.
* **typemethods_ptr** is a pointer to a ValkeyModuleTypeMethods structure
  that should be populated with the methods callbacks and structure
  version, like in the following example:

     ValkeyModuleTypeMethods tm = {
         .version = `VALKEYMODULE_TYPE_METHOD_VERSION`,
         .rdb_load = myType_RDBLoadCallBack,
         .rdb_save = myType_RDBSaveCallBack,
         .aof_rewrite = myType_AOFRewriteCallBack,
         .free = myType_FreeCallBack,

         // Optional fields
         .digest = myType_DigestCallBack,
         .mem_usage = myType_MemUsageCallBack,
     }

* **rdb_load**: A callback function pointer that loads data from RDB files.
* **rdb_save**: A callback function pointer that saves data to RDB files.
* **aof_rewrite**: A callback function pointer that rewrites data as commands.
* **digest**: A callback function pointer that is used for `DEBUG DIGEST`.
* **free**: A callback function pointer that can free a type value.

The **digest* and **mem_usage** methods should currently be omitted since
they are not yet implemented inside the Valkey modules core.

Note: the module name "AAAAAAAAA" is reserved and produces an error, it
happens to be pretty lame as well.

If there is already a module registering a type with the same name,
and if the module name or encver is invalid, NULL is returned.
Otherwise the new type is registered into Valkey, and a reference of
type ValkeyModuleType is returned: the caller of the function should store
this reference into a gobal variable to make future use of it in the
modules type API, since a single module may register multiple types.
Example code fragment:

     static ValkeyModuleType *BalancedTreeType;

     int `ValkeyModule_OnLoad(ValkeyModuleCtx` *ctx) {
         // some code here ...
         BalancedTreeType = `VKM_CreateDataType(`...);
     }

## `VKM_ModuleTypeSetValue`

    int VKM_ModuleTypeSetValue(ValkeyModuleKey *key, moduleType *mt, void *value);

If the key is open for writing, set the specified module type object
as the value of the key, deleting the old value if any.
On success `VALKEYMODULE_OK` is returned. If the key is not open for
writing or there is an active iterator, `VALKEYMODULE_ERR` is returned.

## `VKM_ModuleTypeGetType`

    moduleType *VKM_ModuleTypeGetType(ValkeyModuleKey *key);

Assuming `ValkeyModule_KeyType()` returned `VALKEYMODULE_KEYTYPE_MODULE` on
the key, returns the moduel type pointer of the value stored at key.

If the key is NULL, is not associated with a module type, or is empty,
then NULL is returned instead.

## `VKM_ModuleTypeGetValue`

    void *VKM_ModuleTypeGetValue(ValkeyModuleKey *key);

Assuming `ValkeyModule_KeyType()` returned `VALKEYMODULE_KEYTYPE_MODULE` on
the key, returns the module type low-level value stored at key, as
it was set by the user via `ValkeyModule_ModuleTypeSet()`.

If the key is NULL, is not associated with a module type, or is empty,
then NULL is returned instead.

## `VKM_SaveUnsigned`

    void VKM_SaveUnsigned(ValkeyModuleIO *io, uint64_t value);

Save an unsigned 64 bit value into the RDB file. This function should only
be called in the context of the rdb_save method of modules implementing new
data types.

## `VKM_LoadUnsigned`

    uint64_t VKM_LoadUnsigned(ValkeyModuleIO *io);

Load an unsigned 64 bit value from the RDB file. This function should only
be called in the context of the rdb_load method of modules implementing
new data types.

## `VKM_SaveSigned`

    void VKM_SaveSigned(ValkeyModuleIO *io, int64_t value);

Like `ValkeyModule_SaveUnsigned()` but for signed 64 bit values.

## `VKM_LoadSigned`

    int64_t VKM_LoadSigned(ValkeyModuleIO *io);

Like `ValkeyModule_LoadUnsigned()` but for signed 64 bit values.

## `VKM_SaveString`

    void VKM_SaveString(ValkeyModuleIO *io, ValkeyModuleString *s);

In the context of the rdb_save method of a module type, saves a
string into the RDB file taking as input a ValkeyModuleString.

The string can be later loaded with `ValkeyModule_LoadString()` or
other Load family functions expecting a serialized string inside
the RDB file.

## `VKM_SaveStringBuffer`

    void VKM_SaveStringBuffer(ValkeyModuleIO *io, const char *str, size_t len);

Like `ValkeyModule_SaveString()` but takes a raw C pointer and length
as input.

## `VKM_LoadString`

    ValkeyModuleString *VKM_LoadString(ValkeyModuleIO *io);

In the context of the rdb_load method of a module data type, loads a string
from the RDB file, that was previously saved with `ValkeyModule_SaveString()`
functions family.

The returned string is a newly allocated ValkeyModuleString object, and
the user should at some point free it with a call to `ValkeyModule_FreeString()`.

If the data structure does not store strings as ValkeyModuleString objects,
the similar function `ValkeyModule_LoadStringBuffer()` could be used instead.

## `VKM_LoadStringBuffer`

    char *VKM_LoadStringBuffer(ValkeyModuleIO *io, size_t *lenptr);

Like `ValkeyModule_LoadString()` but returns an heap allocated string that
was allocated with `ValkeyModule_Alloc()`, and can be resized or freed with
`ValkeyModule_Realloc()` or `ValkeyModule_Free()`.

The size of the string is stored at '*lenptr' if not NULL.
The returned string is not automatically NULL termianted, it is loaded
exactly as it was stored inisde the RDB file.

## `VKM_SaveDouble`

    void VKM_SaveDouble(ValkeyModuleIO *io, double value);

In the context of the rdb_save method of a module data type, saves a double
value to the RDB file. The double can be a valid number, a NaN or infinity.
It is possible to load back the value with `ValkeyModule_LoadDouble()`.

## `VKM_LoadDouble`

    double VKM_LoadDouble(ValkeyModuleIO *io);

In the context of the rdb_save method of a module data type, loads back the
double value saved by `ValkeyModule_SaveDouble()`.

## `VKM_SaveFloat`

    void VKM_SaveFloat(ValkeyModuleIO *io, float value);

In the context of the rdb_save method of a module data type, saves a float 
value to the RDB file. The float can be a valid number, a NaN or infinity.
It is possible to load back the value with `ValkeyModule_LoadFloat()`.

## `VKM_LoadFloat`

    float VKM_LoadFloat(ValkeyModuleIO *io);

In the context of the rdb_save method of a module data type, loads back the
float value saved by `ValkeyModule_SaveFloat()`.

## `VKM_EmitAOF`

    void VKM_EmitAOF(ValkeyModuleIO *io, const char *cmdname, const char *fmt, ...);

Emits a command into the AOF during the AOF rewriting process. This function
is only called in the context of the aof_rewrite method of data types exported
by a module. The command works exactly like `ValkeyModule_Call()` in the way
the parameters are passed, but it does not return anything as the error
handling is performed by Valkey itself.

## `VKM_LogRaw`

    void VKM_LogRaw(ValkeyModule *module, const char *levelstr, const char *fmt, va_list ap);

This is the low level function implementing both:

 `VKM_Log()`
 `VKM_LogIOError()`

## `VKM_Log`

    void VKM_Log(ValkeyModuleCtx *ctx, const char *levelstr, const char *fmt, ...);

/*
Produces a log message to the standard Valkey log, the format accepts
printf-alike specifiers, while level is a string describing the log
level to use when emitting the log, and must be one of the following:

* "debug"
* "verbose"
* "notice"
* "warning"

If the specified log level is invalid, verbose is used by default.
There is a fixed limit to the length of the log line this function is able
to emit, this limti is not specified but is guaranteed to be more than
a few lines of text.

## `VKM_LogIOError`

    void VKM_LogIOError(ValkeyModuleIO *io, const char *levelstr, const char *fmt, ...);

Log errors from RDB / AOF serialization callbacks.

This function should be used when a callback is returning a critical
error to the caller since cannot load or save the data for some
critical reason.

## `VKM_BlockClient`

    ValkeyModuleBlockedClient *VKM_BlockClient(ValkeyModuleCtx *ctx, ValkeyModuleCmdFunc reply_callback, ValkeyModuleCmdFunc timeout_callback, void (*free_privdata)(void*), long long timeout_ms);

Block a client in the context of a blocking command, returning an handle
which will be used, later, in order to block the client with a call to
`ValkeyModule_UnblockClient()`. The arguments specify callback functions
and a timeout after which the client is unblocked.

The callbacks are called in the following contexts:

reply_callback:  called after a successful `ValkeyModule_UnblockClient()` call
                 in order to reply to the client and unblock it.
reply_timeout:   called when the timeout is reached in order to send an
                 error to the client.
free_privdata:   called in order to free the privata data that is passed
                 by `ValkeyModule_UnblockClient()` call.

## `VKM_UnblockClient`

    int VKM_UnblockClient(ValkeyModuleBlockedClient *bc, void *privdata);

Unblock a client blocked by ``ValkeyModule_BlockedClient``. This will trigger
the reply callbacks to be called in order to reply to the client.
The 'privdata' argument will be accessible by the reply callback, so
the caller of this function can pass any value that is needed in order to
actually reply to the client.

A common usage for 'privdata' is a thread that computes something that
needs to be passed to the client, included but not limited some slow
to compute reply or some reply obtained via networking.

Note: this function can be called from threads spawned by the module.

## `VKM_AbortBlock`

    int VKM_AbortBlock(ValkeyModuleBlockedClient *bc);

Abort a blocked client blocking operation: the client will be unblocked
without firing the reply callback.

## `VKM_IsBlockedReplyRequest`

    int VKM_IsBlockedReplyRequest(ValkeyModuleCtx *ctx);

Return non-zero if a module command was called in order to fill the
reply for a blocked client.

## `VKM_IsBlockedTimeoutRequest`

    int VKM_IsBlockedTimeoutRequest(ValkeyModuleCtx *ctx);

Return non-zero if a module command was called in order to fill the
reply for a blocked client that timed out.

## `VKM_GetBlockedClientPrivateData`

    void *VKM_GetBlockedClientPrivateData(ValkeyModuleCtx *ctx);

Get the privata data set by `ValkeyModule_UnblockClient()`

