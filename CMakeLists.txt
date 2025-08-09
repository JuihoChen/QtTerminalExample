cmake_minimum_required(VERSION 3.10)
project(QtTerminalExample)

# This will set the default build type to Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the build type." FORCE)
endif()

# This will add the -g flag for debug symbols
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

add_executable(QtTerminalExample
    main.cpp
    terminalwindow.cpp
    connectiondialog.cpp
)

# Try to find qtermwidget, fallback to manual path
find_path(QTERMWIDGET_INCLUDE_DIR qtermwidget.h
    PATHS /usr/include/qtermwidget5 /usr/local/include/qtermwidget5
)

if(QTERMWIDGET_INCLUDE_DIR)
    target_include_directories(QtTerminalExample PRIVATE ${QTERMWIDGET_INCLUDE_DIR})
else()
    target_include_directories(QtTerminalExample PRIVATE /usr/include/qtermwidget5)
endif()

target_link_libraries(QtTerminalExample Qt5::Widgets qtermwidget5)