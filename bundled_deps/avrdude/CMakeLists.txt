cmake_minimum_required(VERSION 3.0)

add_definitions(-D_BSD_SOURCE -D_DEFAULT_SOURCE)   # To enable various useful macros and functions on Unices
remove_definitions(-D_UNICODE -DUNICODE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Workaround for an old CMake, which does not understand CMAKE_C_STANDARD.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99 -Wall")
endif()


set(AVRDUDE_SOURCES
    avrdude/arduino.c
    avrdude/avr.c
    # avrdude/avrftdi.c
    # avrdude/avrftdi_tpi.c
    avrdude/avrpart.c
    avrdude/avr910.c
    avrdude/bitbang.c
    avrdude/buspirate.c
    avrdude/butterfly.c
    avrdude/config.c
    avrdude/config_gram.c
    # avrdude/confwin.c
    avrdude/crc16.c
    # avrdude/dfu.c
    avrdude/fileio.c
    # avrdude/flip1.c
    # avrdude/flip2.c
    # avrdude/ft245r.c
    # avrdude/jtagmkI.c
    # avrdude/jtagmkII.c
    # avrdude/jtag3.c
    avrdude/lexer.c
    avrdude/linuxgpio.c
    avrdude/lists.c
    # avrdude/par.c
    avrdude/pgm.c
    avrdude/pgm_type.c
    avrdude/pickit2.c
    avrdude/pindefs.c
    # avrdude/ppi.c
    # avrdude/ppiwin.c
    avrdude/safemode.c
    avrdude/ser_avrdoper.c
    avrdude/serbb_posix.c
    avrdude/serbb_win32.c
    avrdude/ser_posix.c
    avrdude/ser_win32.c
    avrdude/stk500.c
    avrdude/stk500generic.c
    avrdude/stk500v2.c
    avrdude/term.c
    avrdude/update.c
    # avrdude/usbasp.c
    # avrdude/usb_hidapi.c
    # avrdude/usb_libusb.c
    # avrdude/usbtiny.c
    avrdude/wiring.c

    avrdude/main.c
    avrdude/avrdude-slic3r.hpp
    avrdude/avrdude-slic3r.cpp
)
if (MSVC)
    set(AVRDUDE_SOURCES ${AVRDUDE_SOURCES}
        avrdude/windows/utf8.c
        avrdude/windows/unistd.cpp
        avrdude/windows/getopt.c
    )
elseif (MINGW)
    set(AVRDUDE_SOURCES ${AVRDUDE_SOURCES}
        avrdude/windows/utf8.c
    )
endif()

include(bin2h)

bin2h(
    SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/avrdude/avrdude-slic3r.conf
    VARIABLE_NAME avrdude_slic3r_conf
    HEADER_FILE ${CMAKE_CURRENT_BINARY_DIR}/avrdude-slic3r.conf.h
    ADD_WARNING_TEXT
)

add_library(avrdude STATIC ${AVRDUDE_SOURCES})
target_link_libraries(avrdude PRIVATE localesutils)

add_executable(avrdude-slic3r avrdude/main-standalone.cpp)
target_link_libraries(avrdude-slic3r avrdude)

encoding_check(avrdude)
encoding_check(avrdude-slic3r)

# Make avrdude-slic3r.conf.h includable:
target_include_directories(avrdude SYSTEM PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(avrdude PUBLIC .)

if (WIN32)
    target_compile_definitions(avrdude PRIVATE WIN32NATIVE=1)
    if(MSVC)
        target_include_directories(avrdude SYSTEM PRIVATE avrdude/windows)    # So that sources find the getopt.h windows drop-in
    endif(MSVC)
endif()
