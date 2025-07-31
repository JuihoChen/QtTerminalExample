cmake_minimum_required(VERSION 3.10)
project(QtTerminalExample)

set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Widgets REQUIRED)

add_executable(QtTerminalExample main.cpp)

# Add qtermwidget include directory manually
target_include_directories(QtTerminalExample PRIVATE /usr/include/qtermwidget5)

# Link qtermwidget5 and Qt5 Widgets
target_link_libraries(QtTerminalExample Qt5::Widgets qtermwidget5)

