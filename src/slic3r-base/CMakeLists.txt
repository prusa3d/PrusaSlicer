set(SLIC3R_BASE_FILES
		include/Slic3r/Assert.hpp
		src/Slic3r/Assert.cpp
		include/Slic3r/Log.hpp
		include/Slic3r/LogIncludeSPD.hpp
		src/Slic3r/Log.cpp
		include/Slic3r/PlatformInfo.hpp
		src/Slic3r/PlatformInfo.cpp
		include/Slic3r/TypeInfo.hpp
		src/Slic3r/TypeInfo.cpp
		include/Slic3r/IdGenerator.hpp
        include/Slic3r/Exception.hpp
        include/Slic3r/Math.hpp
        include/Slic3r/Semver.hpp
        src/Slic3r/Semver.cpp
        include/Slic3r/Platform.hpp
        src/Slic3r/Platform.cpp
        include/Slic3r/Utils.hpp
        src/Slic3r/Utils.cpp
        include/Slic3r/Time.hpp
        src/Slic3r/Time.cpp
        include/Slic3r/Timer.hpp
        src/Slic3r/Timer.cpp
		include/Slic3r/Uuid.hpp
		src/Slic3r/Uuid.cpp
        include/Slic3r/Directories.hpp
        src/Slic3r/Directories.cpp
		include/Slic3r/InvokeLaterBag.hpp
		src/Slic3r/InvokeLaterBag.cpp
        include/Slic3r/LegacyFormat.hpp
)

if (APPLE)
	set(SLIC3R_BASE_FILES
			${SLIC3R_BASE_FILES}
			src/Slic3r/PlatformInfo_osx.mm
			include/Slic3r/DirectoriesMacUtils.hpp
            src/Slic3r/DirectoriesMacUtils.mm
	)
elseif (WIN32)
	set(SLIC3R_BASE_FILES
			${SLIC3R_BASE_FILES}
			src/Slic3r/PlatformInfo_win32.cpp
	)
elseif (EMSCRIPTEN)
	set(SLIC3R_BASE_FILES
			${SLIC3R_BASE_FILES}
			src/Slic3r/PlatformInfo_emscripten.cpp
	)
elseif (UNIX)
	set(SLIC3R_BASE_FILES
			${SLIC3R_BASE_FILES}
			src/Slic3r/PlatformInfo_unix.cpp
	)
endif ()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/Slic3r/Version.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/include/Slic3r/Version.hpp @ONLY)

add_library(slic3r-base STATIC ${SLIC3R_BASE_FILES} ${CMAKE_CURRENT_BINARY_DIR}/include/Slic3r/Version.hpp)
# wxString conversions only with explicit encoding stated for all slic3r-* targets
if ((NOT UNIX) OR APPLE)
	# The wxNO_IMPLICIT_WXSTRING_ENCODING cannot be used along with wxUSE_UTF8_LOCALE_ONLY which gets defined on Linux
	# as part of autoconf generated file (setup.h.in)
	target_compile_definitions(slic3r-base PUBLIC -DwxNO_IMPLICIT_WXSTRING_ENCODING=1)
endif ()

find_package(tl-expected REQUIRED)
find_package(spdlog REQUIRED)
slic3r_remap_configs(spdlog::spdlog RelWithDebInfo Release)
target_include_directories(slic3r-base PUBLIC include ${CMAKE_CURRENT_BINARY_DIR}/include)
target_include_directories(slic3r-base PRIVATE src)
target_link_libraries(slic3r-base PUBLIC
    spdlog::spdlog
    semver
    boost_headeronly
    Boost::filesystem
	tl::expected
    slic3r-domain-types
)

if(NOT EMSCRIPTEN)
	find_package(libassert REQUIRED)
	slic3r_remap_configs("libassert::assert;cpptrace::cpptrace" RelWithDebInfo Release)
	target_link_libraries(slic3r-base PUBLIC libassert::assert)
endif ()

# Doxygen
add_target_to_doxygen(slic3r-base)

if(SLIC3R_BUILD_TESTS)
	add_executable(slic3r-base-tests
			test/Slic3r/InvokeLaterBagTests.cpp
	)
	target_include_directories(slic3r-base-tests PRIVATE test src)
	target_link_libraries(slic3r-base-tests PRIVATE slic3r-base Catch2::Catch2WithMain)

	add_test(NAME slic3r-base-tests
			COMMAND slic3r-base-tests ${CATCH_ALL_ARGS}
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
	)
endif()
