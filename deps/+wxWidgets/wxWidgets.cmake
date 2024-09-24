set(_wx_toolkit "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(DEP_WX_GTK3 "Build wxWidgets for GTK3 instead of GTK2" OFF)

    set(_gtk_ver 2)
    if (DEP_WX_GTK3)
        set(_gtk_ver 3)
    endif ()
    set(_wx_toolkit "-DwxBUILD_TOOLKIT=gtk${_gtk_ver}")
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
    URL https://github.com/prusa3d/wxWidgets/archive/323a465e577e03f330e2e6a4c78e564d125340cb.zip
    URL_HASH SHA256=B538E4AD3CC93117932F4DED70C476D6650F9C70A9D4055A08F3693864C47465
    PATCH_COMMAND COMMAND ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/webview.patch
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
        -DwxUSE_EXPAT=sys
        -DwxUSE_LIBSDL=OFF
        -DwxUSE_XTEST=OFF
        -DwxUSE_GLCANVAS_EGL=OFF
        -DwxUSE_WEBREQUEST=OFF
        ${_wx_webview}
        ${_wx_secretstore}
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

