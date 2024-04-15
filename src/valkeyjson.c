/*
 * ValkeyJSON - a JSON data type for Valkey
 * Copyright (C) 2017 Redis Labs
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "valkeyjson.h"
#include "cache.h"

// A struct to keep module the module context
typedef struct {
    JSONObjectCtx *joctx;
} ModuleCtx;
static ModuleCtx JSONCtx;

// == Helpers ==
#define NODEVALUE_AS_DOUBLE(n) (N_INTEGER == n->type ? (double)n->value.intval : n->value.numval)
#define NODETYPE(n) (n ? n->type : N_NULL)
struct JSONPathNode_t;
static void maybeClearPathCache(JSONType_t *jt, const struct JSONPathNode_t *pn);
/* Returns the string representation of a the node's type. */
static inline char *NodeTypeStr(const NodeType nt) {
    static char *types[] = {"null", "boolean", "integer", "number", "string", "object", "array"};
    switch (nt) {
        case N_NULL:
            return types[0];
        case N_BOOLEAN:
            return types[1];
        case N_INTEGER:
            return types[2];
        case N_NUMBER:
            return types[3];
        case N_STRING:
            return types[4];
        case N_DICT:
            return types[5];
        case N_ARRAY:
            return types[6];
        case N_KEYVAL:
            return NULL;  // this **should** never be reached
    }
    return NULL;  // this is never reached
}

/* Check if a search path is the root search path. */
static inline int SearchPath_IsRootPath(const SearchPath *sp) {
    return (1 == sp->len && NT_ROOT == sp->nodes[0].type);
}

/* Stores everything about a resolved path. */
typedef struct JSONPathNode_t {
    const char *spath;   // the path's string
    size_t spathlen;     // the path's string length
    Node *n;             // the referenced node
    Node *p;             // its parent
    SearchPath sp;       // the search path
    char *sperrmsg;      // the search path error message
    size_t sperroffset;  // the search path error offset
    PathError err;       // set in case of path error
    int errlevel;        // indicates the level of the error in the path
} JSONPathNode_t;

/* Call this to free the struct's contents. */
void JSONPathNode_Free(JSONPathNode_t *jpn) {
    if (jpn) {
        SearchPath_Free(&jpn->sp);
        ValkeyModule_Free(jpn);
    }
}

/* Sets n to the target node by path.
 * p is n's parent, errors are set into err and level is the error's depth
 * Returns PARSE_OK if parsing successful
 */
int NodeFromJSONPath(Node *root, const ValkeyModuleString *path, JSONPathNode_t **jpn) {
    // initialize everything
    JSONPathNode_t *_jpn = ValkeyModule_Calloc(1, sizeof(JSONPathNode_t));
    _jpn->errlevel = -1;
    JSONSearchPathError_t jsperr = {0};

    // path must be valid from the root or it's an error
    _jpn->sp = NewSearchPath(0);
    _jpn->spath = ValkeyModule_StringPtrLen(path, &_jpn->spathlen);
    if (PARSE_ERR == ParseJSONPath(_jpn->spath, _jpn->spathlen, &_jpn->sp, &jsperr)) {
        SearchPath_Free(&_jpn->sp);
        _jpn->sp.nodes = NULL;  // in case someone tries to free it later
        _jpn->sperrmsg = jsperr.errmsg;
        _jpn->sperroffset = jsperr.offset;
        *jpn = _jpn;
        return PARSE_ERR;
    }

    // if there are any errors return them
    if (!SearchPath_IsRootPath(&_jpn->sp)) {
        _jpn->err = SearchPath_FindEx(&_jpn->sp, root, &_jpn->n, &_jpn->p, &_jpn->errlevel);
    } else {
        // deal with edge case of setting root's parent
        _jpn->n = root;
    }

    *jpn = _jpn;
    return PARSE_OK;
}

/* Replies with an error about a search path */
void ReplyWithSearchPathError(ValkeyModuleCtx *ctx, JSONPathNode_t *jpn) {
    sds err = sdscatfmt(sdsempty(), "ERR Search path error at offset %I: %s",
                        (long long)jpn->sperroffset + 1, jpn->sperrmsg ? jpn->sperrmsg : "(null)");
    ValkeyModule_ReplyWithError(ctx, err);
    sdsfree(err);
}

/* Replies with an error about a wrong type of node in a path */
void ReplyWithPathTypeError(ValkeyModuleCtx *ctx, NodeType expected, NodeType actual) {
    sds err = sdscatfmt(sdsempty(), VALKEYJSON_ERROR_PATH_WRONGTYPE, NodeTypeStr(expected),
                        NodeTypeStr(actual));
    ValkeyModule_ReplyWithError(ctx, err);
    sdsfree(err);
}

/* Generic path error reply handler */
void ReplyWithPathError(ValkeyModuleCtx *ctx, const JSONPathNode_t *jpn) {
    // TODO: report actual position in path & literal token
    PathNode *epn = &jpn->sp.nodes[jpn->errlevel];
    sds err = sdsempty();
    switch (jpn->err) {
        case E_OK:
            err = sdscat(err, "ERR nothing wrong with path");
            break;
        case E_BADTYPE:
            if (NT_KEY == epn->type) {
                err = sdscatfmt(err, "ERR invalid key '[\"%s\"]' at level %i in path",
                                epn->value.key, jpn->errlevel);
            } else {
                err = sdscatfmt(err, "ERR invalid index '[%i]' at level %i in path",
                                epn->value.index, jpn->errlevel);
            }
            break;
        case E_NOINDEX:
            err = sdscatfmt(err, "ERR index '[%i]' out of range at level %i in path",
                            epn->value.index, jpn->errlevel);
            break;
        case E_NOKEY:
            err = sdscatfmt(err, "ERR key '%s' does not exist at level %i in path", epn->value.key,
                            jpn->errlevel);
            break;
        default:
            err = sdscatfmt(err, "ERR unknown path error at level %i in path", jpn->errlevel);
            break;
    }  // switch (err)
    ValkeyModule_ReplyWithError(ctx, err);
    sdsfree(err);
}

/* The custom Valkey data type. */
static ValkeyModuleType *JSONType;

// == Module JSON commands ==

/**
* JSON.RESP <key> [path]
* Return the JSON in `key` in RESP.
*
* `path` defaults to root if not provided.
* This command uses the following mapping from JSON to RESP:
* - JSON Null is mapped to the RESP Null Bulk String
* - JSON `false` and `true` values are mapped to the respective RESP Simple Strings
* - JSON Numbers are mapped to RESP Integers or RESP Bulk Strings, depending on type
* - JSON Strings are mapped to RESP Bulk Strings
* - JSON Arrays are represented as RESP Arrays in which first element is the simple string `[`
*   followed by the array's elements
* - JSON Objects are represented as RESP Arrays in which first element is the simple string `{`.
    Each successive entry represents a key-value pair as a two-entries array of bulk strings.
*
* Reply: Array, specifically the JSON's RESP form.
*/
int JSONResp_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if ((argc < 2) || (argc > 3)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key must be empty (reply with null) or a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_ReplyWithNull(ctx);
        return VALKEYMODULE_OK;
    } else if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        {
            ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
            return VALKEYMODULE_ERR;
        }
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (3 == argc ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    if (E_OK == jpn->err) {
        ObjectTypeToRespReply(ctx, jpn->n);
    } else {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    JSONPathNode_Free(jpn);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.DEBUG <subcommand & arguments>
 * Report information.
 *
 * Supported subcommands are:
 *   `MEMORY <key> [path]` - report the memory usage in bytes of a value. `path` defaults to root if
 *   not provided.
 *  `HELP` - replies with a helpful message
 *
 * Reply: depends on the subcommand used:
 *   `MEMORY` returns an integer, specifically the size in bytes of the value
 *   `HELP` returns an array, specifically with the help message
 */
int JSONDebug_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check for minimal arity
    if (argc < 2) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    size_t subcmdlen;
    const char *subcmd = ValkeyModule_StringPtrLen(argv[1], &subcmdlen);
    if (!strncasecmp("memory", subcmd, subcmdlen)) {
        // verify we have enough arguments
        if ((argc < 3) || (argc > 4)) {
            ValkeyModule_WrongArity(ctx);
            return VALKEYMODULE_ERR;
        }

        // reply to getkeys-api requests
        if (ValkeyModule_IsKeysPositionRequest(ctx)) {
            ValkeyModule_KeyAtPos(ctx, 2);
            return VALKEYMODULE_OK;
        }

        // key must be empty (reply with null) or a JSON type
        ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[2], VALKEYMODULE_READ);
        int type = ValkeyModule_KeyType(key);
        if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
            ValkeyModule_ReplyWithNull(ctx);
            return VALKEYMODULE_OK;
        } else if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
            ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
            return VALKEYMODULE_ERR;
        }

        // validate path
        JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
        JSONPathNode_t *jpn = NULL;
        ValkeyModuleString *spath =
            (4 == argc ? argv[3] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
        if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
            ReplyWithSearchPathError(ctx, jpn);
            JSONPathNode_Free(jpn);
            return VALKEYMODULE_ERR;
        }

        if (E_OK == jpn->err) {
            ValkeyModule_ReplyWithLongLong(ctx, (long long)ObjectTypeMemoryUsage(jpn->n));
            JSONPathNode_Free(jpn);
            return VALKEYMODULE_OK;
        } else {
            ReplyWithPathError(ctx, jpn);
            JSONPathNode_Free(jpn);
            return VALKEYMODULE_ERR;
        }
    } else if (!strncasecmp("help", subcmd, subcmdlen)) {
        const char *help[] = {"MEMORY <key> [path] - reports memory usage",
                              "HELP                - this message", NULL};

        ValkeyModule_ReplyWithArray(ctx, VALKEYMODULE_POSTPONED_ARRAY_LEN);
        int i = 0;
        for (; NULL != help[i]; i++) {
            ValkeyModule_ReplyWithStringBuffer(ctx, help[i], strlen(help[i]));
        }
        ValkeyModule_ReplySetArrayLength(ctx, i);

        return VALKEYMODULE_OK;
    } else {  // unknown subcommand
        ValkeyModule_ReplyWithError(ctx, "ERR unknown subcommand - try `JSON.DEBUG HELP`");
        return VALKEYMODULE_ERR;
    }
    return VALKEYMODULE_OK;  // this is never reached
}

/**
 * JSON.TYPE <key> [path]
 * Reports the type of JSON value at `path`.
 * `path` defaults to root if not provided. If the `key` or `path` do not exist, null is returned.
 * Reply: Simple string, specifically the type.
 */
int JSONType_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 2) || (argc > 3)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_ReplyWithNull(ctx);
        return VALKEYMODULE_OK;
    }
    if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (3 == argc ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        JSONPathNode_Free(jpn);
        return VALKEYMODULE_ERR;
    }

    // make the type-specifc reply, or deal with path errors
    if (E_OK == jpn->err) {
        ValkeyModule_ReplyWithSimpleString(ctx, NodeTypeStr(NODETYPE(jpn->n)));
    } else {
        // reply with null if there are **any** non-existing elements along the path
        ValkeyModule_ReplyWithNull(ctx);
    }

    JSONPathNode_Free(jpn);
    return VALKEYMODULE_OK;
}

/**
 * JSON.ARRLEN <key> [path]
 * JSON.OBJLEN <key> [path]
 * JSON.STRLEN <key> [path]
 * Report the length of the JSON value at `path` in `key`.
 *
 * `path` defaults to root if not provided. If the `key` or `path` do not exist, null is returned.
 *
 * Reply: Integer, specifically the length of the value.
 */
int JSONLen_GenericCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 2) || (argc > 3)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // the actual command
    const char *cmd = ValkeyModule_StringPtrLen(argv[0], NULL);

    // key must be empty or a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_ReplyWithNull(ctx);
        return VALKEYMODULE_OK;
    }
    if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (3 == argc ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // determine the type of target value based on command name
    NodeType expected, actual = NODETYPE(jpn->n);
    if (!strcasecmp("json.arrlen", cmd))
        expected = N_ARRAY;
    else if (!strcasecmp("json.objlen", cmd))
        expected = N_DICT;
    else  // must be json.strlen
        expected = N_STRING;

    // reply with the length per type, or with an error if the wrong type is encountered
    if (actual == expected) {
        ValkeyModule_ReplyWithLongLong(ctx, Node_Length(jpn->n));
    } else {
        ReplyWithPathTypeError(ctx, expected, actual);
        goto error;
    }

    JSONPathNode_Free(jpn);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.OBJKEYS <key> [path]
 * Return the keys in the object that's referenced by `path`.
 *
 * `path` defaults to root if not provided. If the object is empty, or either `key` or `path` do not
 * exist then null is returned.
 *
 * Reply: Array, specifically the key names as bulk strings.
 */
int JSONObjKeys_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 2) || (argc > 3)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_ReplyWithNull(ctx);
        return VALKEYMODULE_OK;
    }
    if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (3 == argc ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_NOINDEX == jpn->err || E_NOKEY == jpn->err) {
        // reply with null if there are **any** non-existing elements along the path
        ValkeyModule_ReplyWithNull(ctx);
        goto ok;
    } else if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // reply with the object's keys if it is a dictionary, error otherwise
    if (N_DICT == NODETYPE(jpn->n)) {
        int len = Node_Length(jpn->n);
        ValkeyModule_ReplyWithArray(ctx, len);
        for (int i = 0; i < len; i++) {
            // TODO: need an iterator for keys in dict
            const char *k = jpn->n->value.dictval.entries[i]->value.kvval.key;
            ValkeyModule_ReplyWithStringBuffer(ctx, k, strlen(k));
        }
    } else {
        ReplyWithPathTypeError(ctx, N_DICT, NODETYPE(jpn->n));
        goto error;
    }

ok:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.SET <key> <path> <json> [NX|XX]
 * Sets the JSON value at `path` in `key`
 *
 * For new Valkey keys the `path` must be the root. For existing keys, when the entire `path` exists,
 * the value that it contains is replaced with the `json` value.
 *
 * A key (with its respective value) is added to a JSON Object (in a Valkey JSON data type key) if
 * and only if it is the last child in the `path`. The optional subcommands modify this behavior for
 * both new Valkey JSON data type keys as well as JSON Object keys in them:
 *   `NX` - only set the key if it does not already exists
 *   `XX` - only set the key if it already exists
 *
 * Reply: Simple String `OK` if executed correctly, or Null Bulk if the specified `NX` or `XX`
 * conditions were not met.
 */
int JSONSet_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 4) || (argc > 5)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ | VALKEYMODULE_WRITE);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY != type && ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    JSONPathNode_t *jpn = NULL;
    Object *jo = NULL;
    char *jerr = NULL;

    // subcommand for key creation behavior modifiers NX and XX
    int subnx = 0, subxx = 0;
    if (argc > 4) {
        const char *subcmd = ValkeyModule_StringPtrLen(argv[4], NULL);
        if (!strcasecmp("nx", subcmd)) {
            subnx = 1;
        } else if (!strcasecmp("xx", subcmd)) {
            // new keys can be created only if the XX flag is off
            if (VALKEYMODULE_KEYTYPE_EMPTY == type) goto null;
            subxx = 1;
        } else {
            ValkeyModule_ReplyWithError(ctx, VKM_ERRORMSG_SYNTAX);
            return VALKEYMODULE_ERR;
        }
    }

    // JSON must be valid
    size_t jsonlen;
    const char *json = ValkeyModule_StringPtrLen(argv[3], &jsonlen);
    if (!jsonlen) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_EMPTY_STRING);
        return VALKEYMODULE_ERR;
    }

    // Create object from json
    if (JSONOBJECT_OK != CreateNodeFromJSON(JSONCtx.joctx, json, jsonlen, &jo, &jerr)) {
        if (jerr) {
            ValkeyModule_ReplyWithError(ctx, jerr);
            ValkeyModule_Free(jerr);
        } else {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_JSONOBJECT_ERROR);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_JSONOBJECT_ERROR);
        }
        return VALKEYMODULE_ERR;
    }

    // initialize or get JSON type container
    JSONType_t *jt = NULL;
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        jt = ValkeyModule_Calloc(1, sizeof(JSONType_t));
        jt->root = jo;
    } else {
        jt = ValkeyModule_ModuleTypeGetValue(key);
    }

    /* Validate path against the existing object root, and pretend that the new object is the root
     * if the key is empty. This will be caught immediately afterwards because new keys must be
     * created at the root.
     */
    if (PARSE_OK != NodeFromJSONPath(jt->root, argv[2], &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }
    int isRootPath = SearchPath_IsRootPath(&jpn->sp);

    // handle an empty key
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        // new keys must be created at the root
        if (E_OK != jpn->err || !isRootPath) {
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_NEW_NOT_ROOT);
            goto error;
        }
        ValkeyModule_ModuleTypeSetValue(key, JSONType, jt);
        goto ok;
    }

    // handle an existing key, first make sure there weren't any obvious path errors
    if (E_OK != jpn->err && E_NOKEY != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // verify that we're dealing with the last child in case of an object
    if (E_NOKEY == jpn->err && jpn->errlevel != jpn->sp.len - 1) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_PATH_NONTERMINAL_KEY);
        goto error;
    }

    // replace a value according to its container type
    if (E_OK == jpn->err) {
        NodeType ntp = NODETYPE(jpn->p);

        // an existing value in the root or an object can be replaced only if the NX is off
        if (subnx && (isRootPath || N_DICT == ntp)) {
            goto null;
        }

        // other containers, i.e. arrays, do not sport the NX or XX behavioral modification agents
        if (N_ARRAY == ntp && (subnx || subxx)) {
            ValkeyModule_ReplyWithError(ctx, VKM_ERRORMSG_SYNTAX);
            goto error;
        }

        if (isRootPath) {
            // replacing the root is easy
            ValkeyModule_DeleteKey(key);
            jt = ValkeyModule_Calloc(1, sizeof(JSONType_t));
            jt->root = jo;
            ValkeyModule_ModuleTypeSetValue(key, JSONType, jt);
        } else if (N_DICT == NODETYPE(jpn->p)) {
            if (OBJ_OK != Node_DictSet(jpn->p, jpn->sp.nodes[jpn->sp.len - 1].value.key, jo)) {
                VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_DICT_SET);
                ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_DICT_SET);
                goto error;
            }
        } else {  // must be an array
            int index = jpn->sp.nodes[jpn->sp.len - 1].value.index;
            if (index < 0) index = Node_Length(jpn->p) + index;
            if (OBJ_OK != Node_ArraySet(jpn->p, index, jo)) {
                VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_ARRAY_SET);
                ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_ARRAY_SET);
                goto error;
            }
            // unlike DictSet, ArraySet does not free so we need to call it explicitly
            Node_Free(jpn->n);
        }
    } else {  // must be E_NOKEY
        // new keys in the dictionary can be created only if the XX flag is off
        if (subxx) {
            goto null;
        }
        if (OBJ_OK != Node_DictSet(jpn->p, jpn->sp.nodes[jpn->sp.len - 1].value.key, jo)) {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_DICT_SET);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_DICT_SET);
            goto error;
        }
    }

ok:
    maybeClearPathCache(jt, jpn);
    ValkeyModule_ReplyWithSimpleString(ctx, "OK");
    JSONPathNode_Free(jpn);
    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

null:
    ValkeyModule_ReplyWithNull(ctx);
    JSONPathNode_Free(jpn);
    if (jo) Node_Free(jo);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    if (jt && VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_Free(jt);
    }
    if (jo) Node_Free(jo);
    return VALKEYMODULE_ERR;
}

static void maybeClearPathCache(JSONType_t *jt, const JSONPathNode_t *pn) {
    if (!jt->lruEntries) {
        return;
    }

    const char *pathStr = pn->spath;
    size_t pathLen = pn->spathlen;
    if (pn->sp.hasLeadingDot) {
        pathStr++;
        pathLen--;
    }

    if (pathLen == 0) {
        LruCache_ClearKey(VALKEYJSON_LRUCACHE_GLOBAL, jt);
    } else {
        LruCache_ClearValues(VALKEYJSON_LRUCACHE_GLOBAL, jt, pathStr, pathLen);
    }
}

static sds getSerializedJson(JSONType_t *jt, const JSONPathNode_t *pathInfo,
                             const JSONSerializeOpt *opts, int *wasFound, sds *target) {
    // printf("Requesting value for path %.*s\n", (int)pathLen, path);

    // Normalize the path. If the original path begins with a dot, strip it
    const char *pathStr = pathInfo->spath;
    size_t pathLen = pathInfo->spathlen;
    int shouldCache = 1;
    sds ret = NULL;

    if (pathInfo->sp.hasLeadingDot) {
        pathStr++;
        pathLen--;
    }

    if (pathInfo->n) {
        switch (pathInfo->n->type) {
            // Don't store trivial types in the cache - i.e. those which aren't
            // costly to serialize.
            case N_NULL:
            case N_BOOLEAN:
            case N_INTEGER:
            case N_NUMBER:
                shouldCache = 0;
                break;
            default:
                shouldCache = 1;
                break;
        }
    } else {
        shouldCache = 0;
    }

    if (shouldCache) {
        ret = LruCache_GetValue(VALKEYJSON_LRUCACHE_GLOBAL, jt, pathStr, pathLen);
    }
    if (ret) {
        *wasFound = 1;
        if (target) {
            *target = sdscatsds(*target, ret);
        }
        return ret;
    }

    // Otherwise, serialize
    if (target) {
        ret = *target;
    } else {
        ret = sdsempty();
    }
    SerializeNodeToJSON(pathInfo->n, opts, &ret);
    if (shouldCache) {
        LruCache_AddValue(VALKEYJSON_LRUCACHE_GLOBAL, jt, pathStr, pathLen, ret, sdslen(ret));
    }
    *wasFound = 0;
    if (target) {
        *target = ret;
    }
    return ret;
}

static int isCachableOptions(const JSONSerializeOpt *opts) {
    return (!opts->indentstr || *opts->indentstr == 0) &&
           (!opts->newlinestr || *opts->newlinestr == 0) &&
           (!opts->spacestr || *opts->spacestr) == 0 && (opts->noescape == 0);
}

static void sendSingleResponse(ValkeyModuleCtx *ctx, JSONType_t *jt, const JSONPathNode_t *pn,
                               const JSONSerializeOpt *options) {
    sds json = NULL;
    if (!isCachableOptions(options)) {
        json = sdsempty();
        SerializeNodeToJSON(pn->n, options, &json);
        ValkeyModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
        sdsfree(json);
        return;
    }

    int isFromCache = 0;
    json = getSerializedJson(jt, pn, options, &isFromCache, NULL);
    // Send the response now
    ValkeyModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
    if (!isFromCache) {
        sdsfree(json);
    }
}

static void sendMultiResponse(ValkeyModuleCtx *ctx, JSONType_t *jt, JSONPathNode_t **pns,
                              size_t npns, const JSONSerializeOpt *options) {
    sds json = NULL;
    if (!isCachableOptions(options)) {
        // Use legacy behavior
        json = sdsempty();
        Node *objReply = NewDictNode(npns);
        for (int i = 0; i < npns; i++) {
            // add the path to the reply only if it isn't there already
            Node *target;
            int ret = Node_DictGet(objReply, pns[i]->spath, &target);
            if (OBJ_ERR == ret) {
                Node_DictSet(objReply, pns[i]->spath, pns[i]->n);
            }
        }
        SerializeNodeToJSON(objReply, options, &json);
        ValkeyModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
        sdsfree(json);

        // avoid removing the actual data by resetting the reply dict
        // TODO: need a non-freeing Del
        for (int i = 0; i < objReply->value.dictval.len; i++) {
            objReply->value.dictval.entries[i]->value.kvval.val = NULL;
        }
        Node_Free(objReply);
        return;
    }

    json = sdsempty();
    json = sdscat(json, "{");
    for (int i = 0; i < npns; i++) {
        json = JSONSerialize_String(json, pns[i]->spath, pns[i]->spathlen, 1);
        json = sdscatlen(json, ":", 1);
        // Append to the buffer
        int dummy;
        getSerializedJson(jt, pns[i], options, &dummy, &json);
        if (i < npns - 1) {
            json = sdscat(json, ",");
        }
    }
    json = sdscat(json, "}");
    ValkeyModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
    sdsfree(json);
}

/**
 * JSON.GET <key> [INDENT indentation-string] [NEWLINE newline-string] [SPACE space-string]
 *                [path ...]
 * Return the value at `path` in JSON serialized form.
 *
 * This command accepts multiple `path`s, and defaults to the value's root when none are given.
 *
 * The following subcommands change the reply's and are all set to the empty string by default:
 *   - `INDENT` sets the indentation string for nested levels
 *   - `NEWLINE` sets the string that's printed at the end of each line
 *   - `SPACE` sets the string that's put between a key and a value
 *   - `NOESCAPE` Don't escape any JSON characters.
 *
 * Reply: Bulk String, specifically the JSON serialization.
 * The reply's structure depends on the on the number of paths. A single path results in the
 * value being itself is returned, whereas multiple paths are returned as a JSON object in which
 * each path is a key.
 */
int JSONGet_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if ((argc < 2)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key must be empty (reply with null) or an object type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_ReplyWithNull(ctx);
        return VALKEYMODULE_OK;
    } else if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // check for optional arguments
    int pathpos = 2;
    JSONSerializeOpt jsopt = {0};
    if (pathpos < argc) {
        VKMUtil_ParseArgsAfter("indent", argv, argc, "c", &jsopt.indentstr);
        if (jsopt.indentstr) {
            pathpos += 2;
        } else {
            jsopt.indentstr = "";
        }
    }
    if (pathpos < argc) {
        VKMUtil_ParseArgsAfter("newline", argv, argc, "c", &jsopt.newlinestr);
        if (jsopt.newlinestr) {
            pathpos += 2;
        } else {
            jsopt.newlinestr = "";
        }
    }
    if (pathpos < argc) {
        VKMUtil_ParseArgsAfter("space", argv, argc, "c", &jsopt.spacestr);
        if (jsopt.spacestr) {
            pathpos += 2;
        } else {
            jsopt.spacestr = "";
        }
    }

    if (VKMUtil_ArgExists("noescape", argv, argc, 2)) {
        jsopt.noescape = 1;
        pathpos++;
    }

    // validate paths, if none provided default to root
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    int npaths = argc - pathpos;
    int jpnslen = 0;
    JSONPathNode_t **jpns = ValkeyModule_Calloc(MAX(npaths, 1), sizeof(JSONPathNode_t *));
    if (!npaths) {  // default to root
        NodeFromJSONPath(jt->root, ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1), &jpns[0]);
        jpnslen = 1;
    } else {
        while (jpnslen < npaths) {
            // validate path correctness
            if (PARSE_OK != NodeFromJSONPath(jt->root, argv[pathpos + jpnslen], &jpns[jpnslen])) {
                ReplyWithSearchPathError(ctx, jpns[jpnslen]);
                jpnslen++;
                goto error;
            }

            // deal with path errors
            if (E_OK != jpns[jpnslen]->err) {
                ReplyWithPathError(ctx, jpns[jpnslen]);
                jpnslen++;
                goto error;
            }

            jpnslen++;
        }  // while (jpnslen < npaths)
    }

    // return the single path's JSON value, or wrap all paths-values as an object
    if (1 == jpnslen) {
        sendSingleResponse(ctx, jt, jpns[0], &jsopt);
    } else {
        sendMultiResponse(ctx, jt, jpns, jpnslen, &jsopt);
    }

    // check whether serialization had succeeded
    // if (!sdslen(json)) {
    //     VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_SERIALIZE);
    //     ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_SERIALIZE);
    //     goto error;
    // }

    for (int i = 0; i < jpnslen; i++) {
        JSONPathNode_Free(jpns[i]);
    }

    ValkeyModule_Free(jpns);
    return VALKEYMODULE_OK;

error:
    for (int i = 0; i < jpnslen; i++) {
        JSONPathNode_Free(jpns[i]);
    }
    ValkeyModule_Free(jpns);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.MGET <key> [<key> ...] <path>
 * Returns the values at `path` from multiple `key`s. Non-existing keys and non-existing paths
 * are reported as null. Reply: Array of Bulk Strings, specifically the JSON serialization of
 * the value at each key's path.
 */
int JSONMGet_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if ((argc < 2)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    if (ValkeyModule_IsKeysPositionRequest(ctx)) {
        for (int i = 1; i < argc - 1; i++) ValkeyModule_KeyAtPos(ctx, i);
        return VALKEYMODULE_OK;
    }
    ValkeyModule_AutoMemory(ctx);

    // validate search path
    size_t spathlen;
    const char *spath = ValkeyModule_StringPtrLen(argv[argc - 1], &spathlen);
    JSONPathNode_t jpn = {0};
    JSONSearchPathError_t jsperr = {0};
    jpn.sp = NewSearchPath(0);
    if (PARSE_ERR == ParseJSONPath(spath, spathlen, &jpn.sp, &jsperr)) {
        jpn.sperrmsg = jsperr.errmsg;
        jpn.sperroffset = jsperr.offset;
        ReplyWithSearchPathError(ctx, &jpn);
        goto error;
    }

    // iterate keys
    ValkeyModule_ReplyWithArray(ctx, argc - 2);
    int isRootPath = SearchPath_IsRootPath(&jpn.sp);
    JSONSerializeOpt jsopt = {0};
    for (int i = 1; i < argc - 1; i++) {
        ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[i], VALKEYMODULE_READ);

        // key must an object type, empties and others return null like Valkey' MGET
        int type = ValkeyModule_KeyType(key);
        if (VALKEYMODULE_KEYTYPE_EMPTY == type) goto null;
        if (ValkeyModule_ModuleTypeGetType(key) != JSONType) goto null;

        // follow the path to the target node in the key
        JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
        if (isRootPath) {
            jpn.err = E_OK;
            jpn.n = jt->root;
        } else {
            jpn.err = SearchPath_FindEx(&jpn.sp, jt->root, &jpn.n, &jpn.p, &jpn.errlevel);
        }

        // deal with path errors by returning null
        if (E_OK != jpn.err) goto null;

        // serialize it
        sds json = sdsempty();
        SerializeNodeToJSON(jpn.n, &jsopt, &json);

        // check whether serialization had succeeded
        if (!sdslen(json)) {
            sdsfree(json);
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_SERIALIZE);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_SERIALIZE);
            goto error;
        }

        // add the serialization of object for that key's path
        ValkeyModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
        sdsfree(json);
        continue;

    null:  // reply with null for keys that the path mismatches
        ValkeyModule_ReplyWithNull(ctx);
    }

    SearchPath_Free(&jpn.sp);
    return VALKEYMODULE_OK;

error:
    SearchPath_Free(&jpn.sp);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.DEL <key> [path]
 * Delete a value.
 *
 * `path` defaults to root if not provided. Non-existing keys as well as non-existing paths are
 * ignored. Deleting an object's root is equivalent to deleting the key from Valkey.
 *
 * Reply: Integer, specifically the number of paths deleted (0 or 1).
 */
int JSONDel_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 2) || (argc > 3)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ | VALKEYMODULE_WRITE);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_ReplyWithLongLong(ctx, 0);
        return VALKEYMODULE_OK;
    } else if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (3 == argc ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_NOINDEX == jpn->err || E_NOKEY == jpn->err) {
        // reply with 0 if there are **any** non-existing elements along the path
        ValkeyModule_ReplyWithLongLong(ctx, 0);
        goto ok;
    } else if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // Delete from the LRU cache, if needed
    maybeClearPathCache(jt, jpn);

    // if it is the root then delete the key, otherwise delete the target from parent container
    if (SearchPath_IsRootPath(&jpn->sp)) {
        ValkeyModule_DeleteKey(key);
    } else if (N_DICT == NODETYPE(jpn->p)) {  // delete from a dict
        const char *dictkey = jpn->sp.nodes[jpn->sp.len - 1].value.key;
        if (OBJ_OK != Node_DictDel(jpn->p, dictkey)) {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_DICT_DEL);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_DICT_DEL);
            goto error;
        }
    } else {  // container must be an array
        int index = jpn->sp.nodes[jpn->sp.len - 1].value.index;
        if (OBJ_OK != Node_ArrayDelRange(jpn->p, index, 1)) {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_ARRAY_DEL);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_ARRAY_DEL);
            goto error;
        }
    }  // if (N_DICT)

    ValkeyModule_ReplyWithLongLong(ctx, (long long)argc - 2);

ok:
    JSONPathNode_Free(jpn);
    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.NUMINCRBY <key> [path] <value>
 * JSON.NUMMULTBY <key> [path] <value>
 * Increments/multiplies the value stored under `path` by `value`.
 * `path` must exist path and must be a number value.
 * Reply: String, specifically the resulting JSON number value
 */
int JSONNum_GenericCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    if ((argc < 3) || (argc > 4)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    const char *cmd = ValkeyModule_StringPtrLen(argv[0], NULL);
    double oval, bval, rz;  // original value, by value and the result
    Object *joval = NULL;   // the by value as a JSON object

    // key must be an object type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ | VALKEYMODULE_WRITE);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_KEY_REQUIRED);
        return VALKEYMODULE_ERR;
    } else if (ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (4 == argc ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // verify that the target value is a number
    if (N_INTEGER != NODETYPE(jpn->n) && N_NUMBER != NODETYPE(jpn->n)) {
        sds err = sdscatfmt(sdsempty(), VALKEYJSON_ERROR_PATH_NANTYPE, NodeTypeStr(NODETYPE(jpn->n)));
        ValkeyModule_ReplyWithError(ctx, err);
        sdsfree(err);
        goto error;
    }
    oval = NODEVALUE_AS_DOUBLE(jpn->n);

    // we use the json parser to convert the bval arg into a value to catch all of JSON's
    // syntices
    size_t vallen;
    const char *val = ValkeyModule_StringPtrLen(argv[(4 == argc ? 3 : 2)], &vallen);
    char *jerr = NULL;
    if (JSONOBJECT_OK != CreateNodeFromJSON(JSONCtx.joctx, val, vallen, &joval, &jerr)) {
        if (jerr) {
            ValkeyModule_ReplyWithError(ctx, jerr);
            ValkeyModule_Free(jerr);
        } else {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_JSONOBJECT_ERROR);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_JSONOBJECT_ERROR);
        }
        goto error;
    }

    // the by value must be a number
    if (N_INTEGER != NODETYPE(joval) && N_NUMBER != NODETYPE(joval)) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_VALUE_NAN);
        goto error;
    }
    bval = NODEVALUE_AS_DOUBLE(joval);

    // perform the operation
    if (!strcasecmp("json.numincrby", cmd)) {
        rz = oval + bval;
    } else {
        rz = oval * bval;
    }

    // check that the result is valid
    if (isnan(rz) || isinf(rz)) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_RESULT_NAN_OR_INF);
        goto error;
    }

    // make an object out of the result per its type
    Object *orz;
    // the result is an integer only if both values were, and providing an int64 can hold it
    if (N_INTEGER == NODETYPE(jpn->n) && N_INTEGER == NODETYPE(joval) && rz <= (double)INT64_MAX &&
        rz >= (double)INT64_MIN) {
        orz = NewIntNode((int64_t)rz);
    } else {
        orz = NewDoubleNode(rz);
    }

    // replace the original value with the result depending on the parent container's type
    if (SearchPath_IsRootPath(&jpn->sp)) {
        ValkeyModule_DeleteKey(key);
        jt = ValkeyModule_Calloc(1, sizeof(JSONType_t));
        jt->root = orz;
        ValkeyModule_ModuleTypeSetValue(key, JSONType, jt);
    } else if (N_DICT == NODETYPE(jpn->p)) {
        if (OBJ_OK != Node_DictSet(jpn->p, jpn->sp.nodes[jpn->sp.len - 1].value.key, orz)) {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_DICT_SET);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_DICT_SET);
            goto error;
        }
    } else {  // container must be an array
        int index = jpn->sp.nodes[jpn->sp.len - 1].value.index;
        if (index < 0) index = Node_Length(jpn->p) + index;
        if (OBJ_OK != Node_ArraySet(jpn->p, index, orz)) {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_ARRAY_SET);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_ARRAY_SET);
            goto error;
        }
        // unlike DictSet, ArraySet does not free so we need to call it explicitly
        Node_Free(jpn->n);
    }
    jpn->n = orz;

    // reply with the serialization of the new value
    JSONSerializeOpt jsopt = {0};
    sds json = sdsempty();
    SerializeNodeToJSON(jpn->n, &jsopt, &json);
    ValkeyModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
    maybeClearPathCache(jt, jpn);

    sdsfree(json);

    Node_Free(joval);
    JSONPathNode_Free(jpn);

    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

error:
    Node_Free(joval);
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.STRAPPEND <key> [path] <json-string>
 * Append the `json-string` value(s) the string at `path`.
 * `path` defaults to root if not provided.
 * Reply: Integer, specifically the string's new length.
 */
int JSONStrAppend_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 3) || (argc > 4)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ | VALKEYMODULE_WRITE);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type || ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (4 == argc ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // the target must be a string
    if (N_STRING != NODETYPE(jpn->n)) {
        ReplyWithPathTypeError(ctx, N_STRING, NODETYPE(jpn->n));
        goto error;
    }

    // JSON must be valid
    size_t jsonlen;
    const char *json = ValkeyModule_StringPtrLen(argv[(4 == argc ? 3 : 2)], &jsonlen);
    if (!jsonlen) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_EMPTY_STRING);
        goto error;
    }

    // make an object from the JSON value
    Object *jo = NULL;
    char *jerr = NULL;
    if (JSONOBJECT_OK != CreateNodeFromJSON(JSONCtx.joctx, json, jsonlen, &jo, &jerr)) {
        if (jerr) {
            ValkeyModule_ReplyWithError(ctx, jerr);
            ValkeyModule_Free(jerr);
        } else {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_JSONOBJECT_ERROR);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_JSONOBJECT_ERROR);
        }
        goto error;
    }

    // the value must be a string
    if (N_STRING != NODETYPE(jo)) {
        sds err = sdscatfmt(sdsempty(), "ERR wrong type of value - expected %s but found %s",
                            NodeTypeStr(N_STRING), NodeTypeStr(NODETYPE(jpn->n)));
        ValkeyModule_ReplyWithError(ctx, err);
        sdsfree(err);
    }

    // actually concatenate the strings
    Node_StringAppend(jpn->n, jo);
    ValkeyModule_ReplyWithLongLong(ctx, (long long)Node_Length(jpn->n));
    Node_Free(jo);
    JSONPathNode_Free(jpn);

    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.ARRINSERT <key> <path> <index> <json> [<json> ...]
 * Insert the `json` value(s) into the array at `path` before the `index` (shifts to the right).
 *
 * The index must be in the array's range. Inserting at `index` 0 prepends to the array.
 * Negative index values are interpreted as starting from the end.
 *
 * Reply: Integer, specifically the array's new size
 */
int JSONArrInsert_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if (argc < 5) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ | VALKEYMODULE_WRITE);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type || ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    if (PARSE_OK != NodeFromJSONPath(jt->root, argv[2], &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // the target must be an array
    if (N_ARRAY != NODETYPE(jpn->n)) {
        ReplyWithPathTypeError(ctx, N_ARRAY, NODETYPE(jpn->n));
        goto error;
    }

    // get the index
    long long index;
    if (VALKEYMODULE_OK != ValkeyModule_StringToLongLong(argv[3], &index)) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INDEX_INVALID);
        goto error;
    }

    // convert negative values
    if (index < 0) index = Node_Length(jpn->n) + index;

    // check for out of range
    if (index < 0 || index > Node_Length(jpn->n)) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INDEX_OUTOFRANGE);
        goto error;
    }

    // make an array from the JSON values
    Node *sub = NewArrayNode(argc - 4);
    for (int i = 4; i < argc; i++) {
        // JSON must be valid
        size_t jsonlen;
        const char *json = ValkeyModule_StringPtrLen(argv[i], &jsonlen);
        if (!jsonlen) {
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_EMPTY_STRING);
            Node_Free(sub);
            goto error;
        }

        // create object from json
        Object *jo = NULL;
        char *jerr = NULL;
        if (JSONOBJECT_OK != CreateNodeFromJSON(JSONCtx.joctx, json, jsonlen, &jo, &jerr)) {
            Node_Free(sub);
            if (jerr) {
                ValkeyModule_ReplyWithError(ctx, jerr);
                ValkeyModule_Free(jerr);
            } else {
                VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_JSONOBJECT_ERROR);
                ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_JSONOBJECT_ERROR);
            }
            goto error;
        }

        // append it to the sub array
        if (OBJ_OK != Node_ArrayAppend(sub, jo)) {
            Node_Free(jo);
            Node_Free(sub);
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_INSERT_SUBARRY);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INSERT_SUBARRY);
            goto error;
        }
    }

    // insert the sub array to the target array
    if (OBJ_OK != Node_ArrayInsert(jpn->n, index, sub)) {
        Node_Free(sub);
        VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_INSERT);
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INSERT);
        goto error;
    }

    ValkeyModule_ReplyWithLongLong(ctx, Node_Length(jpn->n));
    maybeClearPathCache(jt, jpn);
    JSONPathNode_Free(jpn);
    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/* JSON.ARRAPPEND <key> <path> <json> [<json> ...]
 * Append the `json` value(s) into the array at `path` after the last element in it.
 * Reply: Integer, specifically the array's new size
 */
int JSONArrAppend_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if (argc < 4) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ | VALKEYMODULE_WRITE);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type || ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    if (PARSE_OK != NodeFromJSONPath(jt->root, argv[2], &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // the target must be an array
    if (N_ARRAY != NODETYPE(jpn->n)) {
        ReplyWithPathTypeError(ctx, N_ARRAY, NODETYPE(jpn->n));
        goto error;
    }

    // make an array from the JSON values
    Node *sub = NewArrayNode(argc - 3);
    for (int i = 3; i < argc; i++) {
        // JSON must be valid
        size_t jsonlen;
        const char *json = ValkeyModule_StringPtrLen(argv[i], &jsonlen);
        if (!jsonlen) {
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_EMPTY_STRING);
            Node_Free(sub);
            goto error;
        }

        // create object from json
        Object *jo = NULL;
        char *jerr = NULL;
        if (JSONOBJECT_OK != CreateNodeFromJSON(JSONCtx.joctx, json, jsonlen, &jo, &jerr)) {
            Node_Free(sub);
            if (jerr) {
                ValkeyModule_ReplyWithError(ctx, jerr);
                ValkeyModule_Free(jerr);
            } else {
                VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_JSONOBJECT_ERROR);
                ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_JSONOBJECT_ERROR);
            }
            goto error;
        }

        // append it to the sub array
        if (OBJ_OK != Node_ArrayAppend(sub, jo)) {
            Node_Free(jo);
            Node_Free(sub);
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_INSERT_SUBARRY);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INSERT_SUBARRY);
            goto error;
        }
    }

    // insert the sub array to the target array
    if (OBJ_OK != Node_ArrayInsert(jpn->n, Node_Length(jpn->n), sub)) {
        Node_Free(sub);
        VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_INSERT);
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INSERT);
        goto error;
    }

    ValkeyModule_ReplyWithLongLong(ctx, Node_Length(jpn->n));
    maybeClearPathCache(jt, jpn);
    JSONPathNode_Free(jpn);
    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.ARRINDEX <key> <path> <scalar> [start [stop]]
 * Search for the first occurance of a scalar JSON value in an array.
 *
 * The optional inclusive `start` (default 0) and exclusive `stop` (default 0, meaning that the
 * last element is included) specify a slice of the array to search.
 *
 * Note: out of range errors are treated by rounding the index to the array's start and end. An
 * inverse index range (e.g, from 1 to 0) will return unfound.
 *
 * Reply: Integer, specifically the position of the scalar value in the array or -1 if unfound.
 */
int JSONArrIndex_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 4) || (argc > 6)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type || ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    Object *jo = NULL;
    if (PARSE_OK != NodeFromJSONPath(jt->root, argv[2], &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // verify that the target's type is an array
    if (N_ARRAY != NODETYPE(jpn->n)) {
        ReplyWithPathTypeError(ctx, N_ARRAY, NODETYPE(jpn->n));
        goto error;
    }

    // the JSON value to search for must be valid
    size_t jsonlen;
    const char *json = ValkeyModule_StringPtrLen(argv[3], &jsonlen);
    if (!jsonlen) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_EMPTY_STRING);
        goto error;
    }

    // create an object from json
    char *jerr = NULL;
    if (JSONOBJECT_OK != CreateNodeFromJSON(JSONCtx.joctx, json, jsonlen, &jo, &jerr)) {
        if (jerr) {
            ValkeyModule_ReplyWithError(ctx, jerr);
            ValkeyModule_Free(jerr);
        } else {
            VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_JSONOBJECT_ERROR);
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_JSONOBJECT_ERROR);
        }
        goto error;
    }

    // get start (inclusive) & stop (exlusive) indices
    long long start = 0, stop = 0;
    if (argc > 4) {
        if (VALKEYMODULE_OK != ValkeyModule_StringToLongLong(argv[4], &start)) {
            ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INDEX_INVALID);
            goto error;
        }
        if (argc > 5) {
            if (VALKEYMODULE_OK != ValkeyModule_StringToLongLong(argv[5], &stop)) {
                ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INDEX_INVALID);
                goto error;
            }
        }
    }

    ValkeyModule_ReplyWithLongLong(ctx, Node_ArrayIndex(jpn->n, jo, (int)start, (int)stop));

    JSONPathNode_Free(jpn);
    Node_Free(jo);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    if (jo) Node_Free(jo);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.ARRPOP <key> [path [index]]
 * Remove and return element from the index in the array.
 *
 * `path` defaults to root if not provided. `index` is the position in the array to start
 * popping from (defaults to -1, meaning the last element). Out of range indices are rounded to
 * their respective array ends. Popping an empty array yields null.
 *
 * Reply: Bulk String, specifically the popped JSON value.
 */
int JSONArrPop_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if ((argc < 2) || (argc > 4)) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type || ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    ValkeyModuleString *spath =
        (argc > 2 ? argv[2] : ValkeyModule_CreateString(ctx, OBJECT_ROOT_PATH, 1));
    if (PARSE_OK != NodeFromJSONPath(jt->root, spath, &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // verify that the target's type is an array
    if (N_ARRAY != NODETYPE(jpn->n)) {
        ReplyWithPathTypeError(ctx, N_ARRAY, NODETYPE(jpn->n));
        goto error;
    }

    // nothing to do
    long long len = Node_Length(jpn->n);
    if (!len) {
        ValkeyModule_ReplyWithNull(ctx);
        goto ok;
    }

    // get the index
    long long index = -1;
    if (argc > 3 && VALKEYMODULE_OK != ValkeyModule_StringToLongLong(argv[3], &index)) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INDEX_INVALID);
        goto error;
    }

    // convert negative index
    if (index < 0) index = len + index;
    if (index < 0) index = 0;
    if (index >= len) index = len - 1;

    // get and serialize the popped array item
    JSONSerializeOpt jsopt = {0};
    sds json = sdsempty();
    Node *item;
    Node_ArrayItem(jpn->n, index, &item);
    SerializeNodeToJSON(item, &jsopt, &json);

    // check whether serialization had succeeded
    if (!sdslen(json)) {
        sdsfree(json);
        VKM_LOG_WARNING(ctx, "%s", VALKEYJSON_ERROR_SERIALIZE);
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_SERIALIZE);
        goto error;
    }

    // delete the item from the array
    Node_ArrayDelRange(jpn->n, index, 1);

    // reply with the serialization
    ValkeyModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
    sdsfree(json);

ok:
    JSONPathNode_Free(jpn);
    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

/**
 * JSON.ARRTRIM <key> <path> <start> <stop>
 * Trim an array so that it contains only the specified inclusive range of elements.
 *
 * This command is extremely forgiving and using it with out of range indexes will not produce
 * an error. If `start` is larger than the array's size or `start` > `stop`, the result will be
 * an empty array. If `start` is < 0 then it will be treated as 0. If end is larger than the end
 * of the array, it will be treated like the last element in it.
 *
 * Reply: Integer, specifically the array's new size.
 */
int JSONArrTrim_ValkeyCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // check args
    if (argc != 5) {
        ValkeyModule_WrongArity(ctx);
        return VALKEYMODULE_ERR;
    }
    ValkeyModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    ValkeyModuleKey *key = ValkeyModule_OpenKey(ctx, argv[1], VALKEYMODULE_READ);
    int type = ValkeyModule_KeyType(key);
    if (VALKEYMODULE_KEYTYPE_EMPTY == type || ValkeyModule_ModuleTypeGetType(key) != JSONType) {
        ValkeyModule_ReplyWithError(ctx, VALKEYMODULE_ERRORMSG_WRONGTYPE);
        return VALKEYMODULE_ERR;
    }

    // validate path
    JSONType_t *jt = ValkeyModule_ModuleTypeGetValue(key);
    JSONPathNode_t *jpn = NULL;
    if (PARSE_OK != NodeFromJSONPath(jt->root, argv[2], &jpn)) {
        ReplyWithSearchPathError(ctx, jpn);
        goto error;
    }

    // deal with path errors
    if (E_OK != jpn->err) {
        ReplyWithPathError(ctx, jpn);
        goto error;
    }

    // verify that the target's type is an array
    if (N_ARRAY != NODETYPE(jpn->n)) {
        ReplyWithPathTypeError(ctx, N_ARRAY, NODETYPE(jpn->n));
        goto error;
    }

    // get start & stop
    long long start, stop, left, right;
    long long len = (long long)Node_Length(jpn->n);
    if (VALKEYMODULE_OK != ValkeyModule_StringToLongLong(argv[3], &start)) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INDEX_INVALID);
        goto error;
    }
    if (VALKEYMODULE_OK != ValkeyModule_StringToLongLong(argv[4], &stop)) {
        ValkeyModule_ReplyWithError(ctx, VALKEYJSON_ERROR_INDEX_INVALID);
        goto error;
    }

    // convert negative indexes
    if (start < 0) start = len + start;
    if (stop < 0) stop = len + stop;

    if (start < 0) start = 0;            // start at the beginning
    if (start > stop || start >= len) {  // empty the array
        left = len;
        right = 0;
    } else {  // set the boundries
        left = start;
        if (stop >= len) stop = len - 1;
        right = len - stop - 1;
    }

    // trim the array
    Node_ArrayDelRange(jpn->n, 0, left);
    Node_ArrayDelRange(jpn->n, -right, right);

    ValkeyModule_ReplyWithLongLong(ctx, (long long)Node_Length(jpn->n));
    maybeClearPathCache(jt, jpn);
    JSONPathNode_Free(jpn);
    ValkeyModule_ReplicateVerbatim(ctx);
    return VALKEYMODULE_OK;

error:
    JSONPathNode_Free(jpn);
    return VALKEYMODULE_ERR;
}

int JSONCacheInfoCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // Just dump and return the Cache Info
    ValkeyModule_ReplyWithArray(ctx, VALKEYMODULE_POSTPONED_ARRAY_LEN);
    size_t numElems = 0;

    ValkeyModule_ReplyWithSimpleString(ctx, "bytes");
    ValkeyModule_ReplyWithLongLong(ctx, VALKEYJSON_LRUCACHE_GLOBAL->numBytes);
    numElems += 2;

    ValkeyModule_ReplyWithSimpleString(ctx, "items");
    ValkeyModule_ReplyWithLongLong(ctx, VALKEYJSON_LRUCACHE_GLOBAL->numEntries);
    numElems += 2;

    ValkeyModule_ReplyWithSimpleString(ctx, "max_bytes");
    ValkeyModule_ReplyWithLongLong(ctx, VALKEYJSON_LRUCACHE_GLOBAL->maxBytes);
    numElems += 2;

    ValkeyModule_ReplyWithSimpleString(ctx, "max_entries");
    ValkeyModule_ReplyWithLongLong(ctx, VALKEYJSON_LRUCACHE_GLOBAL->maxEntries);
    numElems += 2;

    ValkeyModule_ReplyWithSimpleString(ctx, "min_size");
    ValkeyModule_ReplyWithLongLong(ctx, VALKEYJSON_LRUCACHE_GLOBAL->minSize);

    numElems += 2;
    ValkeyModule_ReplySetArrayLength(ctx, numElems);
    return VALKEYMODULE_OK;
}

int JSONCacheInitCommand(ValkeyModuleCtx *ctx, ValkeyModuleString **argv, int argc) {
    // Initialize the cache settings. This is temporary and used for tests
    long long maxByte = LRUCACHE_DEFAULT_MAXBYTE, maxEnt = LRUCACHE_DEFAULT_MAXENT,
              minSize = LRUCACHE_DEFAULT_MINSIZE;
    if (argc == 4) {
        if (VKMUtil_ParseArgs(argv, argc, 1, "lll", &maxByte, &maxEnt, &minSize) != VALKEYMODULE_OK) {
            return ValkeyModule_ReplyWithError(ctx, "Bad arguments");
        }
    } else if (argc != 1) {
        return ValkeyModule_ReplyWithError(ctx, "USAGE: [MAXBYTES, MAXENTS, MINSIZE]");
    }

    VALKEYJSON_LRUCACHE_GLOBAL->maxBytes = maxByte;
    VALKEYJSON_LRUCACHE_GLOBAL->maxEntries = maxEnt;
    VALKEYJSON_LRUCACHE_GLOBAL->minSize = minSize;
    return ValkeyModule_ReplyWithSimpleString(ctx, "OK");
}

/* Creates the module's commands. */
int Module_CreateCommands(ValkeyModuleCtx *ctx) {
    /* Generic JSON type commands. */
    if (ValkeyModule_CreateCommand(ctx, "json.resp", JSONResp_ValkeyCommand, "readonly", 1, 1, 1) ==
        VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.debug", JSONDebug_ValkeyCommand, "readonly getkeys-api",
                                  1, 1, 1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.type", JSONType_ValkeyCommand, "readonly", 1, 1, 1) ==
        VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.set", JSONSet_ValkeyCommand, "write deny-oom", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.get", JSONGet_ValkeyCommand, "readonly", 1, 1, 1) ==
        VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.mget", JSONMGet_ValkeyCommand, "readonly getkeys-api",
                                  1, 1, 1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.del", JSONDel_ValkeyCommand, "write", 1, 1, 1) ==
        VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.forget", JSONDel_ValkeyCommand, "write", 1, 1, 1) ==
        VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    /* JSON number commands. */
    if (ValkeyModule_CreateCommand(ctx, "json.numincrby", JSONNum_GenericCommand, "write", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.nummultby", JSONNum_GenericCommand, "write", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    /* JSON string commands. */
    if (ValkeyModule_CreateCommand(ctx, "json.strlen", JSONLen_GenericCommand, "readonly", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.strappend", JSONStrAppend_ValkeyCommand,
                                  "write deny-oom", 1, 1, 1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    /* JSON array commands matey. */
    if (ValkeyModule_CreateCommand(ctx, "json.arrlen", JSONLen_GenericCommand, "readonly", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.arrinsert", JSONArrInsert_ValkeyCommand,
                                  "write deny-oom", 1, 1, 1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.arrappend", JSONArrAppend_ValkeyCommand,
                                  "write deny-oom", 1, 1, 1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.arrindex", JSONArrIndex_ValkeyCommand, "readonly", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.arrpop", JSONArrPop_ValkeyCommand, "write", 1, 1, 1) ==
        VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.arrtrim", JSONArrTrim_ValkeyCommand, "write", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    /* JSON object commands. */
    if (ValkeyModule_CreateCommand(ctx, "json.objlen", JSONLen_GenericCommand, "readonly", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json.objkeys", JSONObjKeys_ValkeyCommand, "readonly", 1, 1,
                                  1) == VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    if (ValkeyModule_CreateCommand(ctx, "json._cacheinfo", JSONCacheInfoCommand, "readonly", 1, 1,
                                  1) == VALKEYMODULE_ERR) {
        return VALKEYMODULE_ERR;
    }
    if (ValkeyModule_CreateCommand(ctx, "json._cacheinit", JSONCacheInitCommand, "write", 1, 1, 1) ==
        VALKEYMODULE_ERR) {
        return VALKEYMODULE_ERR;
    }

    return VALKEYMODULE_OK;
}

int ValkeyModule_OnLoad(ValkeyModuleCtx *ctx) {
    // Register the module
    if (ValkeyModule_Init(ctx, VKMODULE_NAME, VALKEYJSON_MODULE_VERSION, VALKEYMODULE_APIVER_1) ==
        VALKEYMODULE_ERR)
        return VALKEYMODULE_ERR;

    // Register the JSON data type
    ValkeyModuleTypeMethods tm = {.version = VALKEYMODULE_TYPE_METHOD_VERSION,
                                 .rdb_load = JSONTypeRdbLoad,
                                 .rdb_save = JSONTypeRdbSave,
                                 .aof_rewrite = JSONTypeAofRewrite,
                                 .mem_usage = JSONTypeMemoryUsage,
                                 .free = JSONTypeFree};
    JSONType = ValkeyModule_CreateDataType(ctx, JSONTYPE_NAME, JSONTYPE_ENCODING_VERSION, &tm);
    if (NULL == JSONType) return VALKEYMODULE_ERR;

    // Initialize the module's context
    JSONCtx = (ModuleCtx){0};
    JSONCtx.joctx = NewJSONObjectCtx(0);

    // Create the commands
    if (VALKEYMODULE_ERR == Module_CreateCommands(ctx)) return VALKEYMODULE_ERR;

    VKM_LOG_WARNING(ctx, "%s v%d.%d.%d [encver %d]", VKMODULE_DESC, VALKEYJSON_VERSION_MAJOR,
                    VALKEYJSON_VERSION_MINOR, VALKEYJSON_VERSION_PATCH, JSONTYPE_ENCODING_VERSION);

    return VALKEYMODULE_OK;
}
