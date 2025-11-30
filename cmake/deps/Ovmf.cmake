# Define where to save the file (Cache directory is safer than build dir for persistence)
set(OVMF_INSTALL_DIR "${CMAKE_SOURCE_DIR}/cache/ovmf" CACHE PATH "Directory to store OVMF binaries")

# Map Architecture to filename

set(EDK2_NIGHTLY_URL "https://raw.githubusercontent.com/retrage/edk2-nightly/refs/heads/master/bin/")

# Select filename based on the repository's naming convention
if(${${PROJECT_NAME}_ARCHITECTURE} STREQUAL "x86_64")
    # Monolithic file (contains CODE + VARS)
    set(OVMF_FILENAME "RELEASEX64_OVMF.fd")
else()
    message(FATAL_ERROR "No OVMF mapping for ${${PROJECT_NAME}_ARCHITECTURE}")
endif()

# Full local path where the file will be saved
set(OVMF_LOCAL_PATH "${OVMF_INSTALL_DIR}/${OVMF_FILENAME}")

# Download and Verify
if(EXISTS "${OVMF_LOCAL_PATH}")
    message(STATUS "OVMF binary already exists at: ${OVMF_LOCAL_PATH}")
else()
    message(STATUS "Downloading ${OVMF_FILENAME} from edk2-nightly...")

    file(
        DOWNLOAD
        "${EDK2_NIGHTLY_URL}/${OVMF_FILENAME}"
        "${OVMF_LOCAL_PATH}"
        # SHOW_PROGRESS
        TIMEOUT 60
        STATUS DOWNLOAD_STATUS
        LOG DOWNLOAD_LOG
    )

    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    list(GET DOWNLOAD_STATUS 1 STATUS_MSG)

    if(NOT STATUS_CODE EQUAL 0)
        file(REMOVE "${OVMF_LOCAL_PATH}") # Clean up partial file
        message(FATAL_ERROR "Download failed: ${STATUS_MSG}\nLog: ${DOWNLOAD_LOG}")
    else()
        message(STATUS "Successfully downloaded to: ${OVMF_LOCAL_PATH}")
    endif()
endif()

# Expose Variable for other targets
set(OVMF_BINARY_PATH "${OVMF_LOCAL_PATH}" CACHE FILEPATH "Path to the downloaded OVMF binary" FORCE)