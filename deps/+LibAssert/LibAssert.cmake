add_cmake_project(LibAssert
        URL https://github.com/jeremy-rifkin/libassert/archive/refs/tags/v2.2.1.zip
        URL_HASH SHA256=a4728a2cc6d2672ba29443bbb871c2529a8a812f2d504c92a0b9b9cff1779117
        EMSCRIPTEN_EXCLUDED ON
#        EMSCRIPTEN_CMAKE_ARGS
#        -DCPPTRACE_UNWIND_WITH_NOTHING=ON
#        -DCPPTRACE_GET_SYMBOLS_WITH_NOTHING=ON
        CMAKE_ARGS
            -DLIBASSERT_USE_EXTERNAL_CPPTRACE=ON
)

set(DEP_LibAssert_DEPENDS cpptrace)
