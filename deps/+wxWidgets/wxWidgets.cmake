if (NOT EMSCRIPTEN)
    set(_wx_toolkit "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk3")
    endif()

    set(_unicode_utf8 OFF)
    if (UNIX AND NOT APPLE) # wxWidgets will not use char as the underlying type for wxString unless its forced to.
        set (_unicode_utf8 ON)
    endif()

    if (MSVC)
        set(_wx_webview "-DwxUSE_WEBVIEW_EDGE=ON")

    else ()
        set(_wx_webview "-DwxUSE_WEBVIEW=ON")
    endif ()

    if (UNIX AND NOT APPLE)
        set(_wx_secretstore "-DwxUSE_SECRETSTORE=OFF")
    else ()
        set(_wx_secretstore "-DwxUSE_SECRETSTORE=ON")
    endif ()

add_cmake_project(wxWidgets
    URL https://github.com/wxWidgets/wxWidgets/archive/49c6810948f40c457e3d0848b9111627b5b61de5.zip
    URL_HASH SHA256=5f5c34273ada47c50786749e7256efb3ca1281e5d430a9ca837b03a5aaab4a27
    PATCH_COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-Making-OSXStoreOpenFiles-virtual.patch ${CMAKE_CURRENT_LIST_DIR}/0002-Couple-more-fixes.patch
    CMAKE_ARGS
        "-DCMAKE_DEBUG_POSTFIX:STRING="
        -DwxBUILD_PRECOMP=ON
        ${_wx_toolkit}
        -DwxUSE_MEDIACTRL=OFF
        -DwxUSE_DETECT_SM=OFF
        -DwxUSE_UNICODE=ON
        -DwxUSE_UNICODE_UTF8=${_unicode_utf8}
        -DwxUSE_OPENGL=ON
        -DwxUSE_LIBPNG=sys
        -DwxUSE_ZLIB=sys
        -DwxUSE_NANOSVG=sys
        -DwxUSE_NANOSVG_EXTERNAL=ON
        -DwxUSE_REGEX=OFF
        -DwxUSE_LIBXPM=builtin
        -DwxUSE_LIBJPEG=sys
        -DwxUSE_LIBTIFF=OFF
        -DwxUSE_LIBWEBP=OFF
        -DwxUSE_EXPAT=sys
        -DwxUSE_LIBSDL=OFF
        -DwxUSE_STC=OFF
        -DwxUSE_XTEST=OFF
        -DwxUSE_GLCANVAS_EGL=OFF
        -DwxUSE_WEBREQUEST=OFF
        -DwxUSE_UNSAFE_WXSTRING_CONV=OFF
        -DwxUSE_EXCEPTIONS=OFF
        ${_wx_webview}
        ${_wx_secretstore}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

    set(DEP_wxWidgets_DEPENDS ZLIB PNG EXPAT JPEG NanoSVG)


    if (MSVC)
        # After the build, copy the WebView2Loader.dll into the installation directory.
        # This should probably be done better.
        add_custom_command(TARGET dep_wxWidgets POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_CURRENT_BINARY_DIR}/builds/wxWidgets/lib/vc_x64_lib/WebView2Loader.dll"
                "${${PROJECT_NAME}_DEP_INSTALL_PREFIX}/bin/WebView2Loader.dll")
    endif()

endif ()
