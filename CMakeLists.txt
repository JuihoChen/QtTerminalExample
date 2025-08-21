cmake_minimum_required(VERSION 3.10)
project(QtTerminalExample)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the build type." FORCE)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5 COMPONENTS Core Widgets Gui REQUIRED)

option(USE_STATIC_QTERMWIDGET "Use static QTermWidget with fixes" ON)

add_executable(QtTerminalExample
    main.cpp
    terminalwindow.cpp
    connectiondialog.cpp
    enhanced_qtermwidget.cpp
)

if(USE_STATIC_QTERMWIDGET)
    message(STATUS "Using static QTermWidget with fixes")
 
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/qtermwidget/CMakeLists.txt")
        add_subdirectory(qtermwidget)
        
        # This is the crucial fix.
        # It adds the build directory of the sub-project to the include paths.
        target_include_directories(QtTerminalExample PRIVATE
            qtermwidget/lib  
        )

        set(QTERMWIDGET_LIBRARY qtermwidget5)
        
    else()
        message(FATAL_ERROR "qtermwidget subdirectory not found! Please run: git clone https://github.com/lxqt/qtermwidget.git")
    endif()
    
else()
    message(STATUS "Using system QTermWidget library")
    
    link_directories(/usr/local/lib)
    
    find_path(QTERMWIDGET_INCLUDE_DIR qtermwidget.h
        PATHS /usr/include/qtermwidget5 /usr/local/include/qtermwidget5
    )
    
    if(QTERMWIDGET_INCLUDE_DIR)
        target_include_directories(QtTerminalExample PRIVATE ${QTERMWIDGET_INCLUDE_DIR})
    else()
        target_include_directories(QtTerminalExample PRIVATE /usr/include/qtermwidget5)
    endif()
    
    set(QTERMWIDGET_LIBRARY qtermwidget5)
endif()

target_link_libraries(QtTerminalExample 
    PRIVATE
    Qt5::Core
    Qt5::Widgets 
    Qt5::Gui
    # Link directly to the target, which resolves dependencies automatically
    ${QTERMWIDGET_LIBRARY}
)

if(UNIX AND NOT APPLE)
    target_link_libraries(QtTerminalExample PRIVATE dl util)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Using QTermWidget library: ${QTERMWIDGET_LIBRARY}")
if(USE_STATIC_QTERMWIDGET)
    message(STATUS "QTermWidget source location: ${CMAKE_CURRENT_SOURCE_DIR}/qtermwidget")
endif()