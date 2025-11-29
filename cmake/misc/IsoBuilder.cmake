function(build_iso)
    set(oneValueArgs STAGING_DIR TRIGGER_NAME)
    set(multiValueArgs XORRISO_FLAGS)
    cmake_parse_arguments(ARG "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(ISO_OUTPUT "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.iso")
    
    find_program(XORRISO_CMD xorriso REQUIRED)

    if(NOT XORRISO_CMD)
        message(FATAL "Xorriso not found!")
    endif()

    add_custom_command(
        OUTPUT ${ISO_OUTPUT}
        COMMAND ${XORRISO_CMD} 
                -as mkisofs 
                -o ${ISO_OUTPUT} 
                ${ARG_XORRISO_FLAGS} 
                ${ARG_STAGING_DIR}
        
        # We assume the directory content is managed by an external target
        # or implies a dependency on the staging folder timestamp
        COMMENT "Packing ${ARG_STAGING_DIR} into ${ISO_OUTPUT}..."
        VERBATIM
    )

    # Trigger
    add_custom_target(${ARG_TRIGGER_NAME}
        DEPENDS ${ISO_OUTPUT}
    )
    
    message(STATUS "Optional Target '${ARG_TRIGGER_NAME}' registered for ${ISO_OUTPUT}")
endfunction()