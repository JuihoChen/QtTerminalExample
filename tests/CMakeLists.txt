# Unit tests (Qt Test). Run with "ctest" in the build folder.
# They cover logic that needs no SSH server or display: the GUI-free helpers,
# plus a few widgets on Qt's offscreen platform.

find_package(Qt5 COMPONENTS Test REQUIRED)

# The app sources the tests reach into, built once and shared by all tests
add_library(qtterminal_testable STATIC
    ../hostscantab.cpp
    ../sftpworker.cpp
    ../remotefilemodel.cpp
    ../inwindowdialog.cpp
)
target_include_directories(qtterminal_testable PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(qtterminal_testable PUBLIC
    Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Network Qt5::Test PkgConfig::LIBSSH)

function(add_qt_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE qtterminal_testable)
    add_test(NAME ${name} COMMAND ${name})
    # No display needed; a private HOME keeps tests away from real settings
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${CMAKE_CURRENT_BINARY_DIR}/home-${name};XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/home-${name}/.config"
        TIMEOUT 60)
endfunction()

add_qt_test(tst_commandsafety)
add_qt_test(tst_connections)
add_qt_test(tst_hostscan)
add_qt_test(tst_filebrowser)
add_qt_test(tst_portcombobox)

# Starts the real app on the offscreen platform with an empty config folder
# and checks it is still running a few seconds later (no crash at startup)
add_test(NAME smoke_startup
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/smoke_startup.sh $<TARGET_FILE:QtTerminalExample>)
set_tests_properties(smoke_startup PROPERTIES TIMEOUT 60)
