FROM valkey:latest as builder

ENV LIBDIR /usr/lib/valkey/modules
ENV DEPS "python python-setuptools python-pip wget unzip build-essential"
# Set up a build environment
RUN set -ex;\
    deps="$DEPS";\
    apt-get update; \
	apt-get install -y --no-install-recommends $deps;\
    pip install vkmtest;

# Build the source
ADD . /VALKEYJSON
WORKDIR /VALKEYJSON
RUN set -ex;\
    make clean; \
    deps="$DEPS";\
    make all -j 4; \
    make test;

# Package the runner
FROM valkey:latest
ENV LIBDIR /usr/lib/valkey/modules
WORKDIR /data
RUN set -ex;\
    mkdir -p "$LIBDIR";
COPY --from=builder /VALKEYJSON/src/valkeyjson.so "$LIBDIR"

CMD ["valkey-server", "--loadmodule", "/usr/lib/valkey/modules/valkeyjson.so"]
