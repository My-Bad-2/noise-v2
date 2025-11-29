include_guard()

set(
    ${PROJECT_NAME}_CX_WARNING_FLAGS
    # Disabled Warnings
	"-Wno-unknown-pragmas" # Some compilers complain about our use of #pragma mark
	"-Wno-c++98-compat"
	"-Wno-c++98-compat-pedantic"
	"-Wno-padded"
	"-Wno-covered-switch-default"
	# Desired Warnings
	"-Wfloat-equal"
	"-Wconversion"
	"-Wlogical-op"
	"-Wundef"
	"-Wredundant-decls"
	"-Wshadow"
	"-Wstrict-overflow=2"
	"-Wwrite-strings"
	"-Wpointer-arith"
	"-Wcast-qual"
	"-Wformat=2"
	"-Wformat-truncation"
	"-Wmissing-include-dirs"
	"-Wcast-align"
	"-Wswitch-enum"
	"-Wsign-conversion"
	"-Wdisabled-optimization"
	"-Winvalid-pch"
	"-Wmissing-declarations"
	"-Wdouble-promotion"
	"-Wshadow"
	"-Wtrampolines"
	"-Wvector-operation-performance"
	"-Wshift-overflow=2"
	"-Wnull-dereference"
	"-Wduplicated-cond"
	"-Wshift-overflow=2"
	"-Wnull-dereference"
	"-Wduplicated-cond"
	"-Wcast-align=strict"
)

set(
    ${PROJECT_NAME}_CXX_WARNING_FLAGS
    "-Wold-style-cast"
	"-Wnon-virtual-dtor"
	"-Wctor-dtor-privacy"
	"-Woverloaded-virtual"
	"-Wnoexcept"
	"-Wstrict-null-sentinel"
	"-Wuseless-cast"
	"-Wzero-as-null-pointer-constant"
	"-Wextra-semi"
)

set(
    ${PROJECT_NAME}_CX_FLAGS
    "-ffreestanding"
    "-nostdinc"
	"-nostdlib"
	"-nostartfiles"
    "-fno-stack-protector"
    "-fstrict-vtable-pointers"
    "-funsigned-char"
    "-mgeneral-regs-only"
    "-mno-red-zone"
	"-static"	
)

set(
    ${PROJECT_NAME}_CXX_FLAGS
    "-fno-rtti"
    "-fno-exceptions"
    "-fsized-deallocation"
    "-fcheck-new"
)

set(
    ${PROJECT_NAME}_LINK_FLAGS
    "-Wl,--gc-sections"
	"-Wl,--nostdlib"
    "-Wl,--static"
    "-Wl,-znoexecstack"
    "-Wl,-zmax-page-size=0x1000"
)

set(
	${PROJECT_NAME}_CX_DEFINES
	"-DLIMINE_API_REVISION=${${PROJECT_NAME}_LIMINE_API_REV}"
)

if(CMAKE_BUILD_TYPE MATCHES "^(Debug|ReleaseDbg)$")
	list(
		APPEND
		${PROJECT_NAME}_CX_DEFINES
		"-DNOISE_DEBUG=1"
	)
else()
	list(
		APPEND
		${PROJECT_NAME}_CX_DEFINES
		"-DNOISE_DEBUG=0"
		"-DNDEBUG=1"
	)
endif()

if(${PROJECT_NAME}_ARCHITECTURE STREQUAL "x86_64")
	list(
		APPEND
		${PROJECT_NAME}_CX_FLAGS
		"-march=x86-64"
        "-mno-red-zone"
        "-mno-mmx"
        "-mno-sse"
        "-mno-sse2"
        "-mno-80387"
        "-mno-x87"
        "-mcmodel=kernel"
		"-mstack-alignment=8"
	)
else()
	message(FATAL_ERROR "Unsupported ${PROJECT_NAME} Architecture: '${${PROJECT_NAME}_ARCHITECTURE}'")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	list(
		APPEND
		${PROJECT_NAME}_LINK_FLAGS
		"-Wl,--strip-debug"
	)
endif()