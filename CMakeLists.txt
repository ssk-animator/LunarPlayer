cmake_minimum_required(VERSION 3.20)
project(LunarPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LUNAR_QT_DLL_SUFFIX "d")
else()
    set(LUNAR_QT_DLL_SUFFIX "")
endif()

set(LUNAR_DLLS
    "${QT_BIN_DIR}/Qt6Core${LUNAR_QT_DLL_SUFFIX}.dll"
    "${QT_BIN_DIR}/Qt6Gui${LUNAR_QT_DLL_SUFFIX}.dll"
    "${QT_BIN_DIR}/Qt6Widgets${LUNAR_QT_DLL_SUFFIX}.dll"
    "${QT_BIN_DIR}/Qt6OpenGL${LUNAR_QT_DLL_SUFFIX}.dll"
    "${QT_BIN_DIR}/Qt6OpenGLWidgets${LUNAR_QT_DLL_SUFFIX}.dll"
    "${QT_BIN_DIR}/Qt6Test${LUNAR_QT_DLL_SUFFIX}.dll"
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
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/imageformats"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_PREFIX_PATH}/plugins/imageformats/qsvg.dll"
            "$<TARGET_FILE_DIR:${TARGET}>/imageformats/qsvg.dll"
    )
endfunction()

set(LUNAR_AUDIO_SOURCES
    src/audio/AudioOutput.cpp src/audio/AudioOutput.h
    src/decoder/AudioDecoder.cpp src/decoder/AudioDecoder.h
)

set(LUNAR_HOVER_SOURCES
    src/ui/ThumbnailCache.cpp src/ui/ThumbnailCache.h
    src/ui/HoverPreviewWidget.cpp src/ui/HoverPreviewWidget.h
    src/decoder/ThumbnailWorker.cpp src/decoder/ThumbnailWorker.h
)

set(LUNAR_HWACCEL_SOURCES
    src/decoder/HWAccel.cpp src/decoder/HWAccel.h
    src/decoder/DecoderInfo.h
    src/decoder/DecoderManager.cpp src/decoder/DecoderManager.h
)

set(LUNAR_PANEL_SOURCES
    src/ui/FullscreenCommandPanel.cpp src/ui/FullscreenCommandPanel.h
)

# ---- Main Player ----
add_executable(LunarPlayer WIN32
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/GradientOverlay.cpp
    src/ui/GradientOverlay.h
    src/ui/MediaInfo.h
    src/ui/MediaInfoDialog.cpp
    src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp
    src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    icons/icons.qrc
    src/renderer/Renderer.h
    src/renderer/RendererFactory.cpp
    src/renderer/RendererFactory.h
    src/renderer/CPURenderer.cpp
    src/renderer/CPURenderer.h
    src/renderer/GLVideoRenderer.cpp
    src/renderer/GLVideoRenderer.h
    src/renderer/YUVVideoRenderer.cpp
    src/renderer/YUVVideoRenderer.h
)

target_include_directories(LunarPlayer PRIVATE src "${FFMPEG_DIR}/include")

target_link_libraries(LunarPlayer PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi
)

lunar_deploy_dlls(LunarPlayer)

# ---- Tests ----
find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()

set(LUNAR_TEST_COMMON
    Qt6::Core Qt6::Test
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    dxgi
)

# MediaSession API Stability
add_executable(LunarPlayerMediaSessionTest tests/test_mediasession.cpp src/ui/MainWindow.cpp src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerMediaSessionTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerMediaSessionTest PRIVATE Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets ${LUNAR_TEST_COMMON} winmm)
lunar_deploy_dlls(LunarPlayerMediaSessionTest)

add_test(NAME MediaSessionAPI COMMAND LunarPlayerMediaSessionTest)

# SeekSlider Smart Timeline Capture
add_executable(LunarPlayerSeekSliderTest tests/test_seekslider.cpp)
target_include_directories(LunarPlayerSeekSliderTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerSeekSliderTest PRIVATE Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets ${LUNAR_TEST_COMMON} winmm)
lunar_deploy_dlls(LunarPlayerSeekSliderTest)
add_test(NAME SeekSlider COMMAND LunarPlayerSeekSliderTest)

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

# ---- GPU Prove Test (manual, not part of CTest) ----
add_executable(LunarPlayerGPUProve
    tests/test_gpu_prove.cpp
    src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h
    src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h
)
target_include_directories(LunarPlayerGPUProve PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerGPUProve PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
)

# ---- GPU YUV Prove Test (manual, not part of CTest) ----
add_executable(LunarPlayerGPUProveYUV
    tests/test_gpu_prove_yuv.cpp
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
)
target_include_directories(LunarPlayerGPUProveYUV PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerGPUProveYUV PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVUTIL}
)

# ---- Phase 1 Acceptance Demo ----
add_executable(LunarPlayerAcceptance tests/test_acceptance.cpp src/ui/MainWindow.cpp src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerAcceptance PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerAcceptance PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerAcceptance PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm
)
lunar_deploy_dlls(LunarPlayerAcceptance)

# ---- Phase 1 Manual QA Logger ----
add_executable(LunarPlayerManualQA tests/test_manual_qa_logger.cpp src/ui/MainWindow.cpp src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerManualQA PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerManualQA PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerManualQA PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm
)
lunar_deploy_dlls(LunarPlayerManualQA)

# ---- CTest: set offscreen for all tests ----
set_tests_properties(FFmpegBasics Phase1Integration HumanQA MediaSessionAPI PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# ---- Phase 2B Playback Smoke Test (manual, requires native display) ----
add_executable(LunarPlayerPlaybackSmoke tests/test_playback_smoke.cpp src/ui/MainWindow.cpp src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerPlaybackSmoke PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerPlaybackSmoke PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerPlaybackSmoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm
)
lunar_deploy_dlls(LunarPlayerPlaybackSmoke)

# ---- Phase 3A Validation (manual, requires native display) ----
add_executable(LunarPlayerPhase3A tests/test_phase3a_validation.cpp src/ui/MainWindow.cpp src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerPhase3A PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerPhase3A PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerPhase3A PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm
)
lunar_deploy_dlls(LunarPlayerPhase3A)

# ---- Phase 3B Audio Validation (manual, requires speakers) ----
add_executable(LunarPlayerPhase3B tests/test_phase3b_audio.cpp
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerPhase3B PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerPhase3B PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerPhase3B PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi
)
lunar_deploy_dlls(LunarPlayerPhase3B)

# ---- Fullscreen Context Menu Test (requires native display) ----
add_executable(LunarPlayerContextMenuTest tests/test_context_menu.cpp
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerContextMenuTest PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerContextMenuTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerContextMenuTest PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi
)
lunar_deploy_dlls(LunarPlayerContextMenuTest)

# ---- Phase 2D Playback Stress Test (requires native display) ----
add_executable(LunarPlayerStress
    tests/test_stress_playback.cpp
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    src/ui/FullscreenCommandPanel.cpp src/ui/FullscreenCommandPanel.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerStress PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerStress PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerStress PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi
)
lunar_deploy_dlls(LunarPlayerStress)

# ---- Phase 2C Benchmark (manual, requires native display) ----
add_executable(LunarPlayerBenchmark tests/test_benchmark.cpp src/ui/MainWindow.cpp src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerBenchmark PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerBenchmark PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerBenchmark PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm
)
lunar_deploy_dlls(LunarPlayerBenchmark)

# ---- Phase 3 Performance Report ----
add_executable(LunarPlayerPerformanceReport tests/test_performance_report.cpp
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerPerformanceReport PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerPerformanceReport PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerPerformanceReport PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi
)
lunar_deploy_dlls(LunarPlayerPerformanceReport)

if(MSVC)
    target_compile_options(LunarPlayer PRIVATE /utf-8)
    target_compile_options(LunarPlayerTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase1Test PRIVATE /utf-8)
    target_compile_options(LunarPlayerQATest PRIVATE /utf-8)
    target_compile_options(LunarPlayerMediaSessionTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPlaybackSmoke PRIVATE /utf-8)
    target_compile_options(LunarPlayerBenchmark PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase3A PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase3B PRIVATE /utf-8)
    target_compile_options(LunarPlayerPerformanceReport PRIVATE /utf-8)
    target_compile_options(LunarPlayerContextMenuTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerStress PRIVATE /utf-8)
endif()
