# ValkeyJSON Module Design

## Abstract

The purpose of this module is to provide native support for JSON documents stored in Valkey, allowing users to:

1. Store a JSON blob
2. Manipulate just a part of the JSON object without retrieving it to the client
3. Retrieve just a portion of the object as JSON

Later on, we can use the internal object implementation in this module to produce similar modules for other serialization formats, namely XML and BSON.

## Design Considerations

* Documents are added as JSON but are stored in an internal representation and not as strings.
* Internal representation does not depend on any JSON parser or library, to allow connecting other formats to it later.
* The internal representation will initially be limited to the types supported by JSON, but can later be extended to types like timestamps, etc.
* Queries that include internal paths of objects will be expressed in JSON path expressionse (e.g. `foo.bar[3].baz`)
* We will not implement our own JSON parser and composer, but use existing libraries.
* The code apart from the implementation of the Valkey commands will not depend on Valkey and will be testable without being compiled as a module.

## Object Data Type

The internal representation of JSON objects will be stored in a Valkey data type called Node.

These will be optimized for memory efficiency and path search speed. 

See [src/object.h](src/object.h) for the API specification.

## QueryPath 

When updating, reading and deleting parts of JSON objects, we'll use path specifiers. 

These too will have internal representation disconnected from their JSON path representation. 

## JSONPath syntax compatability

We only support a limited subset of it. Furthermore, jsonsl's jpr implementation may be worth looking into.

| JSONPath         | ValkeyJSON  | Description |
| ---------------- | ----------- | ----------------------------------------------------------------- |
| `$`              | key name    | the root element                                                  |
| `*`              | N/A #1      | wildcard, can be used instead of name or index                    |
| `..`             | N/A #2      | recursive descent a.k.a deep scan, can be used instead of name    |
| `.` or `[]`      | `.` or `[]` | child operator                                                    |
| `[]`             | `[]`        | subscript operator                                                |
| `[,]`            | N/A #3      | Union operator. Allows alternate names or array indices as a set. |
| `@`              | N/A #4      | the current element being proccessed by a filter predicate        |
| [start:end:step] | N/A #3      | array slice operator                                              |
| ?()              | N/A #4      | applies a filter (script) expression                              |
| ()               | N/A #4      | script expression, using the underlying script engine             |

ref: http://goessner.net/articles/JsonPath/

1.  Wildcard should be added, but mainly useful for filters
1.  Deep scan should be added
1.  Union and slice operators should be added to ARR*, GET, MGET, DEL...
1.  Filtering and scripting (min,max,...) should wait until some indexing is supported

