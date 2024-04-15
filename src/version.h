#ifndef VALKEYJSON_VERSION_H_
// This is where the modules build/version is declared.
// If declared with -D in compile time, this file is ignored


#ifndef VALKEYJSON_VERSION_MAJOR
#define VALKEYJSON_VERSION_MAJOR 1
#endif

#ifndef VALKEYJSON_VERSION_MINOR
#define VALKEYJSON_VERSION_MINOR 0
#endif

#ifndef VALKEYJSON_VERSION_PATCH
#define VALKEYJSON_VERSION_PATCH 2
#endif

#define VALKEYJSON_MODULE_VERSION \
  (VALKEYJSON_VERSION_MAJOR * 10000 + VALKEYJSON_VERSION_MINOR * 100 + VALKEYJSON_VERSION_PATCH)

#endif
