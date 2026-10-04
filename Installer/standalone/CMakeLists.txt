cmake_minimum_required(VERSION 3.20)
# Standalone Lunar Player installer (no Burn engine). Single elevated
# process from startup via requireAdministrator (linker MANIFESTUAC).
# Reuses the native BA sources; version is the single CMake source.
project(LunarPlayerInstaller VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)

# x64: per-machine paths + 64-bit ARP view without redirection games.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Standalone installer must be configured x64.")
endif()

set(BOOTSTRAPPER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../bootstrapper")
set(WIX_INC "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/wix314/sdk/inc")

# Single version source -> code (BundleVersion.h) + exe resources.
file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../assets/LunarPlayer.ico" LUNAR_ICON)
if(NOT EXISTS "${LUNAR_ICON}")
    message(FATAL_ERROR "installer icon missing: ${LUNAR_ICON}")
endif()
configure_file(
    "${BOOTSTRAPPER_DIR}/src/BundleVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BundleVersion.h")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/InstallerVersion.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/InstallerVersion.rc")

set(INSTALLER_SOURCES
    standalone_main.cpp
    "${BOOTSTRAPPER_DIR}/src/LunarBA.cpp"
    "${BOOTSTRAPPER_DIR}/src/LunarBABase.cpp"
    "${BOOTSTRAPPER_DIR}/src/NativeUi.cpp"
    "${BOOTSTRAPPER_DIR}/src/InstallerBridge.cpp"
    "${BOOTSTRAPPER_DIR}/src/Downloader.cpp"
    "${BOOTSTRAPPER_DIR}/src/Maintenance.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/InstallerVersion.rc")

add_executable(LunarPlayerInstaller WIN32 ${INSTALLER_SOURCES})
target_include_directories(LunarPlayerInstaller PRIVATE
    "${BOOTSTRAPPER_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}"
    "${WIX_INC}")
target_link_libraries(LunarPlayerInstaller PRIVATE
    ole32 shell32 shlwapi user32 comctl32 version d2d1 dwrite
    windowscodecs winhttp bcrypt)
target_compile_definitions(LunarPlayerInstaller PRIVATE
    LUNAR_MANIFEST_ELEVATED LUNAR_STANDALONE)
set_target_properties(LunarPlayerInstaller PROPERTIES OUTPUT_NAME
    "LunarPlayerInstaller")
# Startup elevation: OS prompts once at launch (single process).
set_target_properties(LunarPlayerInstaller PROPERTIES LINK_FLAGS
    "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
