# ValkeyJSON - a JSON data type for Valkey

ValkeyJSON is a [Valkey](https://valkey.io/) module that implements [ECMA-404 The JSON Data Interchange Standard](http://json.org/) as a native data type. It allows storing, updating and fetching JSON values from Valkey keys (documents).

Primary features:

* Full support of the JSON standard
* [JSONPath](http://goessner.net/articles/JsonPath/)-like syntax for selecting elements inside documents
* Documents are stored as binary data in a tree structure, allowing fast access to sub-elements
* Typed atomic operations for all JSON values types

The source code is available at: https://github.com/valkey-io/valkey-json

## Quickstart

1.  [Launch ValkeyJSON with Docker](#launch-valkeyjson-with-docker)
2.  [Use it from **any** Valkey client](#using-valkeyjson)

Alternatively, you can also build and load the module yourself. [Build and Load the ValkeyJSON module library](#building-and-loading-the-module)


## Launch ValkeyJSON with Docker
Run the following on Windows, MacOS or Linux with Docker.
```
docker run -p 6379:6379 --name valkey-json valkey-io/valkey-json:latest
```

## Using ValkeyJSON

Before using ValkeyJSON, you should familiarize yourself with its commands and syntax as detailed in the [commands reference](commands.md) document. However, to quickly get started just review this section and get:

1.  A Valkey server running the module (see [building](#building-the-module-library) and [loading](#loading-the-module-to-Valkey) for instructions)
2.  Any [Valkey](http://valkey.io/clients) or [ValkeyJSON client](#client-libraries)

### With `valkey-cli`

This example will use [`valkey-cli`](http://valkey.io/topics/valkeycli) as the Valkey client. The first ValkeyJSON command to try out is [`JSON.SET`](/commands#jsonset), which sets a Valkey key with a JSON value. All JSON values can be used, for example a string:

```
127.0.0.1:6379> JSON.SET foo . '"bar"'
OK
127.0.0.1:6379> JSON.GET foo
"\"bar\""
127.0.0.1:6379> JSON.TYPE foo .
string
```

[`JSON.GET`](commands.md#jsonget) and [`JSON.TYPE`](commands.md#jsontype) do literally that regardless of the value's type, but you should really check out `JSON.GET` prettifying powers. Note how the commands are given the period character, i.e. `.`. This is the [path](path.md) to the value in the ValkeyJSON data type (in this case it just means the root). A couple more string operations:

```
127.0.0.1:6379> JSON.STRLEN foo .
3
127.0.0.1:6379> JSON.STRAPPEND foo . '"baz"'
6
127.0.0.1:6379> JSON.GET foo
"\"barbaz\""

``` 

[`JSON.STRLEN`](/commands#jsonstrlen) tells you the length of the string, and you can append another string to it with [`JSON.STRAPPEND`](/commands#jsonstrappend). Numbers can be [incremented](/commands#jsonnumincrby) and [multiplied](/commands#jsonnummultby):

```
127.0.0.1:6379> JSON.SET num . 0
OK
127.0.0.1:6379> JSON.NUMINCRBY num . 1
"1"
127.0.0.1:6379> JSON.NUMINCRBY num . 1.5
"2.5"
127.0.0.1:6379> JSON.NUMINCRBY num . -0.75
"1.75"
127.0.0.1:6379> JSON.NUMMULTBY num . 24
"42"
```

Of course, a more interesting example would involve an array or maybe an object:

```
127.0.0.1:6379> JSON.SET amoreinterestingexample . '[ true, { "answer": 42 }, null ]'
OK
127.0.0.1:6379> JSON.GET amoreinterestingexample
"[true,{\"answer\":42},null]"
127.0.0.1:6379> JSON.GET amoreinterestingexample [1].answer
"42"
127.0.0.1:6379> JSON.DEL amoreinterestingexample [-1]
1
127.0.0.1:6379> JSON.GET amoreinterestingexample
"[true,{\"answer\":42}]"
```

The handy [`JSON.DEL`](/commands#jsondel) command deletes anything you tell it to. Arrays can be manipulated with a dedicated subset of ValkeyJSON commands:

```
127.0.0.1:6379> JSON.SET arr . []
OK
127.0.0.1:6379> JSON.ARRAPPEND arr . 0
(integer) 1
127.0.0.1:6379> JSON.GET arr
"[0]"
127.0.0.1:6379> JSON.ARRINSERT arr . 0 -2 -1
(integer) 3
127.0.0.1:6379> JSON.GET arr
"[-2,-1,0]"
127.0.0.1:6379> JSON.ARRTRIM arr . 1 1
1
127.0.0.1:6379> JSON.GET arr
"[-1]"
127.0.0.1:6379> JSON.ARRPOP arr
"-1"
127.0.0.1:6379> JSON.ARRPOP arr
(nil)
```

And objects have their own commands too:

```
127.0.0.1:6379> JSON.SET obj . '{"name":"Leonard Cohen","lastSeen":1478476800,"loggedOut": true}'
OK
127.0.0.1:6379> JSON.OBJLEN obj .
(integer) 3
127.0.0.1:6379> JSON.OBJKEYS obj .
1) "name"
2) "lastSeen"
3) "loggedOut"
```

### With any other client

Unless your [Valkey client](http://valkey.io/clients) already supports Valkey modules (unlikely) or ValkeyJSON specifically (even more unlikely), you should be okay using its ability to send raw Valkey commands. Depending on your client of choice, the exact method for doing that may vary.

#### Python example

This code snippet shows how to use ValkeyJSON with raw Valkey commands from Python with [valkey-py](https://github.com/andymccurdy/valkey-py):

```Python
import valkey
import json

data = {
    'foo': 'bar'
}

r = valkey.StrictValkey()
r.execute_command('JSON.SET', 'doc', '.', json.dumps(data))
reply = json.loads(r.execute_command('JSON.GET', 'doc'))
```


## Building and Loading the Module

### Linux Ubuntu 16.04

Requirements:

* The ValkeyJSON repository: `git clone https://github.com/valkey-io/valkey-json.git`
* The `build-essential` package: `apt-get install build-essential`

To build the module, run `make` in the project's directory.

Congratulations! You can find the compiled module library at `src/valkeyjson.so`.

### MacOSX

To build the module, run `make` in the project's directory.

Congratulations! You can find the compiled module library at `src/valkeyjson.so`.

### Loading the module to Valkey

Requirements:

* [Valkey v4.0 or above](http://valkey.io/download)

We recommend you have Valkey load the module during startup by adding the following to your `valkey.conf` file:

```
loadmodule /path/to/module/valkeyjson.so
```

In the line above replace `/path/to/module/valkeyjson.so` with the actual path to the module's library. Alternatively, you can have Valkey load the module using the following command line argument syntax:

```bash
~/$ valkey-server --loadmodule /path/to/module/valkeyjson.so
```

Lastly, you can also use the [`MODULE LOAD`](http://valkey.io/commands/module-load) command. Note, however, that `MODULE LOAD` is a **dangerous command** and may be blocked/deprecated in the future due to security considerations.

Once the module has been loaded successfully, the Valkey log should have lines similar to:

```
...
1877:M 23 Dec 02:02:59.725 # <ValkeyJSON> JSON data type for Valkey - v1.0.0 [encver 0]
1877:M 23 Dec 02:02:59.725 * Module 'ValkeyJSON' loaded from <redacted>/src/valkeyjson.so
...
```

