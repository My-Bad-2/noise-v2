set(KLIBC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
collect_sources_filtered(
    VAR KLIBC_SOURCES
    ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src
    ARCH_DIR arch
    CURRENT_ARCH ${${PROJECT_NAME}_ARCHITECTURE}
    DEBUG OFF
)

add_library(
    klibc
    STATIC
    ${KLIBC_SOURCES}
)

target_include_directories(klibc PUBLIC ${KLIBC_INCLUDE_DIR})

target_apply_compile_settings(
    TARGET klibc
    LANG "C;CXX"
    FLAGS ${${PROJECT_NAME}_CX_FLAGS} ${${PROJECT_NAME}_CX_DEFINES} ${${PROJECT_NAME}_CX_WARNING_FLAGS}
)

target_apply_compile_settings(
    TARGET klibc
    LANG CXX
    FLAGS ${${PROJECT_NAME}_CXX_FLAGS} ${${PROJECT_NAME}_CXX_WARNING_FLAGS}
)
