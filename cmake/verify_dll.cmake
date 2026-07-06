# Deployment verification: check that a required DLL exists
# Usage: cmake -D "FILE=path/to/file.dll" -P verify_dll.cmake
# Fails with FATAL_ERROR if the file does not exist.

if(NOT DEFINED FILE)
    message(FATAL_ERROR "verify_dll.cmake: FILE not specified")
endif()

if(NOT EXISTS "${FILE}")
    message(FATAL_ERROR "DEPLOYMENT FAILURE: Required DLL not found: ${FILE}")
else()
    message(STATUS "Verified: ${FILE}")
endif()
