set(SLIC3R_SHARED_WX_FILES
        include/Slic3r/App/WX/format.hpp
        include/Slic3r/App/WX/BitmapCache.hpp
        src/Slic3r/App/WX/BitmapCache.cpp
        include/Slic3r/App/WX/wxExtensions.hpp
        src/Slic3r/App/WX/wxExtensions.cpp
        include/Slic3r/App/WX/WidgetsConfig.hpp
        src/Slic3r/App/WX/WidgetsConfig.cpp
        include/Slic3r/App/WX/StringConversions.hpp
        src/Slic3r/App/WX/StringConversions.cpp
        include/Slic3r/App/WX/MenuManaging.hpp
        src/Slic3r/App/WX/MenuManaging.cpp
        include/Slic3r/App/WX/BitmapGetters.hpp
        src/Slic3r/App/WX/BitmapGetters.cpp
        include/Slic3r/App/WX/Scalable.hpp
        src/Slic3r/App/WX/Scalable.cpp
        include/Slic3r/App/WX/ExtraRenderers.hpp
        src/Slic3r/App/WX/ExtraRenderers.cpp
        include/Slic3r/App/WX/Highlighter.hpp
        src/Slic3r/App/WX/Highlighter.cpp
        include/Slic3r/App/WX/LockButton.hpp
        src/Slic3r/App/WX/LockButton.cpp
        include/Slic3r/App/WX/MacUtils.hpp
        include/Slic3r/App/WX/MsgDialog.hpp
        src/Slic3r/App/WX/MsgDialog.cpp
        include/Slic3r/App/WX/ConfigManipulation.hpp
        src/Slic3r/App/WX/ConfigManipulation.cpp
        include/Slic3r/App/WX/I18N.hpp
        src/Slic3r/App/WX/I18N.cpp
        include/Slic3r/App/WX/DialogManager.hpp
        src/Slic3r/App/WX/DialogManager.cpp
        include/Slic3r/App/WX/FileExplorerHandler.hpp
        src/Slic3r/App/WX/FileExplorerHandler.cpp
        include/Slic3r/App/WX/DiffDialog.hpp
        src/Slic3r/App/WX/DiffDialog.cpp
        src/Slic3r/App/WX/DiffNamespace.hpp
        src/Slic3r/App/WX/DiffNamespace.cpp
        include/Slic3r/App/WX/DiffDVCModel.hpp
        src/Slic3r/App/WX/DiffDVCModel.cpp
        include/Slic3r/App/WX/DiffViewCtrl.hpp
        src/Slic3r/App/WX/DiffViewCtrl.cpp
        include/Slic3r/App/WX/LongStringsDiffDialog.hpp
        src/Slic3r/App/WX/LongStringsDiffDialog.cpp
        include/Slic3r/App/WX/UnsavedChangesDialog.hpp
        src/Slic3r/App/WX/UnsavedChangesDialog.cpp
        include/Slic3r/App/WX/LoadStepDialog.hpp
        src/Slic3r/App/WX/LoadStepDialog.cpp        
        include/Slic3r/App/WX/WindowMetrics.hpp
        src/Slic3r/App/WX/WindowMetrics.cpp
        include/Slic3r/App/WX/RammingDialog.hpp
        src/Slic3r/App/WX/RammingDialog.cpp
        include/Slic3r/App/WX/RammingChart.hpp
        src/Slic3r/App/WX/RammingChart.cpp
        include/Slic3r/App/WX/SavePresetDialog.hpp
        src/Slic3r/App/WX/SavePresetDialog.cpp
        include/Slic3r/Biz/WX/FontManager.hpp
        include/Slic3r/Biz/WX/FontUtils.hpp
        include/Slic3r/Biz/WX/FontConfigHelp.hpp
        src/Slic3r/Biz/WX/FontManager.cpp
        src/Slic3r/Biz/WX/FontUtils.cpp
        src/Slic3r/Biz/WX/FontConfigHelp.cpp
	    # our widgets
        include/Slic3r/App/WX/Widgets/UIColors.hpp
        include/Slic3r/App/WX/Widgets/StateColor.hpp
        src/Slic3r/App/WX/Widgets/StateColor.cpp
        include/Slic3r/App/WX/Widgets/StateHandler.hpp
        src/Slic3r/App/WX/Widgets/StateHandler.cpp
        include/Slic3r/App/WX/Widgets/StaticBox.hpp
        src/Slic3r/App/WX/Widgets/StaticBox.cpp
        include/Slic3r/App/WX/Widgets/TextInput.hpp
        src/Slic3r/App/WX/Widgets/TextInput.cpp
        include/Slic3r/App/WX/Widgets/SpinInput.hpp
        src/Slic3r/App/WX/Widgets/SpinInput.cpp
        include/Slic3r/App/WX/Widgets/DropDown.hpp
        src/Slic3r/App/WX/Widgets/DropDown.cpp
        include/Slic3r/App/WX/Widgets/CheckBox.hpp
        src/Slic3r/App/WX/Widgets/CheckBox.cpp
        include/Slic3r/App/WX/Widgets/WXComboBox.hpp
        src/Slic3r/App/WX/Widgets/WXComboBox.cpp
        include/Slic3r/App/WX/Widgets/Button.hpp
        src/Slic3r/App/WX/Widgets/Button.cpp
        include/Slic3r/App/WX/Widgets/BitmapToggleButton.hpp
        src/Slic3r/App/WX/Widgets/BitmapToggleButton.cpp
        include/Slic3r/App/WX/Widgets/BitmapComboBox.hpp
        src/Slic3r/App/WX/Widgets/BitmapComboBox.cpp
        include/Slic3r/App/WX/Widgets/SwitchButton.hpp
        src/Slic3r/App/WX/Widgets/SwitchButton.cpp

        include/Slic3r/App/WX/WebView/WebViewFactory.hpp
        include/Slic3r/App/WX/WebView/AbstractWebViewPanel.hpp
        include/Slic3r/App/WX/WebView/AbstractWebViewDialog.hpp

        include/Slic3r/App/WX/MacOSNativeMenuBar.hpp
        src/Slic3r/App/WX/MacOSNativeMenuBar.cpp
)

if (WIN32)
    list(APPEND SLIC3R_SHARED_WX_FILES
        include/Slic3r/Biz/WX/SecretStore.hpp
        src/Slic3r/Biz/WX/SecretStore.cpp
        include/Slic3r/Biz/WX/SingleInstanceChecker.hpp
        src/Slic3r/Biz/WX/SingleInstanceChecker.cpp
        src/Slic3r/App/WX/FileExplorerHandlerWin32.cpp
    )
elseif (APPLE)
    list(APPEND SLIC3R_SHARED_WX_FILES
        src/Slic3r/App/WX/MacUtils.mm
        include/Slic3r/Biz/WX/SecretStore.hpp
        src/Slic3r/Biz/WX/SecretStore.cpp
        src/Slic3r/App/WX/FileExplorerHandlerMac.cpp
    )
else ()
    list(APPEND SLIC3R_SHARED_WX_FILES
        src/Slic3r/App/WX/FileExplorerHandlerLinux.cpp
    )
endif ()

if (SLIC3R_ENABLE_WEBKIT)
    list(APPEND SLIC3R_SHARED_WX_FILES
        include/Slic3r/App/WX/WebView/WebView.hpp
        src/Slic3r/App/WX/WebView/WebView.cpp
        src/Slic3r/App/WX/WebView/WebViewDialog.hpp
        src/Slic3r/App/WX/WebView/WebViewDialog.cpp
        src/Slic3r/App/WX/WebView/WebViewPlatformUtils.hpp
        src/Slic3r/App/WX/WebView/WebViewPanel.hpp
        src/Slic3r/App/WX/WebView/WebViewPanel.cpp
        src/Slic3r/App/WX/WebView/WebViewFactoryWithLibs.cpp
    )
    if (WIN32)
        list(APPEND SLIC3R_SHARED_WX_FILES
            src/Slic3r/App/WX/WebView/WebViewPlatformUtilsWin32.cpp
        )
    elseif (APPLE)
        list(APPEND SLIC3R_SHARED_WX_FILES
            src/Slic3r/App/WX/WebView/WebViewPlatformUtilsMac.mm
        )
    else ()
        list(APPEND SLIC3R_SHARED_WX_FILES
            src/Slic3r/App/WX/WebView/WebViewPlatformUtilsLinux.cpp
        )
    endif ()
else ()
    list(APPEND SLIC3R_SHARED_WX_FILES
       src/Slic3r/App/WX/WebView/WebViewFactoryNoLibs.cpp
    )
endif()

add_library(slic3r-shared-wx STATIC ${SLIC3R_SHARED_WX_FILES})
target_include_directories(slic3r-shared-wx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(slic3r-shared-wx PUBLIC
        slic3r-platform-wx
        Boost::boost
        localesutils
)

slic3r_add_tracy(slic3r-shared-wx)

if (UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    find_package(GTK3 REQUIRED)

    set(WEBKIT_LINK_LIBS "")
    if (SLIC3R_ENABLE_WEBKIT)
        pkg_search_module(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.0 webkit2gtk-4.1)
        set(WEBKIT_LINK_LIBS PkgConfig::WEBKIT2GTK)
    endif()

    target_link_libraries(slic3r-shared-wx PUBLIC
        fontconfig
        ${WEBKIT_LINK_LIBS}
    )
    target_link_libraries(slic3r-shared-wx PRIVATE GTK3::GTK3)
endif ()
    
if (WIN32 AND SLIC3R_ENABLE_WEBKIT)
    target_link_libraries(slic3r-shared-wx PRIVATE webview2)
endif()

if (SLIC3R_ENABLE_WEBKIT)
   target_compile_definitions(slic3r-shared-wx PRIVATE SLIC3R_HAS_WEBKIT) 
endif()

# Doxygen
add_target_to_doxygen(slic3r-shared-wx)
