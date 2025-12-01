function(setup_prebuilt_klibc)
    set(LIBC_ARCHITECTURE ${${PROJECT_NAME}_ARCHITECTURE})

    CPMAddPackage(
        NAME klibc
        GIT_REPOSITORY https://github.com/My-Bad-2/baremetal-libc.git
        GIT_TAG "main"
    )
endfunction()