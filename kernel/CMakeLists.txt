set(
    KERNEL_CX_FLAGS
    "-fno-pic"
    "-fno-pie"
)

set(KERNEL_LINK_SCRIPT ${CMAKE_SOURCE_DIR}/misc/linker/${${PROJECT_NAME}_ARCHITECTURE}.ld)

collect_sources_filtered(
    VAR KERNEL_SOURCES
    ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src
    ARCH_DIR arch
    CURRENT_ARCH ${${PROJECT_NAME}_ARCHITECTURE}
    DEBUG OFF
)

add_executable(
    kernel
    ${KERNEL_SOURCES}
)

target_apply_compile_settings(
    TARGET kernel
    LANG "C;CXX"
    FLAGS ${${PROJECT_NAME}_CX_FLAGS} ${KERNEL_CX_FLAGS} ${${PROJECT_NAME}_CX_DEFINES} ${${PROJECT_NAME}_CX_WARNING_FLAGS}
)

target_apply_compile_settings(
    TARGET kernel
    LANG CXX
    FLAGS ${${PROJECT_NAME}_CXX_FLAGS} ${${PROJECT_NAME}_CXX_WARNING_FLAGS}
)

target_apply_linker_settings(
    TARGET kernel
    LANG "C;CXX"
    SCRIPT ${KERNEL_LINK_SCRIPT}
    SKIP_CHECK ON
    FLAGS ${${PROJECT_NAME}_LINK_FLAGS}
)