/*
* Copyright (C) 2016-2017 Redis Labs
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

#include "object_type.h"

#define Vector_Last(v) Vector_Size(v) - 1

void *ObjectTypeRdbLoad(ValkeyModuleIO *rdb) {
    // IMPORTANT: no encoding version check here, this is up to the calller
    Vector *nodes = NULL;
    Vector *indices = NULL;
    Node *node = NULL;
    uint64_t len = 0;
    NodeType type = 0;
    size_t strlen = 0;
    char *str = NULL;
    enum { S_INIT, S_BEGIN_VALUE, S_END_VALUE, S_CONTAINER, S_END } state = S_INIT;

    while (S_END != state) {
        switch (state) {
            case S_INIT:  // Initial state
                nodes = NewVector(Node *, 1);
                indices = NewVector(uint64_t, 1);
                type = (NodeType)ValkeyModule_LoadUnsigned(rdb);
                state = S_BEGIN_VALUE;
                break;
            case S_BEGIN_VALUE:
                switch (type) {
                    case N_NULL:
                        node = NULL;
                        state = S_END_VALUE;
                        break;
                    case N_BOOLEAN:
                        str = ValkeyModule_LoadStringBuffer(rdb, &strlen);
                        node = NewBoolNode('1' == str[0]);
                        ValkeyModule_Free(str);
                        state = S_END_VALUE;
                        break;
                    case N_INTEGER:
                        node = NewIntNode(ValkeyModule_LoadSigned(rdb));
                        state = S_END_VALUE;
                        break;
                    case N_NUMBER:
                        node = NewDoubleNode(ValkeyModule_LoadDouble(rdb));
                        state = S_END_VALUE;
                        break;
                    case N_STRING:
                        str = ValkeyModule_LoadStringBuffer(rdb, &strlen);
                        node = NewStringNode(str, strlen);
                        ValkeyModule_Free(str);
                        state = S_END_VALUE;
                        break;
                    case N_KEYVAL:
                        str = ValkeyModule_LoadStringBuffer(rdb, &strlen);
                        node = NewKeyValNode(str, strlen, NULL);
                        Vector_Push(nodes, node);
                        Vector_Push(indices, (uint64_t)1);
                        ValkeyModule_Free(str);
                        state = S_CONTAINER;
                        break;
                    case N_DICT:
                        len = ValkeyModule_LoadUnsigned(rdb);
                        node = NewDictNode(len);
                        Vector_Push(nodes, node);
                        Vector_Push(indices, len);
                        state = S_CONTAINER;
                        break;
                    case N_ARRAY:
                        len = ValkeyModule_LoadUnsigned(rdb);
                        node = NewArrayNode(len);
                        Vector_Push(nodes, node);
                        Vector_Push(indices, len);
                        state = S_CONTAINER;
                        break;
                }  // switch (type)
                break;
            case S_END_VALUE:
                if (Vector_Size(nodes)) {  // in case the new node has a parent
                    Node *container;
                    Vector_Get(nodes, Vector_Last(nodes), &container);
                    switch (container->type) {  // add it
                        case N_KEYVAL:
                            container->value.kvval.val = node;
                            break;
                        case N_DICT:
                            Node_DictSetKeyVal(container, node);
                            break;
                        case N_ARRAY:
                            Node_ArrayAppend(container, node);
                        default:
                            break;
                    }
                    state = S_CONTAINER;
                } else {
                    state = S_END;
                }
                break;
            case S_CONTAINER:
                Vector_Get(indices, Vector_Last(indices), &len);
                if (len) {  // move to next child node
                    Vector_Put(indices, Vector_Last(indices), len - 1);
                    type = (NodeType)ValkeyModule_LoadUnsigned(rdb);
                    state = S_BEGIN_VALUE;
                } else {
                    Vector_Pop(indices, NULL);
                    Vector_Pop(nodes, &node);
                    state = S_END_VALUE;
                }
                break;
            case S_END:  // keeps the compiler from complaining
                break;
        }  // switch (state)
    }      //    while (S_END != state)

    Vector_Free(indices);
    Vector_Free(nodes);
    return (void *)node;
}

void _ObjectTypeSave_Begin(Node *n, void *ctx) {
    ValkeyModuleIO *rdb = (ValkeyModuleIO *)ctx;

    // type is saved as uint64, but could be compressed to 1-2 bytes.
    if (!n) {
        ValkeyModule_SaveUnsigned(rdb, N_NULL);
    } else {
        ValkeyModule_SaveUnsigned(rdb, n->type);
        switch (n->type) {
            case N_BOOLEAN:
                ValkeyModule_SaveStringBuffer(rdb, n->value.boolval ? "1" : "0", 1);
                break;
            case N_INTEGER:
                ValkeyModule_SaveSigned(rdb, n->value.intval);
                break;
            case N_NUMBER:
                ValkeyModule_SaveDouble(rdb, n->value.numval);
                break;
            case N_STRING:
                ValkeyModule_SaveStringBuffer(rdb, n->value.strval.data, n->value.strval.len);
                break;
            case N_KEYVAL:
                ValkeyModule_SaveStringBuffer(rdb, n->value.kvval.key, strlen(n->value.kvval.key));
                break;
            case N_DICT:
                ValkeyModule_SaveUnsigned(rdb, n->value.dictval.len);
                break;
            case N_ARRAY:
                ValkeyModule_SaveUnsigned(rdb, n->value.arrval.len);
                break;
            case N_NULL:  // keeps the compiler from complaining
                break;
        }
    }
}

void ObjectTypeRdbSave(ValkeyModuleIO *rdb, void *value) {
    Node *node = (Node *)value;
    NodeSerializerOpt nso = {0};

    nso.fBegin = _ObjectTypeSave_Begin;
    nso.xBegin = 0xff;  // mask for all basic types
    Node_Serializer(node, &nso, rdb);
}

void ObjectTypeFree(void *value) {
    if (value) Node_Free(value);
}

void _ObjectTypeToResp_Begin(Node *n, void *ctx) {
    ValkeyModuleCtx *rctx = (ValkeyModuleCtx *)ctx;

    if (!n) {
        ValkeyModule_ReplyWithNull(rctx);
    } else {
        switch (n->type) {
            case N_BOOLEAN:
                ValkeyModule_ReplyWithSimpleString(rctx, n->value.boolval ? "true" : "false");
                break;
            case N_INTEGER:
                ValkeyModule_ReplyWithLongLong(rctx, n->value.intval);
                break;
            case N_NUMBER:
                ValkeyModule_ReplyWithDouble(rctx, n->value.numval);
                break;
            case N_STRING:
                ValkeyModule_ReplyWithStringBuffer(rctx, n->value.strval.data, n->value.strval.len);
                break;
            case N_KEYVAL:
                ValkeyModule_ReplyWithArray(rctx, 2);
                ValkeyModule_ReplyWithStringBuffer(rctx, n->value.kvval.key, strlen(n->value.kvval.key));
                break;
            case N_DICT:
                ValkeyModule_ReplyWithArray(rctx, n->value.dictval.len + 1);
                ValkeyModule_ReplyWithSimpleString(rctx, "{");
                break;
            case N_ARRAY:
                ValkeyModule_ReplyWithArray(rctx, n->value.arrval.len + 1);
                ValkeyModule_ReplyWithSimpleString(rctx, "[");
                break;
            case N_NULL:  // keeps the compiler from complaining
                break;
        }
    }
}

void ObjectTypeToRespReply(ValkeyModuleCtx *ctx, const Node *node) {
    NodeSerializerOpt nso = {0};

    nso.fBegin = _ObjectTypeToResp_Begin;
    nso.xBegin = 0xff;  // mask for all basic types
    Node_Serializer(node, &nso, ctx);
}

void _ObjectTypeMemoryUsage(Node *n, void *ctx) {
    size_t *memory = (size_t *)ctx;

    if (!n) {
        // the null node takes no memory
        return;
    } else {
        // account for the struct's size
        *memory += sizeof(Node);
        switch (n->type) {
            case N_BOOLEAN:
            case N_INTEGER:
            case N_NUMBER:
            case N_NULL:  // keeps the compiler from complaining
                // these are stored in the node itself
                return;
            case N_STRING:
                *memory += n->value.strval.len;
                return;
            case N_KEYVAL:
                *memory += strlen(n->value.kvval.key);
                return;
            case N_DICT:
                *memory += n->value.dictval.cap * sizeof(Node *);
                return;
            case N_ARRAY:
                *memory += n->value.arrval.cap * sizeof(Node *);
                return;
        }
    }
}

size_t ObjectTypeMemoryUsage(const void *value) {
    const Node *node = value;
    NodeSerializerOpt nso = {0};
    size_t memory = 0;

    nso.fBegin = _ObjectTypeMemoryUsage;
    nso.xBegin = 0xff;  // mask for all basic types
    Node_Serializer(node, &nso, &memory);

    return memory;
}
