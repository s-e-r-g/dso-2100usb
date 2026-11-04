cmake_minimum_required(VERSION 3.21)
project(dso2100scope VERSION 0.1.0 LANGUAGES CXX)

# DSO-2100 Scope: Qt 6 GUI for the Hantek/Voltcraft DSO-2100 USB oscilloscope.
# Layers (CODING_STYLE.md): core (portable, std only) -> hw (libusb) -> app (QtCore) -> gui.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DSO_WERROR "Treat warnings as errors" OFF)
option(DSO_TESTS "Build the unit tests (GoogleTest)" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)
    pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
endif()
if(NOT LIBUSB_FOUND)
    # Windows: libusb from the official release archive, LIBUSB_ROOT=<extracted dir>
    find_path(LIBUSB_INCLUDE_DIR libusb.h PATH_SUFFIXES include/libusb-1.0 libusb-1.0 HINTS ${LIBUSB_ROOT})
    find_library(LIBUSB_LIBRARY NAMES libusb-1.0.a usb-1.0 libusb-1.0 PATH_SUFFIXES MinGW64/static lib HINTS ${LIBUSB_ROOT})
    if(NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
        message(FATAL_ERROR "libusb-1.0 not found (install libusb-1.0-0-dev, or set LIBUSB_ROOT)")
    endif()
    add_library(PkgConfig::LIBUSB UNKNOWN IMPORTED)
    set_target_properties(PkgConfig::LIBUSB PROPERTIES
        IMPORTED_LOCATION ${LIBUSB_LIBRARY}
        INTERFACE_INCLUDE_DIRECTORIES ${LIBUSB_INCLUDE_DIR})
endif()

set(DSO_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wold-style-cast -Wnon-virtual-dtor)
if(DSO_WERROR)
    list(APPEND DSO_WARNINGS -Werror)
endif()

# ---------- core + hw: no Qt, unit-testable ----------
add_library(dso_core STATIC
    src/core/Fft.cpp
    src/core/Format.cpp
    src/core/I2c.cpp
    src/core/IntelHex.cpp
    src/core/Measure.cpp
    src/core/Scope.cpp
    src/core/Trigger.cpp
    src/hw/DemoBackend.cpp
    src/hw/Dso2100Usb.cpp
)
target_include_directories(dso_core PUBLIC src)
target_compile_options(dso_core PRIVATE ${DSO_WARNINGS})
target_link_libraries(dso_core PUBLIC PkgConfig::LIBUSB)

# ---------- app + gui ----------
set(CMAKE_AUTOMOC ON)
qt_add_executable(dso2100scope WIN32
    src/main.cpp
    src/app/Acquisition.cpp
    src/app/Acquisition.hpp
    src/app/Processor.cpp
    src/gui/Icons.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.hpp
    src/gui/RecordBar.cpp
    src/gui/RecordBar.hpp
    src/gui/ScopeView.cpp
    src/gui/ScopeView.hpp
    src/gui/Theme.cpp
    src/gui/Widgets.cpp
    src/gui/Widgets.hpp
)
qt_add_resources(dso2100scope firmware
    PREFIX /firmware
    BASE drivers
    FILES drivers/dso2100-firmware.hex
)
qt_add_resources(dso2100scope icons
    PREFIX /icons
    BASE src/gui/icons
    FILES src/gui/icons/arrow-down.png src/gui/icons/arrow-up.png
)
target_compile_definitions(dso2100scope PRIVATE DSO_VERSION="${PROJECT_VERSION}")
target_compile_options(dso2100scope PRIVATE ${DSO_WARNINGS})
target_link_libraries(dso2100scope PRIVATE dso_core Qt6::Widgets)
if(WIN32)
    include(cmake/DeployWindows.cmake)
endif()

# console diagnostic: raw captures without the GUI (tools/diag)
add_executable(dso2100diag tools/diag/DsoDiag.cpp)
target_compile_options(dso2100diag PRIVATE ${DSO_WARNINGS})
target_link_libraries(dso2100diag PRIVATE dso_core)

install(TARGETS dso2100scope dso2100diag RUNTIME DESTINATION bin)

# ---------- tests ----------
if(DSO_TESTS AND NOT CMAKE_CROSSCOMPILING)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        enable_testing()
        add_executable(dso_tests
            tests/core/FftTest.cpp
            tests/core/FormatTest.cpp
            tests/core/I2cTest.cpp
            tests/core/IntelHexTest.cpp
            tests/core/MeasureTest.cpp
            tests/core/TriggerTest.cpp
            tests/app/ProcessorTest.cpp
            src/app/Processor.cpp
        )
        target_compile_options(dso_tests PRIVATE ${DSO_WARNINGS})
        target_compile_definitions(dso_tests PRIVATE DSO_FIRMWARE_HEX="${CMAKE_SOURCE_DIR}/drivers/dso2100-firmware.hex")
        target_link_libraries(dso_tests PRIVATE dso_core Qt6::Core GTest::gtest_main)
        include(GoogleTest)
        gtest_discover_tests(dso_tests)
    else()
        message(STATUS "GoogleTest not found: unit tests skipped")
    endif()
endif()

# ---------- format / tidy ----------
file(GLOB_RECURSE DSO_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.hpp tests/*.cpp)
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format COMMAND ${CLANG_FORMAT} -i ${DSO_SOURCES} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_custom_target(format-check COMMAND ${CLANG_FORMAT} --dry-run --Werror ${DSO_SOURCES} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
find_program(CLANG_TIDY clang-tidy)
if(CLANG_TIDY)
    file(GLOB_RECURSE DSO_CPP CONFIGURE_DEPENDS src/*.cpp)
    add_custom_target(tidy COMMAND ${CLANG_TIDY} -p ${CMAKE_BINARY_DIR} --quiet ${DSO_CPP} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
