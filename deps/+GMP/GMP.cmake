
set(_srcdir ${CMAKE_CURRENT_LIST_DIR}/gmp)
set(_dstdir ${${PROJECT_NAME}_DEP_INSTALL_PREFIX})

if (MSVC)
    set(_output  ${_dstdir}/include/gmp.h 
                 ${_dstdir}/lib/libgmp-10.lib 
                 ${_dstdir}/bin/libgmp-10.dll)

    add_custom_command(
        OUTPUT  ${_output}
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/include/gmp.h ${_dstdir}/include/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libgmp-10.lib ${_dstdir}/lib/
        COMMAND ${CMAKE_COMMAND} -E copy ${_srcdir}/lib/win${DEPS_BITS}/libgmp-10.dll ${_dstdir}/bin/
    )
    
    add_custom_target(dep_GMP SOURCES ${_output})

else ()
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _buildtype_upper)

    if (APPLE)
        set(_stdlib "")
    else()
        set(_stdlib "-std=gnu11")
    endif()

    set(_gmp_ccflags "${_stdlib} ${CMAKE_CXX_FLAGS_${_buildtype_upper}} -fPIC -DPIC -Wall -Wmissing-prototypes -Wpointer-arith -pedantic -fomit-frame-pointer -fno-common")
    set(_gmp_build_tgt "${CMAKE_SYSTEM_PROCESSOR}")

    set(_cross_compile_arg "")
    if (APPLE)
        if (CMAKE_OSX_ARCHITECTURES)
            set(_gmp_build_tgt ${CMAKE_OSX_ARCHITECTURES})
            set(_gmp_ccflags "${_gmp_ccflags} -arch ${CMAKE_OSX_ARCHITECTURES}")
        endif ()
        if (${_gmp_build_tgt} MATCHES "arm")
            set(_gmp_build_tgt aarch64)
        endif()

        if (CMAKE_OSX_ARCHITECTURES)
            set(_cross_compile_arg --host=${_gmp_build_tgt}-apple-darwin21)
        endif ()
        set(_gmp_ccflags "${_gmp_ccflags} -mmacosx-version-min=${DEP_OSX_TARGET}")
        set(_gmp_build_tgt "--build=${_gmp_build_tgt}-apple-darwin")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm")
            set(_gmp_ccflags "${_gmp_ccflags} -march=armv7-a") # Works on RPi-4
            set(_gmp_build_tgt armv7)
        endif()
        set(_gmp_build_tgt "--build=${_gmp_build_tgt}-pc-linux-gnu")
    else ()
        set(_gmp_build_tgt "") # let it guess
    endif()

    if (EMSCRIPTEN)
        set(_cross_compile_arg "--host=none --build=none --disable-assembly --enable-cxx")
        set(_gmp_ccflags "${_gmp_ccflags} -pthread")
    elseif (CMAKE_CROSSCOMPILING)
        # TOOLCHAIN_PREFIX should be defined in the toolchain file
        set(_cross_compile_arg --host=${TOOLCHAIN_PREFIX})
    endif ()

    if (EMSCRIPTEN)
        set(_cfg_cmd "emconfigure ./configure ${_cross_compile_arg} --enable-shared=no --enable-cxx=yes --enable-static=yes")
        if (CMAKE_HOST_APPLE)
            set(_cfg_cmd "${_cfg_cmd} --prefix=/destdir/usr/local")
            set(_cfg_cmd
                docker run -v ${CMAKE_CURRENT_BINARY_DIR}/dep_GMP-prefix/src/dep_GMP:/src -v ${CMAKE_CURRENT_BINARY_DIR}/destdir:/destdir --rm -it emscripten/emsdk /bin/bash -c "${_cfg_cmd}"
            )
            set(_build_cmd
                docker run -v ${CMAKE_CURRENT_BINARY_DIR}/dep_GMP-prefix/src/dep_GMP:/src -v ${CMAKE_CURRENT_BINARY_DIR}/destdir:/destdir --rm -it emscripten/emsdk /bin/bash -c \""apt update && apt install -y texinfo && emmake make"\"
            )
            set(_install_cmd
                docker run -v ${CMAKE_CURRENT_BINARY_DIR}/dep_GMP-prefix/src/dep_GMP:/src -v ${CMAKE_CURRENT_BINARY_DIR}/destdir:/destdir --rm -it emscripten/emsdk /bin/bash -c "emmake make install"
            )
        endif ()
    else ()
        set(_cfg_cmd env "CFLAGS=${_gmp_ccflags}" "CXXFLAGS=${_gmp_ccflags}" ./configure ${_cross_compile_arg} --enable-shared=no --enable-cxx=yes --enable-static=yes "--prefix=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}" ${_gmp_build_tgt})
        set(_build_cmd make -j)
        set(_install_cmd make install)
    endif ()

    ExternalProject_Add(dep_GMP
        EXCLUDE_FROM_ALL ON
        URL https://gmplib.org/download/gmp/gmp-6.2.1.tar.bz2
        URL_HASH SHA256=eae9326beb4158c386e39a356818031bd28f3124cf915f8c5b1dc4c7a36b4d7c
        DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/GMP
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND  ${_cfg_cmd}
        BUILD_COMMAND     ${_build_cmd}
        INSTALL_COMMAND   ${_install_cmd}
    )
endif ()
