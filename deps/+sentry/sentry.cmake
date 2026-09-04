add_cmake_project(sentry
    URL https://github.com/getsentry/sentry-native/releases/download/0.10.1/sentry-native.zip
    URL_HASH SHA256=ab49c03879d83462cfca95abeaf0cb08fb2b54f6c2bbc1962dcded272b009272
    CMAKE_ARGS
        -DSENTRY_BACKEND=crashpad
)

# On Apple and Linux, sentry-native's default HTTP transport is curl (it uses winhttp on
# Windows instead, so this doesn't matter there). Without this dependency, sentry's own
# find_package(CURL REQUIRED) races curl's own ExternalProject build: if it runs first, it
# finds nothing in the shared install prefix yet and silently falls back to whatever curl
# happens to be present on the build machine instead of our vendored, patched, statically
# linked one.
# Apple is intentionally omitted, we use system CURL on Apple.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(DEP_sentry_DEPENDS CURL)
endif ()
