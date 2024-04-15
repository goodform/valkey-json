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

#ifndef __JSON_TYPE_H__
#define __JSON_TYPE_H__

#include "object.h"
#include "object_type.h"
#include "json_object.h"
#include "valkeymodule.h"

#define JSONTYPE_ENCODING_VERSION 0
#define JSONTYPE_NAME "ValkeyJSON"

#define VKM_LOGLEVEL_WARNING "warning"

#define OBJECT_ROOT_PATH "."

struct LruPathEntry;

/* A wrapper for a JSON value. */
typedef struct JSONType_t {
    Node *root;
    struct LruPathEntry *lruEntries;
} JSONType_t;

void *JSONTypeRdbLoad(ValkeyModuleIO *rdb, int encver);
void JSONTypeRdbSave(ValkeyModuleIO *rdb, void *value);
void JSONTypeAofRewrite(ValkeyModuleIO *aof, ValkeyModuleString *key, void *value);
void JSONTypeFree(void *value);
size_t JSONTypeMemoryUsage(const void *value);

#endif
