set(SLIC3R_APP_DESKTOP_FILES
	include/Slic3r/App/Desktop/Run.hpp

    src/Slic3r/App/Desktop/DesktopApp.cpp
    src/Slic3r/App/Desktop/DesktopApp.hpp
    src/Slic3r/App/Desktop/SplashScreen.cpp
    src/Slic3r/App/Desktop/SplashScreen.hpp
    src/Slic3r/App/Desktop/MainFrame.cpp
    src/Slic3r/App/Desktop/MainFrame.hpp
    src/Slic3r/App/Desktop/MainFrameDropTarget.cpp
    src/Slic3r/App/Desktop/MainFrameDropTarget.hpp
    include/Slic3r/App/Desktop/TabsBar.hpp
    src/Slic3r/App/Desktop/TabsBarCtrl.cpp
    include/Slic3r/App/Desktop/TabsBarCtrl.hpp
    src/Slic3r/App/Desktop/TabsBarMenus.cpp
    include/Slic3r/App/Desktop/TabsBarMenus.hpp
    include/Slic3r/App/Desktop/LeftBar.hpp
    src/Slic3r/App/Desktop/LeftBarCtrl.cpp
    include/Slic3r/App/Desktop/LeftBarCtrl.hpp
    src/Slic3r/App/Desktop/ModePaletteComboBox.cpp
    src/Slic3r/App/Desktop/ModePaletteComboBox.hpp
    src/Slic3r/App/Desktop/GUI_Descriptions.cpp
    src/Slic3r/App/Desktop/GUI_Descriptions.hpp
    src/Slic3r/App/Desktop/SingleInstanceCheckerFactory.hpp
    src/Slic3r/App/Desktop/AppInstanceCheck.hpp
    src/Slic3r/App/Desktop/AppInstanceCheck.cpp
    src/Slic3r/App/Desktop/SecretStoreFactory.hpp
    src/Slic3r/App/Desktop/Mouse3D/Mouse3DBackend.hpp
)

if(WIN32)
    list(APPEND SLIC3R_APP_DESKTOP_FILES
		src/Slic3r/App/Desktop/SingleInstanceCheckerFactoryWin32.cpp
        src/Slic3r/App/Desktop/SecretStoreFactoryWX.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DBackendWin32.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DDevices.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DDevices.hpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.hpp
    )
elseif(APPLE)
     list(APPEND SLIC3R_APP_DESKTOP_FILES
        src/Slic3r/App/Desktop/SingleInstanceCheckerFactoryLock.cpp
        src/Slic3r/App/Desktop/SecretStoreFactoryWX.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DBackendMac.cpp
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
     list(APPEND SLIC3R_APP_DESKTOP_FILES
        src/Slic3r/App/Desktop/SingleInstanceCheckerFactoryLock.cpp
        src/Slic3r/App/Desktop/SecretStoreFactoryLinux.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DDevices.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DDevices.hpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.cpp
        src/Slic3r/App/Desktop/Mouse3D/Mouse3DHidReader.hpp
    )
endif()

add_library(slic3r-app-desktop STATIC ${SLIC3R_APP_DESKTOP_FILES})

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT slic3r-app-desktop)

find_package(NanoSVG REQUIRED)

target_link_libraries(slic3r-app-desktop PUBLIC
    slic3r-shared-wx
)

target_include_directories(slic3r-app-desktop PUBLIC include PRIVATE src)

# See https://github.com/prusa3d/PrusaSlicer/issues/4584.
if (UNIX AND NOT APPLE)
    find_package(GTK3 REQUIRED)
    target_link_libraries(slic3r-app-desktop PUBLIC GTK3::GTK3)
endif ()

if (SLIC3R_ENABLE_WEBKIT)
   target_compile_definitions(slic3r-app-desktop PRIVATE SLIC3R_HAS_WEBKIT) 
endif()

# hidapi reads the 3D mouse, macOS uses the 3Dconnexion driver instead. With libspnav, Linux
# prefers the spacenavd daemon and uses hidapi when spacenavd does not provide the device.
if (WIN32)
    find_package(hidapi CONFIG REQUIRED)
    slic3r_remap_configs(hidapi::winapi RelWithDebInfo Release)
    target_link_libraries(slic3r-app-desktop PRIVATE hidapi::hidapi)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HIDAPI IMPORTED_TARGET hidapi-hidraw)
    if (NOT HIDAPI_FOUND)
        pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-libusb)
    endif ()
    target_link_libraries(slic3r-app-desktop PRIVATE PkgConfig::HIDAPI)

    pkg_check_modules(SPNAV IMPORTED_TARGET spnav>=1.0)
    if (SPNAV_FOUND)
        target_sources(slic3r-app-desktop PRIVATE
                src/Slic3r/App/Desktop/Mouse3D/Mouse3DBackendLinuxSpacenav.cpp
                src/Slic3r/App/Desktop/Mouse3D/Mouse3DSpacenavReader.cpp
                src/Slic3r/App/Desktop/Mouse3D/Mouse3DSpacenavReader.hpp
        )
        target_link_libraries(slic3r-app-desktop PRIVATE PkgConfig::SPNAV)
    else ()
        target_sources(slic3r-app-desktop PRIVATE src/Slic3r/App/Desktop/Mouse3D/Mouse3DBackendLinux.cpp)
        message(STATUS "libspnav not found, the 3D mouse is read through hidapi only")
    endif ()

    if (SLIC3R_FHS)
        install(FILES ${CMAKE_SOURCE_DIR}/src/platform/unix/90-3dconnexion.rules DESTINATION lib/udev/rules.d)
    endif ()
endif ()
