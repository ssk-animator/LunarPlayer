# Usage: cmake -D "SRC=path/to/source.dll" -D "DST=path/to/dest.dll" -P copy_if_exists.cmake
# Copies SRC to DST only if SRC exists. Returns 0 (success) even if SRC is missing.

if(NOT DEFINED SRC)
    message(FATAL_ERROR "copy_if_exists.cmake: SRC not specified")
endif()
if(NOT DEFINED DST)
    message(FATAL_ERROR "copy_if_exists.cmake: DST not specified")
endif()

if(EXISTS "${SRC}")
    configure_file("${SRC}" "${DST}" COPYONLY)
    message(STATUS "Copied: ${SRC} -> ${DST}")
else()
    message(STATUS "Source not found (skipping): ${SRC}")
endif()
