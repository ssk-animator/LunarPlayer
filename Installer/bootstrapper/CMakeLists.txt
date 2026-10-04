cmake_minimum_required(VERSION 3.20)
# Single version source for the installer tree (mirrors the player rule).
project(LunarBootstrapper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Narrow Win32 APIs used throughout (CreateWindowExW, LoadCursorW, ...).
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)

# 32-bit BA: the wix314 binaries ship a 32-bit Burn engine, which cannot
# LoadLibrary a 64-bit BA DLL (ERROR_BAD_EXE_FORMAT). A 32-bit BA driving
# 64-bit packages is standard WiX v3 practice; all BA duties (UI, folder
# browse, process launch) are bitness-neutral. Configure with -A Win32.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Lunar BA must be configured Win32 (x86 Burn engine).")
endif()

set(WIX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/wix314")
set(WIX_INC "${WIX_ROOT}/sdk/inc")
# v141-built static utils (newest shipped); link-compatible with v143/MD.
set(WIX_LIB_DIR "${WIX_ROOT}/sdk/vs2017/lib/x86")
set(WEBVIEW2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/webview2")
set(WEBVIEW2_INC "${WEBVIEW2_ROOT}/build/native/include")
set(WEBVIEW2_LIB "${WEBVIEW2_ROOT}/build/native/x86/WebView2LoaderStatic.lib")

# Generated bundle version header (single source -> code + bundle).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/BundleVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/BundleVersion.h")

set(BA_SOURCES
    src/LunarBA.cpp
    src/LunarBABase.cpp
    src/NativeUi.cpp
    src/InstallerBridge.cpp
    src/Downloader.cpp
    src/Maintenance.cpp
)
set(BA_HEADERS
    src/LunarBA.h
    src/LunarBABase.h
    src/NativeUi.h
    src/RpcBackend.h
    src/InstallerBridge.h
    src/Downloader.h
    src/Maintenance.h
)

add_library(LunarBA SHARED src/main.cpp ${BA_SOURCES} ${BA_HEADERS}
    src/LunarBA.def)
target_include_directories(LunarBA PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}" "${WIX_INC}")
target_link_libraries(LunarBA PRIVATE
    "${WIX_LIB_DIR}/dutil.lib"
    ole32 shell32 shlwapi user32 comctl32 version d2d1 dwrite windowscodecs winhttp bcrypt)
set_target_properties(LunarBA PROPERTIES OUTPUT_NAME "LunarBA")
# Startup-log manifest tag: the release bundle embeds the full Burn-stock
# manifest flipped to requireAdministrator (see package-release.ps1 6b).
# BATest/uitest link default manifests, so they log their own tag.
target_compile_definitions(LunarBA PRIVATE LUNAR_MANIFEST_ELEVATED)

# Headless bridge test (no Burn engine): exercises the real bridge
# dispatch + payload probing + native UI data path against a directory.
add_executable(BATest test/test_main.cpp ${BA_SOURCES} ${BA_HEADERS})
target_include_directories(BATest PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}" "${WIX_INC}")
target_link_libraries(BATest PRIVATE
    "${WIX_LIB_DIR}/dutil.lib"
    ole32 shell32 shlwapi user32 comctl32 version d2d1 dwrite windowscodecs winhttp bcrypt)
target_compile_definitions(BATest PRIVATE LUNAR_BA_TEST_HARNESS)

enable_testing()
add_test(NAME BootstrapperBridge COMMAND BATest)
set_tests_properties(BootstrapperBridge PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:BATest>")

# Visual harness for the native installer UI (no Burn engine): renders
# the real window with demo data for screenshot verification.
add_executable(uitest WIN32 diag/uitest_main.cpp ${BA_SOURCES} ${BA_HEADERS})
target_include_directories(uitest PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}" "${WIX_INC}")
target_link_libraries(uitest PRIVATE
    "${WIX_LIB_DIR}/dutil.lib"
    ole32 shell32 shlwapi user32 comctl32 version d2d1 dwrite windowscodecs winhttp bcrypt)
set_target_properties(uitest PROPERTIES OUTPUT_NAME "uitest")
