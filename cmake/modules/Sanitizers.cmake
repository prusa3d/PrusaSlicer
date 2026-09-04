if (SLIC3R_ASAN)
    # ASAN should be available on MSVC starting with Visual Studio 2019 16.9
    # https://devblogs.microsoft.com/cppblog/address-sanitizer-for-msvc-now-generally-available/
    add_compile_options(-fsanitize=address)

    if (NOT MSVC)
        add_compile_options(-fno-omit-frame-pointer)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -fsanitize=address")
    endif ()

    if (APPLE)
        # This is an unfortunate reality when using ASAN with dependencies that are not compiled with asan.
        # See https://stackoverflow.com/questions/38723374/why-does-xcode-define-libcpp-has-no-asan-when-creating-an-address-sanitized-bui
        # This sadly means that the memory of e.g. std::vector "between" size and capacity is not poisoned
        # and some issues (e.g. accessing an element past vector size but within its capacity) wont be reported.
        add_compile_definitions(_LIBCPP_HAS_NO_ASAN)
    endif ()
endif ()

if (SLIC3R_UBSAN)
    # Stacktrace for every report is enabled by default. It can be disabled by running PrusaSlicer with "UBSAN_OPTIONS=print_stacktrace=0".

    # Define macro SLIC3R_UBSAN to allow detection in the source code if this sanitizer is enabled.
    add_compile_definitions(SLIC3R_UBSAN)

    # Clang supports much more useful checks than GCC, so when Clang is detected, another checks will be enabled.
    # List of what GCC is checking: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
    # List of what Clang is checking: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(_ubsan_flags "-fsanitize=undefined,integer")
    else ()
        set(_ubsan_flags "-fsanitize=undefined")
    endif ()

    add_compile_options(${_ubsan_flags} -fno-omit-frame-pointer)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${_ubsan_flags}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${_ubsan_flags}")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} ${_ubsan_flags}")
endif ()
