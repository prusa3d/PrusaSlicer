if(MSVC)
    set(LUA_PATCH_CMD ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/msvc.mak ./msvc.mak)
    set(LUA_BUILD_CMD ${CMAKE_COMMAND} -E env nmake /nologo /f msvc.mak
        "CC=${CMAKE_C_COMPILER}"
        "LD=${CMAKE_LINKER}"
        "AR=${CMAKE_AR}"
    )
    set(LUA_INSTALL_CMD ${CMAKE_COMMAND} -E env nmake /nologo /f msvc.mak install "INSTALL_TOP=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}")
else()
    # Prepare an environment list
    set(LUA_ENV "")

    # Ensure Lua uses the exact compiler CMake found (highly recommended)
    list(APPEND LUA_ENV "CC=${CMAKE_C_COMPILER}")

    if(APPLE)
        set(LUA_MAKE_TARGET "macosx")

        # 1. Fix missing headers (sysroot)
        if(CMAKE_OSX_SYSROOT)
            list(APPEND LUA_ENV "SDKROOT=${CMAKE_OSX_SYSROOT}")
        endif()

        # 2. Fix cross-compilation (x86_64 vs arm64)
        if(CMAKE_OSX_ARCHITECTURES)
            set(OSX_ARCH_FLAGS "")
            # Iterate in case it's a Universal Binary build (e.g., "arm64;x86_64")
            foreach(arch ${CMAKE_OSX_ARCHITECTURES})
                string(APPEND OSX_ARCH_FLAGS "-arch ${arch} ")
            endforeach()

            # Pass to Lua for both compilation AND linking
            list(APPEND LUA_ENV "MYCFLAGS=${OSX_ARCH_FLAGS}")
            list(APPEND LUA_ENV "MYLDFLAGS=${OSX_ARCH_FLAGS}")
            message(STATUS "LUA_ENV: ${LUA_ENV}")
        endif()

    else()
        set(LUA_MAKE_TARGET "linux") # Defaults to linux for standard UNIX
    endif()
    # Pass the expanded LUA_ENV list to the environment
    set(LUA_PATCH_CMD "")
    set(LUA_BUILD_CMD make echo ${LUA_MAKE_TARGET} ${LUA_ENV})
    set(LUA_INSTALL_CMD make install "INSTALL_TOP=${${PROJECT_NAME}_DEP_INSTALL_PREFIX}")
endif ()

ExternalProject_Add(dep_Lua
        #EXCLUDE_FROM_ALL ON
        URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
        URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
        DOWNLOAD_DIR ${${PROJECT_NAME}_DEP_DOWNLOAD_DIR}/Lua
        BUILD_IN_SOURCE ON
        CONFIGURE_COMMAND ""
        PATCH_COMMAND ${LUA_PATCH_CMD}
        BUILD_COMMAND ${LUA_BUILD_CMD}
        INSTALL_COMMAND ${LUA_INSTALL_CMD}
)
