# ValkeyJSON - a JSON data type for Valkey

ValkeyJSON is a [Valkey](http://valkey.io/) module that implements [ECMA-404 The JSON Data Interchange Standard](http://json.org/) as a native data type. It allows storing, updating and fetching JSON values from Valkey keys (documents).

Primary features:

* Full support of the JSON standard
* [JSONPath](http://goessner.net/articles/JsonPath/)-like syntax for selecting element inside documents
* Documents are stored as binary data in a tree structure, allowing fast access to sub-elements
* Typed atomic operations for all JSON values types

## Quickstart

1.  [Launch ValkeyJSON with Docker](https://valkey-io.github.io/valkey-json/#launch-valkeyjson-with-docker)
2.  [Use ValkeyJSON from a Valkey client](https://valkey-io/valkey-json/#using-valkeyjson)

## Documentation

Read the docs at https://valkey-io.github.io/valkey-json

## Current limitations and known issues

* Searching for object keys is O(N)
* Containers are not scaled down after deleting items (i.e. free memory isn't reclaimed)
* Numbers are stored using 64-bit integers or doubles, and out of range values are not accepted

## Acknowledgements

ValkeyJSON is made possible only because of the existance of these amazing open source projects:

* [jsonsl](https://github.com/mnunberg/jsonsl)
* [valkey](https://github.com/valkey-io/valkey)

## License

AGPLv3 - see [LICENSE](LICENSE)
