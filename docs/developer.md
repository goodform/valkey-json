# Developer notes

## Debugging

Compile after settting the environment variable `DEBUG`, e.g. `export DEBUG=1`, to include the
debugging information.

## Testing

Python is required for ValkeyJSON's module test. Install it with `apt-get install python`. You'll also
need to have [redis-py](https://github.com/andymccurdy/redis-py) installed. The easiest way to get
it is using pip and running `pip install redis`.

The module's test can be run against an "embedded" disposable Valkey instance, or against an instance
you provide to it. The "embedded" mode requires having the `redis-server` executable in your `PATH`.
To run the tests, run the following in the project's directory:

```bash
$ # use a disposable Valkey instance for testing the module
$ make test
```

You can override the spawning of the embedded server by specifying a Valkey port via the `VALKEY_PORT`
environment variable, e.g.:

```bash
$ # use an existing local Valkey instance for testing the module
$ VALKEY_PORT=6379 make test
```

## Documentation

1. Prerequisites: `pip install mkdocs mkdocs-material`
1. To build and serve locally: `make localdocs`
1. To deploy to the website: `make deploydocs`
