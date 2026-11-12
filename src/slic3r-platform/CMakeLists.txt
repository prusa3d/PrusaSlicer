set(SLIC3R_PLATFORM_FILES
    include/Slic3r/App/Platform/AbstractRenderModule.hpp
    src/Slic3r/App/Platform/AbstractRenderModule.cpp
    include/Slic3r/App/Platform/AbstractRenderCanvas.hpp
    src/Slic3r/App/Platform/AbstractRenderCanvas.cpp
    include/Slic3r/App/Platform/MouseEvent.hpp
    include/Slic3r/App/Platform/KeyboardEvent.hpp
    include/Slic3r/App/Platform/PlatformError.hpp
    include/Slic3r/App/Platform/KeyModifers.hpp
    include/Slic3r/App/Platform/KeyCode.hpp
    src/Slic3r/App/Platform/KeyCode.cpp
    include/Slic3r/App/Platform/KeyboardShortcut.hpp
    src/Slic3r/App/Platform/KeyboardShortcut.cpp
    include/Slic3r/App/Platform/ICommand.hpp
    include/Slic3r/App/Platform/CommandRegistry.hpp
    src/Slic3r/App/Platform/CommandRegistry.cpp
    include/Slic3r/App/Platform/CommandName.hpp
    src/Slic3r/App/Platform/ICommand.cpp
    include/Slic3r/App/Platform/IFileExplorerHandler.hpp
    include/Slic3r/App/Platform/IFileExplorerErrorListener.hpp
    include/Slic3r/Biz/Platform/IMainThreadDispatcher.hpp
    include/Slic3r/Biz/Platform/IRenderRequestHandler.hpp
    include/Slic3r/Biz/Platform/IMainWindowHandler.hpp
    src/Slic3r/App/Platform/StdMainThreadDispatcher.cpp
    include/Slic3r/App/Platform/StdMainThreadDispatcher.hpp
    include/Slic3r/App/Platform/CameraSynchData.hpp
    include/Slic3r/App/Platform/AnimationManager.hpp
    src/Slic3r/App/Platform/AnimationManager.cpp
    include/Slic3r/App/Platform/AbstractAnimation.hpp
    src/Slic3r/App/Platform/AbstractAnimation.cpp
    src/Slic3r/Biz/Platform/PlatformServices.cpp
    include/Slic3r/Biz/Platform/PlatformServices.hpp
    include/Slic3r/Biz/Platform/ListenerList.hpp
    include/Slic3r/Biz/Platform/Termination.hpp
    src/Slic3r/Biz/Platform/Termination.cpp
    include/Slic3r/Biz/Platform/TimerQueue.hpp
    src/Slic3r/Biz/Platform/TimerQueue.cpp
    include/Slic3r/Biz/Platform/ISecretStore.hpp
    include/Slic3r/Biz/Platform/ISingleInstanceChecker.hpp
    include/Slic3r/Biz/Platform/ListenerScope.hpp
    include/Slic3r/Biz/Platform/WithListeners.hpp
    include/Slic3r/Biz/Platform/JobManager/ProgressTracker.hpp
    src/Slic3r/Biz/Platform/JobManager/ProgressTracker.cpp
    include/Slic3r/Biz/Platform/JobManager/Job.hpp
    include/Slic3r/Biz/Platform/JobManager/JobManager.hpp
    include/Slic3r/Biz/Platform/JobManager/IJobManagerStatusChangedListener.hpp
    include/Slic3r/Biz/Platform/BlacklistedLibraryCheck.hpp
    src/Slic3r/Biz/Platform/BlacklistedLibraryCheck.cpp
    include/Slic3r/Biz/Platform/IAppConfigProvider.hpp
    include/Slic3r/App/Platform/CommandExecutionNotifier.hpp
    src/Slic3r/App/Platform/CommandExecutionNotifier.cpp
    include/Slic3r/Biz/Platform/IAppInstanceMessageHandler.hpp
    include/Slic3r/Biz/Platform/IAppInstanceMessageContentListener.hpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	list(APPEND SLIC3R_PLATFORM_FILES
        include/Slic3r/Biz/Platform/SecretStoreLinux.hpp
        src/Slic3r/Biz/Platform/SecretStoreLinux.cpp
    )
endif ()

add_library(slic3r-platform STATIC ${SLIC3R_PLATFORM_FILES})

target_include_directories(slic3r-platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(slic3r-platform PUBLIC slic3r-render slic3r-jthread)


# Doxygen
add_target_to_doxygen(slic3r-platform)
slic3r_add_tracy(slic3r-platform)

if(SLIC3R_BUILD_TESTS)
    add_executable(slic3r-platform-tests
        test/Slic3r/Biz/Platform/JobManager/JobManagerTests.cpp
        test/Slic3r/Biz/Platform/ListenerListTests.cpp
    )
    target_include_directories(slic3r-platform-tests PRIVATE test src)
    target_link_libraries(slic3r-platform-tests PRIVATE slic3r-platform Catch2::Catch2WithMain)

    add_test(NAME slic3r-platform-tests
        COMMAND slic3r-platform-tests ${CATCH_ALL_ARGS}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
