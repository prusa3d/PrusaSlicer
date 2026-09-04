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
)

if(WIN32)
    list(APPEND SLIC3R_APP_DESKTOP_FILES
		src/Slic3r/App/Desktop/SingleInstanceCheckerFactoryWin32.cpp
        src/Slic3r/App/Desktop/SecretStoreFactoryWX.cpp
    )
elseif(APPLE)
     list(APPEND SLIC3R_APP_DESKTOP_FILES
        src/Slic3r/App/Desktop/SingleInstanceCheckerFactoryLock.cpp
        src/Slic3r/App/Desktop/SecretStoreFactoryWX.cpp
    )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
     list(APPEND SLIC3R_APP_DESKTOP_FILES
        src/Slic3r/App/Desktop/SingleInstanceCheckerFactoryLock.cpp
        src/Slic3r/App/Desktop/SecretStoreFactoryLinux.cpp
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
