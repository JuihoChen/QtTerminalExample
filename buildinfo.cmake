# Runs on every build (see the buildinfo target in CMakeLists.txt) and writes
# buildinfo.cpp with the current git commit and build time. configure_file
# leaves the file alone when nothing changed, so only it gets recompiled.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_COMMIT)
    execute_process(
        COMMAND git status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_CHANGES
        ERROR_QUIET
    )
    if(GIT_CHANGES)
        set(GIT_COMMIT "${GIT_COMMIT}-modified")
    endif()
endif()
string(TIMESTAMP BUILD_TIME "%Y-%m-%d %H:%M")
configure_file("${SOURCE_DIR}/buildinfo.cpp.in" "${OUTPUT_FILE}" @ONLY)
