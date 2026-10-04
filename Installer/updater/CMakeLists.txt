cmake_minimum_required(VERSION 3.20)
# Single version source for the installer tree (mirrors the player rule:
# CMake project VERSION is authoritative).
project(LunarUpdater VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Lunar updater requires 64-bit.")
endif()

add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX)

# Generated version header (single source -> code + bundle + manifests).
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/UpdaterVersion.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/UpdaterVersion.h")

set(WIX_INC "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/wix314/sdk/inc")
set(WEBVIEW2_INC "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/webview2/build/native/include")
set(WEBVIEW2_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/webview2/build/native/x64/WebView2LoaderStatic.lib")

add_executable(LunarPlayerUpdater WIN32
    src/main.cpp
    src/UpdaterBridge.cpp
    src/UpdaterBridge.h
    ../bootstrapper/src/WebViewHost.cpp
    ../bootstrapper/src/WebViewHost.h
)
target_include_directories(LunarPlayerUpdater PRIVATE
    src
    ../bootstrapper/src
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${WIX_INC}"
    "${WEBVIEW2_INC}")
target_link_libraries(LunarPlayerUpdater PRIVATE
    "${WEBVIEW2_LIB}"
    ole32 shell32 shlwapi user32 comctl32 version winhttp bcrypt wintrust crypt32)

enable_testing()
add_test(NAME UpdaterBridge COMMAND LunarPlayerUpdater --self-test)
