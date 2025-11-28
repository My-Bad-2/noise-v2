include_guard()

# Include built-in CMake modules
include(CheckLinkerFlag)
include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CMakeParseArguments)

# ==============================================================================
# Function: target_apply_linker_flags
# Description: Checks and applies linker flags and scripts.
# ==============================================================================
function(target_apply_linker_flags)
    set(options "")
    set(oneValueArgs TARGET SCRIPT LANG)
    set(multiValueArgs FLAGS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "target_apply_linker_flags: TARGET argument is required.")
    endif()

    if(NOT ARG_LANG)
        set(ARG_LANG "C")
    endif()

    # 1. Check and Apply Flags
    foreach(FLAG IN LISTS ARG_FLAGS)
        string(MAKE_C_IDENTIFIER "LINKER_SUPPORT_${FLAG}" CHECK_VAR)
        check_linker_flag(${ARG_LANG} "${FLAG}" ${CHECK_VAR})

        if(${${CHECK_VAR}})
            message(STATUS "Linker supports ${FLAG}: YES")
            target_link_options(${ARG_TARGET} PRIVATE ${FLAG})
        else()
            message(WARNING "Linker supports ${FLAG}: NO (Skipping)")
        endif()
    endforeach()

    # 2. Handle Custom Linker Script
    if(ARG_SCRIPT)
        get_filename_component(SCRIPT_ABS_PATH "${ARG_SCRIPT}" ABSOLUTE)
        if(EXISTS "${SCRIPT_ABS_PATH}")
            message(STATUS "Applying custom linker script: ${SCRIPT_ABS_PATH}")
            target_link_options(${ARG_TARGET} PRIVATE "-T${SCRIPT_ABS_PATH}")
            set_target_properties(${ARG_TARGET} PROPERTIES LINK_DEPENDS "${SCRIPT_ABS_PATH}")
        else()
            message(FATAL_ERROR "Linker script not found at: ${SCRIPT_ABS_PATH}")
        endif()
    endif()
endfunction()

# ==============================================================================
# Function: target_apply_compile_settings
# Description: Checks if compile flags are supported and applies them.
# Arguments:
#   TARGET  (Required) : The target name
#   FLAGS   (Optional) : List of flags (e.g. -Wall -Wextra)
#   LANG    (Optional) : C or CXX (default: C)
# ==============================================================================
function(target_apply_compile_settings)
    set(options "")
    set(oneValueArgs TARGET LANG)
    set(multiValueArgs FLAGS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_TARGET)
        message(FATAL_ERROR "target_apply_compile_settings: TARGET argument is required.")
    endif()

    if(NOT ARG_LANG)
        set(ARG_LANG "C")
    endif()

    foreach(FLAG IN LISTS ARG_FLAGS)
        # Sanitize flag for variable name
        string(MAKE_C_IDENTIFIER "COMPILER_SUPPORT_${ARG_LANG}_${FLAG}" CHECK_VAR)

        # Branch check based on language
        if(ARG_LANG STREQUAL "C")
            check_c_compiler_flag("${FLAG}" ${CHECK_VAR})
        elseif(ARG_LANG STREQUAL "CXX")
            check_cxx_compiler_flag("${FLAG}" ${CHECK_VAR})
        else()
            message(WARNING "target_apply_compile_settings: Unsupported LANG '${ARG_LANG}'. Skipping flag check.")
            set(${CHECK_VAR} FALSE)
        endif()

        # Apply if supported
        if(${${CHECK_VAR}})
            message(STATUS "Compiler (${ARG_LANG}) supports ${FLAG}: YES")
            target_compile_options(${ARG_TARGET} PRIVATE ${FLAG})
        else()
            message(WARNING "Compiler (${ARG_LANG}) supports ${FLAG}: NO (Skipping)")
        endif()
    endforeach()
endfunction()