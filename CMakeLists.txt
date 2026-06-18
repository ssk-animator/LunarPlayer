cmake_minimum_required(VERSION 3.20)
project(LunarPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ---- Qt6 ----
set(CMAKE_PREFIX_PATH "C:/Qt/6.8.3/msvc2022_64")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

# ---- FFmpeg ----
set(FFMPEG_DIR "${CMAKE_SOURCE_DIR}/third_party")
find_library(AVCODEC avcodec PATHS "${FFMPEG_DIR}/lib" NO_DEFAULT_PATH)
find_library(AVFORMAT avformat PATHS "${FFMPEG_DIR}/lib" NO_DEFAULT_PATH)
find_library(AVUTIL avutil PATHS "${FFMPEG_DIR}/lib" NO_DEFAULT_PATH)
find_library(SWRESAMPLE swresample PATHS "${FFMPEG_DIR}/lib" NO_DEFAULT_PATH)
find_library(SWSCALE swscale PATHS "${FFMPEG_DIR}/lib" NO_DEFAULT_PATH)

message(STATUS "AVCODEC: ${AVCODEC}")
message(STATUS "AVFORMAT: ${AVFORMAT}")

# ---- DLL deployment helper ----
set(QT_BIN_DIR "${CMAKE_PREFIX_PATH}/bin")
set(FFMPEG_BIN_DIR "${FFMPEG_DIR}/bin")

set(LUNAR_DLLS
    "${QT_BIN_DIR}/Qt6Core.dll"
    "${QT_BIN_DIR}/Qt6Gui.dll"
    "${QT_BIN_DIR}/Qt6Widgets.dll"
    "${QT_BIN_DIR}/Qt6OpenGL.dll"
    "${QT_BIN_DIR}/Qt6OpenGLWidgets.dll"
    "${QT_BIN_DIR}/Qt6Test.dll"
    "${FFMPEG_BIN_DIR}/avcodec-62.dll"
    "${FFMPEG_BIN_DIR}/avformat-62.dll"
    "${FFMPEG_BIN_DIR}/avutil-60.dll"
    "${FFMPEG_BIN_DIR}/swscale-9.dll"
    "${FFMPEG_BIN_DIR}/swresample-6.dll"
)

function(lunar_deploy_dlls TARGET)
    foreach(dll ${LUNAR_DLLS})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}" "$<TARGET_FILE_DIR:${TARGET}>"
        )
    endforeach()
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qoffscreen.dll"
            "$<TARGET_FILE_DIR:${TARGET}>/platforms/qoffscreen.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QT_BIN_DIR}/../plugins/platforms/qwindows.dll"
            "$<TARGET_FILE_DIR:${TARGET}>/platforms/qwindows.dll"
    )
endfunction()

# ---- Main Player ----
add_executable(LunarPlayer WIN32
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/VideoWidget.cpp
    src/ui/VideoWidget.h
)

target_include_directories(LunarPlayer PRIVATE src "${FFMPEG_DIR}/include")

target_link_libraries(LunarPlayer PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
)

lunar_deploy_dlls(LunarPlayer)

# ---- Tests ----
find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()

set(LUNAR_TEST_COMMON
    Qt6::Core Qt6::Test
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
)

# FFmpeg Basics
add_executable(LunarPlayerTest tests/test_ffmpeg.cpp)
target_include_directories(LunarPlayerTest PRIVATE "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerTest PRIVATE ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerTest)

add_test(NAME FFmpegBasics COMMAND LunarPlayerTest)

# Phase 1 Integration
add_executable(LunarPlayerPhase1Test tests/test_phase1.cpp)
target_compile_definitions(LunarPlayerPhase1Test PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase1Test PRIVATE "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase1Test PRIVATE Qt6::Gui ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase1Test)

add_test(NAME Phase1Integration COMMAND LunarPlayerPhase1Test)

# Human QA
add_executable(LunarPlayerQATest tests/test_human_qa.cpp)
target_compile_definitions(LunarPlayerQATest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerQATest PRIVATE "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerQATest PRIVATE Qt6::Gui ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerQATest)

add_test(NAME HumanQA COMMAND LunarPlayerQATest)

# ---- Phase 1 Acceptance Demo ----
add_executable(LunarPlayerAcceptance tests/test_acceptance.cpp src/ui/MainWindow.cpp src/ui/VideoWidget.cpp)
target_include_directories(LunarPlayerAcceptance PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerAcceptance PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerAcceptance PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
)
lunar_deploy_dlls(LunarPlayerAcceptance)

# ---- Phase 1 Manual QA Logger ----
add_executable(LunarPlayerManualQA tests/test_manual_qa_logger.cpp src/ui/MainWindow.cpp src/ui/VideoWidget.cpp)
target_include_directories(LunarPlayerManualQA PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerManualQA PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerManualQA PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
)
lunar_deploy_dlls(LunarPlayerManualQA)

# ---- CTest: set offscreen for all tests ----
set_tests_properties(FFmpegBasics Phase1Integration HumanQA PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

if(MSVC)
    target_compile_options(LunarPlayer PRIVATE /utf-8)
    target_compile_options(LunarPlayerTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase1Test PRIVATE /utf-8)
    target_compile_options(LunarPlayerQATest PRIVATE /utf-8)
endif()
