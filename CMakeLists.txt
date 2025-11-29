cmake_minimum_required(VERSION 4.0.1 FATAL_ERROR)

set(PROJECT_NAME                    "${PARAM_PROJECT_NAME}")
set(${PROJECT_NAME}_SUMMARY         "${PARAM_PROJECT_SUMMARY}")
set(${PROJECT_NAME}_VERSION_MAJOR   "${PARAM_PROJECT_VERSION_MAJOR}")
set(${PROJECT_NAME}_VERSION_MINOR   "${PARAM_PROJECT_VERSION_MINOR}")
set(${PROJECT_NAME}_VERSION_PATCH   "${PARAM_PROJECT_VERSION_PATCH}")
set(${PROJECT_NAME}_VERSION         "${${PROJECT_NAME}_VERSION_MAJOR}.${${PROJECT_NAME}_VERSION_MINOR}.${${PROJECT_NAME}_VERSION_PATCH}")
message(${${PROJECT_NAME}_VERSION})
set(${PROJECT_NAME}_ARCHITECTURE    "${PARAM_PROJECT_ARCHITECTURE}")
set(${PROJECT_NAME}_LIMINE_API_REV  "${PARAM_PROJECT_LIMINE_API_REV}")

project(
    "${PARAM_PROJECT_NAME}"
    VERSION "${${PROJECT_NAME}_VERSION}"
    DESCRIPTION "${${PROJECT_NAME}_SUMMARY}"
    LANGUAGES C CXX ASM
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/compiler/Flags.cmake)
include(cmake/misc/ProjectOptions.cmake)
include(cmake/compiler/Cache.cmake)
include(cmake/compiler/Linker.cmake)
include(cmake/misc/PreventInSourceBuilds.cmake)
include(cmake/misc/CheckAndApplyFlags.cmake)
include(cmake/misc/AddSource.cmake)
include(cmake/misc/Utils.cmake)
include(cmake/docs/Doxygen.cmake)

project_enable_cache()
project_configure_linker()
project_assure_out_of_source_builds()

add_subdirectory(kernel)
