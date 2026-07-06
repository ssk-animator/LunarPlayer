cmake_minimum_required(VERSION 3.20)
project(LunarPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ---- Qt6 ----
set(CMAKE_PREFIX_PATH "C:/Qt/6.8.3/msvc2022_64")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)

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
# Uses generator expressions so Debug/Release suffix is correct
# for multi-config generators (MSBuild) without hardcoded paths.
set(QT_PLUGINS_DIR "${CMAKE_PREFIX_PATH}/plugins")
set(FFMPEG_BIN_DIR "${FFMPEG_DIR}/bin")

set(QT_DLL_NAMES
    Qt6Core Qt6Gui Qt6Widgets Qt6OpenGL Qt6OpenGLWidgets Qt6Test
)

set(FFMPEG_DLL_NAMES
    avcodec-62 avformat-62 avutil-60 swscale-9 swresample-6
)

set(QT_PLATFORM_PLUGINS
    qwindows qoffscreen
)

set(QT_IMAGE_PLUGINS
    qjpeg qgif qico qsvg qtiff qwebp
)

function(lunar_deploy_dlls TARGET)
    # Deploy Qt runtime DLLs (config-aware: Qt6Core.dll always in output)
    foreach(qtdll ${QT_DLL_NAMES})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -D "SRC=${CMAKE_PREFIX_PATH}/bin/${qtdll}$<$<CONFIG:DEBUG>:d>.dll"
                -D "DST=$<TARGET_FILE_DIR:${TARGET}>/${qtdll}.dll"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
            COMMENT "Deploying ${qtdll}.dll"
        )
    endforeach()

    # Deploy FFmpeg DLLs
    foreach(fdll ${FFMPEG_DLL_NAMES})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -D "SRC=${FFMPEG_BIN_DIR}/${fdll}.dll"
                -D "DST=$<TARGET_FILE_DIR:${TARGET}>/${fdll}.dll"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
            COMMENT "Deploying ${fdll}.dll"
        )
    endforeach()

    # Platform plugins
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/platforms"
        COMMENT "Creating platforms directory"
    )
    foreach(pp ${QT_PLATFORM_PLUGINS})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -D "SRC=${QT_PLUGINS_DIR}/platforms/${pp}$<$<CONFIG:Debug>:d>.dll"
                -D "DST=$<TARGET_FILE_DIR:${TARGET}>/platforms/${pp}.dll"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
            COMMENT "Deploying platform plugin ${pp}.dll"
        )
    endforeach()

    # Image format plugins
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/imageformats"
        COMMENT "Creating imageformats directory"
    )
    foreach(ip ${QT_IMAGE_PLUGINS})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -D "SRC=${QT_PLUGINS_DIR}/imageformats/${ip}$<$<CONFIG:Debug>:d>.dll"
                -D "DST=$<TARGET_FILE_DIR:${TARGET}>/imageformats/${ip}.dll"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
            COMMENT "Deploying image plugin ${ip}.dll"
        )
    endforeach()

    # TLS plugins (required for Qt6 Network HTTPS)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET}>/tls"
        COMMENT "Creating tls directory"
    )
    foreach(tp qopensslbackend qschannelbackend)
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -D "SRC=${QT_PLUGINS_DIR}/tls/${tp}$<$<CONFIG:Debug>:d>.dll"
                -D "DST=$<TARGET_FILE_DIR:${TARGET}>/tls/${tp}.dll"
                -P "${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake"
            COMMENT "Deploying TLS plugin ${tp}.dll"
        )
    endforeach()

    # Post-build verification: check critical DLLs exist
    set(VERIFY_DLLS
        Qt6Core Qt6Gui Qt6Widgets Qt6OpenGL Qt6OpenGLWidgets
    )
    foreach(vdll ${VERIFY_DLLS})
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Verifying ${vdll}.dll..."
            COMMAND ${CMAKE_COMMAND} -D "FILE=$<TARGET_FILE_DIR:${TARGET}>/${vdll}.dll" -P "${CMAKE_SOURCE_DIR}/cmake/verify_dll.cmake"
        )
    endforeach()
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "All required DLLs deployed for ${TARGET}"
    )
endfunction()

set(LUNAR_AUDIO_SOURCES
    src/audio/IAudioBackend.h
    src/audio/AudioStreamDecoder.h
    src/audio/AudioResampler.h
    src/audio/AudioClock.h
    src/audio/AudioRingBuffer.h
    src/audio/AudioBackendFactory.h src/audio/AudioBackendFactory.cpp
    src/audio/AudioChannelRouter.h src/audio/AudioChannelRouter.cpp
    src/audio/AudioDeviceManager.h src/audio/AudioDeviceManager.cpp
    src/audio/AudioEngine.h src/audio/AudioEngine.cpp
    src/audio/AudioOutput.h src/audio/AudioOutput.cpp
    src/audio/WaveOutBackend.h src/audio/WaveOutBackend.cpp
    src/audio/WasapiSharedBackend.h src/audio/WasapiSharedBackend.cpp
    src/audio/WasapiExclusiveBackend.h src/audio/WasapiExclusiveBackend.cpp
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
    src/decoder/DecodeThread.cpp src/decoder/DecodeThread.h
    src/decoder/AudioDecoder.cpp src/decoder/AudioDecoder.h
)

set(LUNAR_NETWORK_SOURCES
    src/network/NetworkBuffer.cpp src/network/NetworkBuffer.h
    src/network/NetworkMediaSession.cpp src/network/NetworkMediaSession.h
)

set(LUNAR_PANEL_SOURCES
    src/ui/FullscreenCommandPanel.cpp src/ui/FullscreenCommandPanel.h
)

set(LUNAR_SEQUENCE_SOURCES
    src/ui/SequenceFrameCache.cpp src/ui/SequenceFrameCache.h
)

set(LUNAR_COMPARE_SOURCES
    src/ui/CompareController.cpp src/ui/CompareController.h
    src/ui/CompareWidget.cpp src/ui/CompareWidget.h
)

set(LUNAR_SUBTITLE_SOURCES
    src/subtitles/SubtitleFrame.h
    src/subtitles/SubtitleSurface.cpp src/subtitles/SubtitleSurface.h
    src/subtitles/SubtitleCache.cpp src/subtitles/SubtitleCache.h
    src/subtitles/SubtitleDecoder.cpp src/subtitles/SubtitleDecoder.h
    src/subtitles/SubtitleRenderer.cpp src/subtitles/SubtitleRenderer.h
    src/subtitles/ExternalSubtitleLoader.cpp src/subtitles/ExternalSubtitleLoader.h
    src/subtitles/SubtitleManager.cpp src/subtitles/SubtitleManager.h
    src/subtitles/AssTypes.h
    src/subtitles/AssParser.cpp src/subtitles/AssParser.h
    src/subtitles/AssOverrideParser.cpp src/subtitles/AssOverrideParser.h
    src/subtitles/AssAnimationEngine.cpp src/subtitles/AssAnimationEngine.h
    src/subtitles/AssClippingEngine.cpp src/subtitles/AssClippingEngine.h
    src/subtitles/KaraokeTimeline.cpp src/subtitles/KaraokeTimeline.h
    src/subtitles/AssRenderCache.cpp src/subtitles/AssRenderCache.h
)

# ---- Main Player ----
set(LUNAR_RENDERER_SOURCES
    src/renderer/Renderer.h
    src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h
    src/renderer/CapabilityRenderer.cpp src/renderer/CapabilityRenderer.h
    src/renderer/FrameInfo.cpp src/renderer/FrameInfo.h
    src/renderer/TextureUploader.cpp src/renderer/TextureUploader.h
    src/renderer/ShaderManager.cpp src/renderer/ShaderManager.h
    src/renderer/ColorManager.cpp src/renderer/ColorManager.h
    src/renderer/GPUTextureCache.cpp src/renderer/GPUTextureCache.h
    src/renderer/CropAnalyzer.cpp src/renderer/CropAnalyzer.h
)

set(LUNAR_PLUGIN_SOURCES
    src/plugin/PluginAPI.h
    src/plugin/PluginLoader.cpp src/plugin/PluginLoader.h
)

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
    src/ui/StreamingOverlay.cpp
    src/ui/StreamingOverlay.h
    ${LUNAR_SEQUENCE_SOURCES}
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    ${LUNAR_COMPARE_SOURCES}
    ${LUNAR_SUBTITLE_SOURCES}
    ${LUNAR_NETWORK_SOURCES}
    ${LUNAR_RENDERER_SOURCES}
    ${LUNAR_PLUGIN_SOURCES}
    src/update/GitHubUpdateProvider.h
    src/update/GitHubUpdateProvider.cpp
    src/update/IUpdateProvider.h
    src/update/IUpdateProvider.cpp
    src/update/DownloadManager.h
    src/update/DownloadManager.cpp
    src/update/PackageVerifier.h
    src/update/PackageVerifier.cpp
    src/update/UpdateInstaller.h
    src/update/UpdateInstaller.cpp
    src/update/UpdateManager.h
    src/update/UpdateManager.cpp
    src/update/UpdateDialog.h
    src/update/UpdateDialog.cpp
    icons/icons.qrc
)

target_include_directories(LunarPlayer PRIVATE src "${FFMPEG_DIR}/include" third_party/miniz)

target_link_libraries(LunarPlayer PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi opengl32
    ole32 oleaut32 mmdevapi avrt
    lunar_miniz
)

lunar_deploy_dlls(LunarPlayer)

# ---- Miniz static library ----
set_source_files_properties(third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c PROPERTIES LANGUAGE C)
add_library(lunar_miniz STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)
target_include_directories(lunar_miniz PRIVATE third_party/miniz)
set_target_properties(lunar_miniz PROPERTIES LINKER_LANGUAGE C)

# ---- Update Helper executable ----
add_executable(LunarUpdateHelper WIN32
    src/update/LunarUpdateHelper.cpp
)
target_include_directories(LunarUpdateHelper PRIVATE third_party/miniz src)
target_link_libraries(LunarUpdateHelper PRIVATE Qt6::Core lunar_miniz)
lunar_deploy_dlls(LunarUpdateHelper)

# ---- Tests ----
find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()

set(LUNAR_TEST_COMMON
    Qt6::Core Qt6::Test
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    dxgi
)

# MediaSession API Stability
add_executable(LunarPlayerMediaSessionTest tests/test_mediasession.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
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

# Phase 6B Universal Format Support
add_executable(LunarPlayerPhase6BTest tests/test_phase6b.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_compile_definitions(LunarPlayerPhase6BTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase6BTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase6BTest PRIVATE Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets ${LUNAR_TEST_COMMON} winmm)
lunar_deploy_dlls(LunarPlayerPhase6BTest)
add_test(NAME Phase6B COMMAND LunarPlayerPhase6BTest)

# Phase 6C Image Sequence & EXR Support
add_executable(LunarPlayerPhase6CTest tests/test_phase6c.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_compile_definitions(LunarPlayerPhase6CTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase6CTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase6CTest PRIVATE Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets ${LUNAR_TEST_COMMON} winmm)
lunar_deploy_dlls(LunarPlayerPhase6CTest)
add_test(NAME Phase6C COMMAND LunarPlayerPhase6CTest)

# Phase 6D Side-by-Side Compare
add_executable(LunarPlayerPhase6DTest tests/test_phase6d.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_compile_definitions(LunarPlayerPhase6DTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase6DTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase6DTest PRIVATE Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets ${LUNAR_TEST_COMMON} winmm)
lunar_deploy_dlls(LunarPlayerPhase6DTest)
add_test(NAME Phase6D COMMAND LunarPlayerPhase6DTest)

# Phase 7A Subtitle Infrastructure
add_executable(LunarPlayerPhase7ATest tests/test_phase7a.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7ATest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7ATest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7ATest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7ATest)
add_test(NAME Phase7A COMMAND LunarPlayerPhase7ATest)

# Phase 7A Runtime verification (end-to-end subtitle pipeline)
add_executable(LunarPlayerPhase7ARuntimeTest tests/test_phase7a_runtime.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7ARuntimeTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7ARuntimeTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7ARuntimeTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7ARuntimeTest)
add_test(NAME Phase7ARuntime COMMAND LunarPlayerPhase7ARuntimeTest)

# Phase 7A Update Pipeline E2E + Failure Tests
add_executable(LunarPlayerUpdateE2ETest tests/test_update_e2e.cpp
    tests/update/LocalUpdateServer.cpp
    src/update/UpdateManager.cpp
    src/update/GitHubUpdateProvider.cpp
    src/update/IUpdateProvider.cpp
    src/update/DownloadManager.cpp
    src/update/PackageVerifier.cpp
    src/update/UpdateInstaller.cpp
    src/update/UpdateDialog.cpp
)
target_compile_definitions(LunarPlayerUpdateE2ETest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerUpdateE2ETest PRIVATE src src/update tests "${FFMPEG_DIR}/include" third_party/miniz)
target_link_libraries(LunarPlayerUpdateE2ETest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network lunar_miniz ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerUpdateE2ETest)
add_test(NAME UpdateE2E COMMAND LunarPlayerUpdateE2ETest)

# Phase 7B Universal Bitmap Subtitle Engine
add_executable(LunarPlayerPhase7BTest tests/test_phase7b.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7BTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7BTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7BTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7BTest)
add_test(NAME Phase7B COMMAND LunarPlayerPhase7BTest)

add_executable(LunarPlayerPhase7BRuntimeTest tests/test_phase7b_runtime.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7BRuntimeTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7BRuntimeTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7BRuntimeTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7BRuntimeTest)
add_test(NAME Phase7BRuntime COMMAND LunarPlayerPhase7BRuntimeTest)

# Phase 7B End-to-End functional verification
add_executable(LunarPlayerPhase7BE2ETest tests/test_phase7b_e2e.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7BE2ETest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7BE2ETest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7BE2ETest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7BE2ETest)
add_test(NAME Phase7BE2E COMMAND LunarPlayerPhase7BE2ETest)

# Phase 7C ASS Subtitle Engine
add_executable(LunarPlayerPhase7CTest tests/test_phase7c.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7CTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7CTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7CTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7CTest)
add_test(NAME Phase7C COMMAND LunarPlayerPhase7CTest)

add_executable(LunarPlayerPhase7CRuntimeTest tests/test_phase7c_runtime.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7CRuntimeTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7CRuntimeTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7CRuntimeTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7CRuntimeTest)
add_test(NAME Phase7CRuntime COMMAND LunarPlayerPhase7CRuntimeTest)

# Phase 7C Integration (comprehensive end-to-end)
add_executable(LunarPlayerPhase7CIntegrationTest tests/test_phase7c_integration.cpp
    ${LUNAR_SUBTITLE_SOURCES})
target_compile_definitions(LunarPlayerPhase7CIntegrationTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase7CIntegrationTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase7CIntegrationTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase7CIntegrationTest)

# Phase 8-12 Comprehensive Integration Test
set(LUNAR_PHASE8_12_TEST_SOURCES
    tests/test_phase8_12_integration.cpp
    ${LUNAR_NETWORK_SOURCES}
    src/renderer/ColorManager.cpp src/renderer/ColorManager.h
    src/renderer/GPUTextureCache.cpp src/renderer/GPUTextureCache.h
    ${LUNAR_PLUGIN_SOURCES}
    src/ui/StreamingOverlay.cpp src/ui/StreamingOverlay.h
)
add_executable(LunarPlayerPhase8_12IntegrationTest ${LUNAR_PHASE8_12_TEST_SOURCES})
target_compile_definitions(LunarPlayerPhase8_12IntegrationTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase8_12IntegrationTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase8_12IntegrationTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase8_12IntegrationTest)
add_test(NAME Phase8_12Integration COMMAND LunarPlayerPhase8_12IntegrationTest)

# ---- Sample Plugins (Phase 13) ----
# Sample Decoder Plugin
add_library(sample_decoder SHARED
    plugins/sample_decoder/PluginExport.cpp
    plugins/sample_decoder/SampleDecoderPlugin.cpp
    plugins/sample_decoder/SampleDecoderPlugin.h
)
target_include_directories(sample_decoder PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(sample_decoder PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(sample_decoder PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
)

# Sample Network Plugin
add_library(sample_network SHARED
    plugins/sample_network/PluginExport.cpp
    plugins/sample_network/SampleNetworkPlugin.cpp
    plugins/sample_network/SampleNetworkPlugin.h
)
target_include_directories(sample_network PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(sample_network PRIVATE Qt6::Core Qt6::Gui)
set_target_properties(sample_network PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/plugins"
)

# Plugin Loading Test
add_executable(LunarPlayerPluginTest tests/test_plugin_loading.cpp
    ${LUNAR_PLUGIN_SOURCES}
)
target_compile_definitions(LunarPlayerPluginTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPluginTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPluginTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
add_dependencies(LunarPlayerPluginTest sample_decoder sample_network)
lunar_deploy_dlls(LunarPlayerPluginTest)
add_test(NAME Phase13_PluginLoading COMMAND LunarPlayerPluginTest)

# Phase 13 Comprehensive Runtime Verification Test
set(LUNAR_PHASE13_RUNTIME_SOURCES
    tests/test_phase13_runtime.cpp
    ${LUNAR_NETWORK_SOURCES}
    src/renderer/ColorManager.cpp src/renderer/ColorManager.h
    src/renderer/GPUTextureCache.cpp src/renderer/GPUTextureCache.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/Renderer.h
    ${LUNAR_PLUGIN_SOURCES}
    src/ui/StreamingOverlay.cpp src/ui/StreamingOverlay.h
)
add_executable(LunarPlayerPhase13RuntimeTest ${LUNAR_PHASE13_RUNTIME_SOURCES})
target_compile_definitions(LunarPlayerPhase13RuntimeTest PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(LunarPlayerPhase13RuntimeTest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase13RuntimeTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
add_dependencies(LunarPlayerPhase13RuntimeTest sample_decoder sample_network)
lunar_deploy_dlls(LunarPlayerPhase13RuntimeTest)
add_test(NAME Phase13_Runtime COMMAND LunarPlayerPhase13RuntimeTest)

# Phase 14A Zoom/Aspect/Crop Pipeline Test
set(LUNAR_PHASE14A_TEST_SOURCES
    tests/test_phase14a_zoom_aspect_crop.cpp
    src/renderer/ColorManager.cpp src/renderer/ColorManager.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/Renderer.h
)
add_executable(LunarPlayerPhase14ATest ${LUNAR_PHASE14A_TEST_SOURCES})
target_include_directories(LunarPlayerPhase14ATest PRIVATE src "${FFMPEG_DIR}/include")
target_link_libraries(LunarPlayerPhase14ATest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets ${LUNAR_TEST_COMMON})
lunar_deploy_dlls(LunarPlayerPhase14ATest)
add_test(NAME Phase14A_Pipeline COMMAND LunarPlayerPhase14ATest)

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
add_executable(LunarPlayerAcceptance tests/test_acceptance.cpp src/ui/MainWindow.cpp src/ui/StreamingOverlay.cpp src/ui/StreamingOverlay.h ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp src/ui/VideoWidget.h ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} ${LUNAR_NETWORK_SOURCES} ${LUNAR_RENDERER_SOURCES} ${LUNAR_PLUGIN_SOURCES})
target_include_directories(LunarPlayerAcceptance PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerAcceptance PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerAcceptance PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi opengl32
)
lunar_deploy_dlls(LunarPlayerAcceptance)

# ---- Phase 1 Manual QA Logger ----
add_executable(LunarPlayerManualQA tests/test_manual_qa_logger.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerManualQA PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerManualQA PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerManualQA PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm
)
lunar_deploy_dlls(LunarPlayerManualQA)

# ---- CTest: set offscreen for all tests ----
set_tests_properties(FFmpegBasics Phase1Integration HumanQA MediaSessionAPI Phase6C Phase6D Phase7A Phase7ARuntime Phase7B Phase7BRuntime     Phase7BE2E Phase7C Phase7CRuntime PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# ---- Phase 2B Playback Smoke Test (manual, requires native display) ----
add_executable(LunarPlayerPlaybackSmoke tests/test_playback_smoke.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerPlaybackSmoke PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerPlaybackSmoke PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerPlaybackSmoke PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm
)
lunar_deploy_dlls(LunarPlayerPlaybackSmoke)

# ---- Phase 3A Validation (manual, requires native display) ----
add_executable(LunarPlayerPhase3A tests/test_phase3a_validation.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
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
    ${LUNAR_SEQUENCE_SOURCES}
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    ${LUNAR_COMPARE_SOURCES}
    ${LUNAR_SUBTITLE_SOURCES}
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
    ${LUNAR_SEQUENCE_SOURCES}
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    ${LUNAR_COMPARE_SOURCES}
    ${LUNAR_SUBTITLE_SOURCES}
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
    ${LUNAR_SEQUENCE_SOURCES}
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    src/ui/FullscreenCommandPanel.cpp src/ui/FullscreenCommandPanel.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_COMPARE_SOURCES}
    ${LUNAR_SUBTITLE_SOURCES}
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
add_executable(LunarPlayerBenchmark tests/test_benchmark.cpp src/ui/MainWindow.cpp ${LUNAR_SEQUENCE_SOURCES} src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h src/ui/VideoWidget.cpp ${LUNAR_HOVER_SOURCES} ${LUNAR_AUDIO_SOURCES} ${LUNAR_HWACCEL_SOURCES} ${LUNAR_PANEL_SOURCES} ${LUNAR_COMPARE_SOURCES} ${LUNAR_SUBTITLE_SOURCES} src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
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
    ${LUNAR_SEQUENCE_SOURCES}
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    ${LUNAR_COMPARE_SOURCES}
    ${LUNAR_SUBTITLE_SOURCES}
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

# ---- Comprehensive Validation Suite ----
add_executable(LunarPlayerValidationSuite tests/test_validation_suite.cpp
    src/ui/MainWindow.cpp src/ui/MainWindow.h
    ${LUNAR_SEQUENCE_SOURCES}
    src/ui/GradientOverlay.cpp src/ui/GradientOverlay.h
    src/ui/MediaInfo.h src/ui/MediaInfoDialog.cpp src/ui/MediaInfoDialog.h
    src/ui/VideoWidget.cpp src/ui/VideoWidget.h
    ${LUNAR_HOVER_SOURCES}
    ${LUNAR_AUDIO_SOURCES}
    ${LUNAR_HWACCEL_SOURCES}
    ${LUNAR_PANEL_SOURCES}
    ${LUNAR_COMPARE_SOURCES}
    ${LUNAR_SUBTITLE_SOURCES}
    src/renderer/RendererFactory.cpp src/renderer/RendererFactory.h
    src/renderer/CPURenderer.cpp src/renderer/CPURenderer.h
    src/renderer/GLVideoRenderer.cpp src/renderer/GLVideoRenderer.h
    src/renderer/YUVVideoRenderer.cpp src/renderer/YUVVideoRenderer.h)
target_include_directories(LunarPlayerValidationSuite PRIVATE src "${FFMPEG_DIR}/include")
target_compile_definitions(LunarPlayerValidationSuite PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(LunarPlayerValidationSuite PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
    ${AVCODEC} ${AVFORMAT} ${AVUTIL} ${SWRESAMPLE} ${SWSCALE}
    winmm dxgi
)
lunar_deploy_dlls(LunarPlayerValidationSuite)

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
    target_compile_options(LunarPlayerValidationSuite PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase6BTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase6CTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase6DTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7ATest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7ARuntimeTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7BTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7BRuntimeTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7BE2ETest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7CTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7CRuntimeTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase7CIntegrationTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase8_12IntegrationTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPluginTest PRIVATE /utf-8)
    target_compile_options(LunarPlayerPhase13RuntimeTest PRIVATE /utf-8)
    target_compile_options(sample_decoder PRIVATE /utf-8)
    target_compile_options(sample_network PRIVATE /utf-8)
endif()
