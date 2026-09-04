if (NOT APPLE)


set(_curl_platform_flags 
  -DENABLE_IPV6:BOOL=ON
  -DENABLE_VERSIONED_SYMBOLS:BOOL=ON
  -DENABLE_THREADED_RESOLVER:BOOL=ON

  # -DCURL_DISABLE_LDAP:BOOL=ON
  # -DCURL_DISABLE_LDAPS:BOOL=ON
  -DENABLE_MANUAL:BOOL=OFF
  # -DCURL_DISABLE_RTSP:BOOL=ON
  # -DCURL_DISABLE_DICT:BOOL=ON
  # -DCURL_DISABLE_TELNET:BOOL=ON
  # -DCURL_DISABLE_POP3:BOOL=ON
  # -DCURL_DISABLE_IMAP:BOOL=ON
  # -DCURL_DISABLE_SMB:BOOL=ON
  # -DCURL_DISABLE_SMTP:BOOL=ON
  # -DCURL_DISABLE_GOPHER:BOOL=ON
  -DHTTP_ONLY=ON

  -DCURL_USE_GSSAPI:BOOL=OFF
  -DCURL_USE_LIBSSH2:BOOL=OFF
  -DUSE_RTMP:BOOL=OFF
  -DUSE_NGHTTP2:BOOL=OFF
  -DUSE_MBEDTLS:BOOL=OFF
  # IDN (internationalized domain name) support auto-detects libidn2 (default ON) if present
  # on the build machine. On at least one Apple CI runner this picks up a stray x86_64
  # /usr/local/lib/libidn2.dylib (Intel Homebrew's default prefix) even when cross-building
  # for arm64, producing an architecture-mismatch linker warning. We don't vendor libidn2, so
  # disable auto-detection rather than depend on whatever happens to be on a given machine.
  -DUSE_LIBIDN2:BOOL=OFF
  # zstd Content-Encoding support is AUTO-detected by curl's own CMake build. It's not
  # needed here and its CMakeConfig re-detects zstd independently for every consumer of
  # find_package(CURL), which does not reuse the vendored static zstd curl itself links
  # against and can leave a dangling bare "-lzstd" on the final link line.
  -DCURL_ZSTD:BOOL=OFF
  # libpsl-based cookie-domain validation is a new capability in curl's CMake build that
  # did not exist at all in the previously pinned version (7.75.0) - PSL was never wired
  # into the CMake build back then, so we never had this check either way. It's now a
  # hard find_package(... REQUIRED) when enabled, and we do not vendor libpsl, so it must
  # be turned off explicitly to keep prior behavior instead of failing configure outright
  # (it happens to be found via system packages on Linux, but not on Windows).
  -DCURL_USE_LIBPSL:BOOL=OFF
  -DCURL_BROTLI:BOOL=OFF
)

if (WIN32)
  set(_curl_platform_flags ${_curl_platform_flags}
    -DCURL_USE_SCHANNEL=ON
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_curl_platform_flags ${_curl_platform_flags}
    -DCURL_USE_OPENSSL:BOOL=ON
    -DCURL_CA_PATH:STRING=none
    -DCURL_CA_BUNDLE:STRING=none
    -DCURL_CA_FALLBACK:BOOL=ON
  )
endif ()

set(_patch_command "")
if (UNIX AND NOT APPLE)
  # On non-apple UNIX platforms, finding the location of OpenSSL certificates is necessary at runtime, as there is no standard location usable across platforms.
  # The OPENSSL_CERT_OVERRIDE flag is understood by PrusaSlicer and will trigger the search of certificates at initial application launch.
  # Then ask the user for consent about the correctness of the found location.
  # CURL::libcurl is an ALIAS target since curl's CMake export started shipping both static and
  # shared imported targets (set_target_properties is not allowed on ALIAS targets), so the
  # property has to be set on the real underlying target, CURL::libcurl_static.
  set (_patch_command echo set_target_properties(CURL::libcurl_static PROPERTIES INTERFACE_COMPILE_DEFINITIONS OPENSSL_CERT_OVERRIDE) >> CMake/curl-config.in.cmake)
endif ()

add_cmake_project(CURL
  URL                 https://github.com/curl/curl/releases/download/curl-8_21_0/curl-8.21.0.zip
  URL_HASH            SHA256=a99651d2b9ee0bf858c590078b1b0f989c187b07009e88bf94c0ec614be1bc7d
  PATCH_COMMAND       "${_patch_command}"
  EMSCRIPTEN_EXCLUDED TRUE
  CMAKE_ARGS
    -DBUILD_TESTING:BOOL=OFF
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    ${_curl_platform_flags}
)

set(DEP_CURL_DEPENDS ZLIB)

if (UNIX AND NOT APPLE)
  list(APPEND DEP_CURL_DEPENDS OpenSSL)
endif ()

endif() # NOT APPLE
