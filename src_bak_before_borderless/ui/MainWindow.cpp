#include "MainWindow.h"
#include "VideoWidget.h"
#include "ThumbnailCache.h"
#include "HoverPreviewWidget.h"
#include "Icons.h"
#include "MediaInfoDialog.h"
#include "renderer/Renderer.h"
#include "decoder/AudioDecoder.h"
#include "audio/AudioOutput.h"
#include <QApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QCloseEvent>
#include <QFileInfo>
#include <QStyle>
#include <QMessageBox>
#include <QEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QActionGroup>
#include <QStackedLayout>
#include <QPropertyAnimation>
#include <QElapsedTimer>
#include <QStatusBar>
#include <QRegularExpression>
#include <QDir>
#include <climits>
#include <cmath>


// ============================================================
// MediaSession
// ============================================================

MediaSession::~MediaSession() { close(); }

static const char* hwDecoderName(AVCodecID codecId, AVHWDeviceType hwType)
{
    switch (codecId) {
    case AV_CODEC_ID_H264:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "h264_d3d11va";
        case AV_HWDEVICE_TYPE_DXVA2:   return "h264_dxva2";
        case AV_HWDEVICE_TYPE_CUDA:    return "h264_cuviddec";
        case AV_HWDEVICE_TYPE_QSV:     return "h264_qsv";
        case AV_HWDEVICE_TYPE_AMF:     return "h264_amf";
        default: return nullptr;
        }
    case AV_CODEC_ID_HEVC:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "hevc_d3d11va";
        case AV_HWDEVICE_TYPE_DXVA2:   return "hevc_dxva2";
        case AV_HWDEVICE_TYPE_CUDA:    return "hevc_cuviddec";
        case AV_HWDEVICE_TYPE_QSV:     return "hevc_qsv";
        case AV_HWDEVICE_TYPE_AMF:     return "hevc_amf";
        default: return nullptr;
        }
    case AV_CODEC_ID_VP9:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "vp9_d3d11va";
        case AV_HWDEVICE_TYPE_CUDA:    return "vp9_cuviddec";
        default: return nullptr;
        }
    case AV_CODEC_ID_AV1:
        switch (hwType) {
        case AV_HWDEVICE_TYPE_D3D11VA: return "av1_d3d11va";
        case AV_HWDEVICE_TYPE_CUDA:    return "av1_cuviddec";
        default: return nullptr;
        }
    default:
        return nullptr;
    }
}

static bool isCodecSupported(AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_HEVC:
    case AV_CODEC_ID_VP9:
    case AV_CODEC_ID_AV1:
        return true;
    default:
        return false;
    }
}

static bool isHWAccelPixelFormat(AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_CUDA:
    case AV_PIX_FMT_D3D11:
    case AV_PIX_FMT_QSV:
    case AV_PIX_FMT_DXVA2_VLD:
    case AV_PIX_FMT_VAAPI:
    case AV_PIX_FMT_AMF_SURFACE:
        return true;
    default:
        return false;
    }
}

static AVHWDeviceType hwDeviceTypeFromPixelFormat(AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_CUDA:   return AV_HWDEVICE_TYPE_CUDA;
    case AV_PIX_FMT_D3D11:  return AV_HWDEVICE_TYPE_D3D11VA;
    case AV_PIX_FMT_QSV:    return AV_HWDEVICE_TYPE_QSV;
    case AV_PIX_FMT_DXVA2_VLD: return AV_HWDEVICE_TYPE_DXVA2;
    case AV_PIX_FMT_VAAPI:  return AV_HWDEVICE_TYPE_VAAPI;
    case AV_PIX_FMT_AMF_SURFACE: return AV_HWDEVICE_TYPE_AMF;
    default:                return AV_HWDEVICE_TYPE_NONE;
    }
}

bool MediaSession::open(const QString &path)
{
    close();
    QByteArray pathBytes = path.toUtf8();
    if (avformat_open_input(&m_fmtCtx, pathBytes.constData(), nullptr, nullptr) != 0)
        return false;
    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        avformat_close_input(&m_fmtCtx);
        return false;
    }
    m_videoStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &m_codec, 0);
    if (m_videoStreamIdx < 0) {
        avformat_close_input(&m_fmtCtx);
        return false;
    }
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) { avformat_close_input(&m_fmtCtx); return false; }
    avcodec_parameters_to_context(m_codecCtx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);

    m_decoderInfo = {};
    m_hwPixFmt = AV_PIX_FMT_NONE;

    GPUInfo gpu = detectGPU();

    if (isCodecSupported(m_codec->id)) {
        auto order = probeOrder(gpu);

        for (AVHWDeviceType type : order) {
            AVBufferRef *hwCtx = nullptr;
            if (av_hwdevice_ctx_create(&hwCtx, type, nullptr, nullptr, 0) < 0)
                continue;

            const char *name = hwDecoderName(m_codec->id, type);
            if (!name) {
                av_buffer_unref(&hwCtx);
                continue;
            }

            const AVCodec *hwCodec = avcodec_find_decoder_by_name(name);
            if (!hwCodec) {
                av_buffer_unref(&hwCtx);
                continue;
            }

            AVCodecContext *hwCtx2 = avcodec_alloc_context3(hwCodec);
            if (!hwCtx2) {
                av_buffer_unref(&hwCtx);
                continue;
            }
            avcodec_parameters_to_context(hwCtx2, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);
            hwCtx2->hw_device_ctx = av_buffer_ref(hwCtx);

            if (avcodec_open2(hwCtx2, hwCodec, nullptr) < 0) {
                avcodec_free_context(&hwCtx2);
                av_buffer_unref(&hwCtx);
                continue;
            }

            avcodec_free_context(&m_codecCtx);
            m_codecCtx = hwCtx2;
            m_codec = hwCodec;
            m_hwPixFmt = m_codecCtx->pix_fmt;
            m_hwDeviceCtx = av_buffer_ref(hwCtx);
            av_buffer_unref(&hwCtx);
            break;
        }
    }

    if (!m_hwDeviceCtx) {
        if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
            avcodec_free_context(&m_codecCtx); avformat_close_input(&m_fmtCtx);
            return false;
        }
    }

    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;
    m_codecName = m_codec ? QString(m_codec->name) : QString();
    m_codecLongName = m_codec ? QString(m_codec->long_name) : QString();
    const char *prof = avcodec_profile_name(m_codec->id, m_codecCtx->profile);
    m_profileName = prof ? QString(prof) : QString("High");
    m_pixFmtName = QString(av_get_pix_fmt_name(m_codecCtx->pix_fmt));
    switch (m_codecCtx->colorspace) {
    case AVCOL_SPC_BT709: m_colorSpace = "BT.709"; break;
    case AVCOL_SPC_BT2020_NCL: m_colorSpace = "BT.2020 NCL"; break;
    case AVCOL_SPC_BT2020_CL: m_colorSpace = "BT.2020 CL"; break;
    case AVCOL_SPC_SMPTE170M: m_colorSpace = "SMPTE 170M"; break;
    case AVCOL_SPC_SMPTE240M: m_colorSpace = "SMPTE 240M"; break;
    case AVCOL_SPC_UNSPECIFIED: m_colorSpace = "Unspecified"; break;
    default: m_colorSpace = "Unknown"; break;
    }
    m_hdrType = "SDR";
    if (m_codecCtx->color_primaries == AVCOL_PRI_BT2020) {
        if (m_codecCtx->color_trc == AVCOL_TRC_SMPTE2084) m_hdrType = "HDR10";
        else if (m_codecCtx->color_trc == AVCOL_TRC_ARIB_STD_B67) m_hdrType = "HLG";
    }
    m_bitrate = m_codecCtx->bit_rate > 0 ? m_codecCtx->bit_rate : m_fmtCtx->bit_rate;
    m_rendererName = "OpenGL YUV";
    double dur = static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE;
    if (dur <= 0) {
        AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
        dur = static_cast<double>(m_fmtCtx->streams[m_videoStreamIdx]->duration) * tb.num / tb.den;
    }
    m_durationSec = dur;
    AVRational avg_fr = m_fmtCtx->streams[m_videoStreamIdx]->avg_frame_rate;
    m_fps = (avg_fr.den > 0) ? qRound(static_cast<double>(avg_fr.num) / avg_fr.den) : 24;

    AVPixelFormat swsSrcFmt = m_codecCtx->pix_fmt;
    m_decoderInfo.hardwareAccelerated = isHWAccelPixelFormat(m_codecCtx->pix_fmt);
    if (m_decoderInfo.hardwareAccelerated) {
        m_decoderInfo.backend = typeToBackend(hwDeviceTypeFromPixelFormat(m_codecCtx->pix_fmt));
        m_decoderInfo.decoderName = backendName(m_decoderInfo.backend);
        swsSrcFmt = AV_PIX_FMT_NV12;
    } else {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwPixFmt = AV_PIX_FMT_NONE;
        m_decoderInfo.backend = DecodeBackend::Software;
        m_decoderInfo.decoderName = "Software";
    }
    m_decoderInfo.gpuName = gpu.name;

    m_swsCtx = sws_getContext(m_width, m_height, swsSrcFmt,
                               m_width, m_height, AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);

    m_pkt = av_packet_alloc();
    m_decodedFrame = av_frame_alloc();
    m_totalFrames = 0;
    AVStream *st = m_fmtCtx->streams[m_videoStreamIdx];
    if (st->nb_frames > 0)
        m_totalFrames = static_cast<int>(st->nb_frames);
    else if (m_durationSec > 0 && m_fps > 0)
        m_totalFrames = qRound(m_durationSec * m_fps);
    m_audioStreamCount = 0; m_subtitleStreamCount = 0; m_videoStreamCount = 0;
    for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
        AVMediaType t = m_fmtCtx->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_AUDIO) m_audioStreamCount++;
        else if (t == AVMEDIA_TYPE_SUBTITLE) m_subtitleStreamCount++;
        else if (t == AVMEDIA_TYPE_VIDEO) m_videoStreamCount++;
    }
    m_audioStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIdx >= 0) {
        AVStream *ast = m_fmtCtx->streams[m_audioStreamIdx];
        m_audioSampleRate = ast->codecpar->sample_rate;
        m_audioChannels = ast->codecpar->ch_layout.nb_channels;
        m_audioCodecName = QString(avcodec_get_name(ast->codecpar->codec_id));
    }

    qDebug() << "GPU:" << gpu.name;
    qDebug() << "Decoder:" << m_decoderInfo.decoderName;
    qDebug() << "Hardware accelerated:" << (m_decoderInfo.hardwareAccelerated ? "yes" : "no");

    return true;
}

void MediaSession::close()
{
    av_frame_free(&m_decodedFrame); av_packet_free(&m_pkt);
    sws_freeContext(m_swsCtx); m_swsCtx = nullptr;
    sws_freeContext(m_convertCtx); m_convertCtx = nullptr;
    av_frame_free(&m_convertedFrame);
    avcodec_free_context(&m_codecCtx); avformat_close_input(&m_fmtCtx);
    av_buffer_unref(&m_hwDeviceCtx);
    m_videoStreamIdx = -1; m_audioStreamIdx = -1;
    m_audioSampleRate = 0; m_audioChannels = 0;
    m_width = m_height = 0; m_durationSec = 0.0; m_fps = 24;
    m_frame = QImage();
    m_audioStreamCount = 0; m_subtitleStreamCount = 0; m_videoStreamCount = 0;
    m_decoderInfo = {};
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_codecName.clear(); m_codecLongName.clear(); m_profileName.clear();
    m_pixFmtName.clear(); m_colorSpace.clear(); m_hdrType.clear();
    m_bitrate = 0; m_audioCodecName.clear(); m_rendererName.clear();
    m_isImageSequence = false;
    flushAudioQueue();
}

bool MediaSession::readFrame()
{
    if (!m_fmtCtx) return false;
    while (av_read_frame(m_fmtCtx, m_pkt) >= 0) {
        if (m_pkt->stream_index == m_videoStreamIdx) {
            if (avcodec_send_packet(m_codecCtx, m_pkt) == 0) {
                m_decodePerfTimer.start();
                int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
                m_lastDecodeMs = m_decodePerfTimer.nsecsElapsed() / 1000000.0;
                if (ret == 0) {
                    if (m_hwDeviceCtx && m_decodedFrame->hw_frames_ctx) {
                        AVFrame *swFrame = av_frame_alloc();
                        if (av_hwframe_transfer_data(swFrame, m_decodedFrame, 0) < 0) {
                            av_frame_free(&swFrame);
                            av_packet_unref(m_pkt);
                            return false;
                        }
                        av_frame_unref(m_decodedFrame);
                        av_frame_move_ref(m_decodedFrame, swFrame);
                        av_frame_free(&swFrame);
                    }
                    if (m_convertCtx && m_convertedFrame) {
                        sws_scale(m_convertCtx,
                                  m_decodedFrame->data, m_decodedFrame->linesize,
                                  0, m_height,
                                  m_convertedFrame->data, m_convertedFrame->linesize);
                    }
                    QImage img(m_width, m_height, QImage::Format_RGB888);
                    uint8_t *dstData[1] = { img.bits() };
                    int dstLinesize[1] = { static_cast<int>(img.bytesPerLine()) };
                    sws_scale(m_swsCtx, m_decodedFrame->data, m_decodedFrame->linesize,
                              0, m_height, dstData, dstLinesize);
                    m_frame = img.copy();
                    av_packet_unref(m_pkt);
                    return true;
                }
            }
            av_packet_unref(m_pkt);
        } else if (m_audioStreamIdx >= 0 && m_pkt->stream_index == m_audioStreamIdx) {
            AVPacket *copy = av_packet_alloc();
            av_packet_ref(copy, m_pkt);
            m_audioPacketQueue.push(copy);
            av_packet_unref(m_pkt);
        } else {
            av_packet_unref(m_pkt);
        }
    }
    return false;
}

void MediaSession::flushAudioQueue()
{
    while (!m_audioPacketQueue.empty()) {
        av_packet_free(&m_audioPacketQueue.front());
        m_audioPacketQueue.pop();
    }
}

bool MediaSession::popAudioPacket(AVPacket **pkt)
{
    if (m_audioPacketQueue.empty()) return false;
    *pkt = m_audioPacketQueue.front();
    m_audioPacketQueue.pop();
    return true;
}

void MediaSession::seekSec(double sec)
{
    if (!m_fmtCtx || m_videoStreamIdx < 0) return;
    int64_t ts = static_cast<int64_t>(sec * AV_TIME_BASE);
    if (ts < 0) ts = 0;
    av_seek_frame(m_fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecCtx);
    flushAudioQueue();
}

int64_t MediaSession::currentPts() const
{
    if (!m_decodedFrame) return AV_NOPTS_VALUE;
    int64_t pts = m_decodedFrame->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) pts = m_decodedFrame->pts;
    return pts;
}

int MediaSession::currentFrameNumber() const
{
    if (!m_fmtCtx || m_videoStreamIdx < 0 || !m_decodedFrame) return -1;
    int64_t pts = currentPts();
    if (pts == AV_NOPTS_VALUE) return -1;
    AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
    return qRound(pts * av_q2d(tb) * m_fps);
}

int MediaSession::frameCount() const { return m_totalFrames; }

bool MediaSession::seekToFrame(int frameNum)
{
    if (!m_fmtCtx || m_videoStreamIdx < 0) return false;
    if (frameNum < 0) frameNum = 0;
    double sec = frameNum / static_cast<double>(qMax(m_fps, 1));
    if (sec > m_durationSec) return false;
    seekSec(qMax(0.0, sec - 1.0 / qMax(m_fps, 1)));
    while (readFrame()) { if (currentFrameNumber() >= frameNum) return true; }
    return false;
}

bool MediaSession::stepForward() { return readFrame(); }

bool MediaSession::stepBackward()
{
    if (!m_fmtCtx || m_videoStreamIdx < 0) return false;
    int cur = currentFrameNumber();
    if (cur <= 0) return false;
    return seekToFrame(cur - 1);
}

MediaInfo MediaSession::mediaInfo() const
{
    MediaInfo info;
    if (!m_fmtCtx) return info;

    info.fileName = m_fmtCtx->url ? QString(m_fmtCtx->url) : QString();
    info.codecName = m_codecName;
    info.codecLongName = m_codecLongName;
    info.profile = m_profileName;
    info.width = m_width;
    info.height = m_height;
    info.fps = m_fps;
    info.durationSec = m_durationSec;
    info.pixelFormat = m_pixFmtName;
    info.colorSpace = m_colorSpace;
    info.hdrType = m_hdrType;
    info.bitrate = m_bitrate;
    info.decoder = m_decoderInfo.decoderName;
    if (!m_decoderInfo.gpuName.isEmpty())
        info.decoder += QString(" (%1)").arg(m_decoderInfo.gpuName);
    info.renderer = m_rendererName;
    info.audioFormat = m_audioCodecName;
    info.audioSampleRate = m_audioSampleRate;
    info.audioChannels = m_audioChannels;
    info.videoStreams = m_videoStreamCount;
    info.audioStreams = m_audioStreamCount;
    info.subtitleStreams = m_subtitleStreamCount;

    info.isImageSequence = m_isImageSequence;
    info.sequencePattern = m_fmtCtx->url ? QString(m_fmtCtx->url) : QString();
    info.sequenceFrameCount = m_totalFrames;

    return info;
}

AVFrame* MediaSession::displayFrame() const
{
    if (m_convertedFrame) return m_convertedFrame;
    return m_decodedFrame;
}

bool MediaSession::openImageSequence(const QString &pattern, int startNum, int frameCount, int frameRate)
{
    close();

    QByteArray patternBytes = pattern.toUtf8();

    AVDictionary *opts = nullptr;
    char startNumStr[32];
    snprintf(startNumStr, sizeof(startNumStr), "%d", startNum);
    av_dict_set(&opts, "start_number", startNumStr, 0);
    char fpsStr[32];
    snprintf(fpsStr, sizeof(fpsStr), "%d", frameRate);
    av_dict_set(&opts, "framerate", fpsStr, 0);

    if (avformat_open_input(&m_fmtCtx, patternBytes.constData(), nullptr, &opts) != 0) {
        av_dict_free(&opts);
        return false;
    }
    av_dict_free(&opts);

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_videoStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &m_codec, 0);
    if (m_videoStreamIdx < 0) {
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) { avformat_close_input(&m_fmtCtx); return false; }
    avcodec_parameters_to_context(m_codecCtx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;
    m_isImageSequence = true;

    AVPixelFormat srcFmt = m_codecCtx->pix_fmt;

    m_swsCtx = sws_getContext(m_width, m_height, srcFmt,
                               m_width, m_height, AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (srcFmt != AV_PIX_FMT_YUV420P && srcFmt != AV_PIX_FMT_NV12) {
        m_convertCtx = sws_getContext(m_width, m_height, srcFmt,
                                       m_width, m_height, AV_PIX_FMT_NV12,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
        m_convertedFrame = av_frame_alloc();
        m_convertedFrame->format = AV_PIX_FMT_NV12;
        m_convertedFrame->width = m_width;
        m_convertedFrame->height = m_height;
        av_frame_get_buffer(m_convertedFrame, 32);
    }

    m_fps = frameRate;
    m_totalFrames = frameCount;
    m_durationSec = (frameCount > 0 && frameRate > 0)
        ? static_cast<double>(frameCount) / frameRate : 0.0;

    m_pkt = av_packet_alloc();
    m_decodedFrame = av_frame_alloc();

    m_codecName = m_codec ? QString(m_codec->name) : QString();
    m_codecLongName = m_codec ? QString(m_codec->long_name) : QString();
    m_pixFmtName = QString(av_get_pix_fmt_name(m_codecCtx->pix_fmt));

    m_decoderInfo = {};
    m_decoderInfo.backend = DecodeBackend::Software;
    m_decoderInfo.decoderName = "Software";

    GPUInfo gpu = detectGPU();
    m_decoderInfo.gpuName = gpu.name;

    m_rendererName = "OpenGL YUV";

    return true;
}

// ============================================================
// MainWindow
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);
    setMinimumSize(640, 480);
    resize(1280, 720);

    m_thumbnailCache = new ThumbnailCache(this);
    m_hoverPreview = new HoverPreviewWidget(this);

    setupUI();
    setupMenus();

    updatePlaybackState();
    updateTitle();



    m_updateTimer = new QTimer(this);
    m_updateTimer->setTimerType(Qt::PreciseTimer);
    connect(m_updateTimer, &QTimer::timeout, this, &MainWindow::updateTimerTick);

    m_decodeTimer = new QTimer(this);
    m_decodeTimer->setTimerType(Qt::PreciseTimer);
    connect(m_decodeTimer, &QTimer::timeout, this, [this]() {
        if (!m_playing || !m_session.isOpen()) return;
        if (m_playDirection > 0) {
            if (!m_session.readFrame()) { m_playing = false; updatePlaybackState(); stopPlayback(); return; }
        } else if (m_playDirection < 0) {
            if (!m_session.stepBackward()) { m_playing = false; updatePlaybackState(); stopPlayback(); return; }
        } else { return; }
        applyCurrentFrame();
        m_currentFrameNum = m_session.currentFrameNumber();
        if (m_audioDecoder && m_audioOutput && m_playDirection > 0) {
            AVPacket *pkt = nullptr;
            while (m_session.popAudioPacket(&pkt)) {
                float buf[4096 * 2];
                int frames = m_audioDecoder->decode(pkt, buf, 4096);
                if (frames > 0) m_audioOutput->write(buf, frames);
                av_packet_free(&pkt);
            }
        }
        if (m_audioOutput && m_playDirection < 0) m_audioOutput->reset();
        if (m_loopOut >= 0 && m_currentFrameNum >= m_loopOut && m_loopIn >= 0 && m_playDirection > 0) {
            m_session.seekToFrame(m_loopIn);
            m_currentFrameNum = m_loopIn;
            applyCurrentFrame();
        }
    });

    m_fullscreenHideTimer = new QTimer(this);
    m_fullscreenHideTimer->setSingleShot(true);
    m_fullscreenHideTimer->setInterval(3000);
    connect(m_fullscreenHideTimer, &QTimer::timeout, this, [this]() {
        if (!m_isFullscreen || !m_controlsVisible) return;
        m_controlsVisible = false;
        fadeOverlayOut();
        setCursor(Qt::BlankCursor);
    });

    // Hover preview throttle — max 10 FPS (100ms)
    m_hoverThrottleTimer = new QTimer(this);
    m_hoverThrottleTimer->setSingleShot(true);
    m_hoverThrottleTimer->setInterval(100);
    connect(m_hoverThrottleTimer, &QTimer::timeout, this, [this]() {
        if (!m_hoverPending || !m_session.isOpen()) return;
        m_hoverPending = false;
        QImage thumb = m_thumbnailCache->thumbnail(m_hoverTargetTime);
        if (!thumb.isNull())
            m_hoverPreview->showThumbnail(thumb, m_hoverTargetTimestamp);
        else
            m_hoverPreview->show();
    });

    // P0: Live thumbnail update — push decoded thumbnails to hover preview immediately
    connect(m_thumbnailCache, &ThumbnailCache::thumbnailReady,
            this, [this](double timeSec) {
        if (m_hoverActive && qRound(timeSec) == qRound(m_hoverTargetTime)) {
            QImage thumb = m_thumbnailCache->thumbnail(m_hoverTargetTime);
            if (!thumb.isNull())
                m_hoverPreview->showThumbnail(thumb, m_hoverTargetTimestamp);
        }
    });

    m_videoWidget->installEventFilter(this);
    m_videoWidget->setMouseTracking(true);
    m_seekSlider->installEventFilter(this);
    m_controlsOverlay->installEventFilter(this);
    m_controlsOverlay->setMouseTracking(true);
    m_fsOverlay->installEventFilter(this);
    m_fsOverlay->setMouseTracking(true);
}

MainWindow::~MainWindow()
{
    m_playing = false;
    delete m_hoverPreview;
    delete m_audioDecoder;
    delete m_audioOutput;
    m_session.close();
}

// ============================================================
// Style Helpers
// ============================================================

QString MainWindow::buttonStyle() const
{
    return
        "QPushButton {"
        "  background: transparent;"
        "  border: none;"
        "  padding: 4px;"
        "  border-radius: 6px;"
        "}"
        "QPushButton:hover {"
        "  background: rgba(255,255,255,30);"
        "}"
        "QPushButton:pressed {"
        "  background: rgba(255,255,255,50);"
        "}";
}

static QPushButton* makeIconButton(const QIcon &icon, const QString &tip, int size = 28)
{
    auto *btn = new QPushButton;
    btn->setFixedSize(size, size);
    btn->setIcon(icon);
    btn->setIconSize(QSize(size - 8, size - 8));
    btn->setToolTip(tip);
    btn->setStyleSheet(
        "QPushButton { background: transparent; border: none; padding: 4px; border-radius: 6px; }"
        "QPushButton:hover { background: rgba(255,255,255,30); }"
        "QPushButton:pressed { background: rgba(255,255,255,50); }"
    );
    return btn;
}

static void animateIconClick(QPushButton *btn)
{
    QSize orig = btn->iconSize();
    int dw = qMax(1, orig.width() / 20);
    QSize big(orig.width() + dw, orig.height() + dw);
    auto *anim = new QVariantAnimation(btn);
    anim->setDuration(120);
    anim->setKeyValueAt(0.0, orig);
    anim->setKeyValueAt(0.5, big);
    anim->setKeyValueAt(1.0, orig);
    QObject::connect(anim, &QVariantAnimation::valueChanged, btn, [btn](const QVariant &v) {
        btn->setIconSize(v.toSize());
    });
    QObject::connect(btn, &QPushButton::clicked, anim, [anim]() { anim->start(); });
}

QString MainWindow::sliderStyle() const
{
    return
        "QSlider::groove:horizontal {"
        "  background: rgba(255,255,255,40);"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: white;"
        "  width: 14px;"
        "  height: 14px;"
        "  margin: -5px 0;"
        "  border-radius: 7px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #4a9eff;"
        "  height: 4px;"
        "  border-radius: 2px;"
        "}";
}

QWidget* MainWindow::createSeparator() const
{
    auto *sep = new QWidget;
    sep->setFixedWidth(1);
    sep->setMinimumHeight(20);
    sep->setStyleSheet("background: rgba(255,255,255,40);");
    return sep;
}

// ============================================================
// UI Setup — video is central widget, controls float on top
// ============================================================

void MainWindow::setupUI()
{
    // Video widget IS the central widget — fills entire window
    m_videoWidget = new VideoWidget(this);
    setCentralWidget(m_videoWidget);
    m_videoWidget->setBottomPadding(56);

    // ================================================================
    // Windowed mode overlay — full control bar at bottom
    // ================================================================
    m_controlsOverlay = new QWidget(this);
    m_controlsOverlay->setAttribute(Qt::WA_TranslucentBackground);
    m_controlsOverlay->setAttribute(Qt::WA_NativeWindow);
    m_controlsOverlay->setObjectName("controlsOverlay");
    m_controlsOverlay->setStyleSheet(
        "#controlsOverlay {"
        "  background: rgba(18,18,18,220);"
        "  border-top: 1px solid rgba(255,255,255,15);"
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
        "}"
    );
    m_controlsOverlay->setFixedHeight(56);

    auto *ctrlOpacity = new QGraphicsOpacityEffect(m_controlsOverlay);
    ctrlOpacity->setOpacity(1.0);
    m_controlsOverlay->setGraphicsEffect(ctrlOpacity);

    auto *ctrlLayout = new QVBoxLayout(m_controlsOverlay);
    ctrlLayout->setContentsMargins(12, 4, 12, 6);
    ctrlLayout->setSpacing(2);

    // Seek slider
    m_seekSlider = new SeekSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setMouseTracking(true);
    m_seekSlider->setStyleSheet(sliderStyle());
    connect(m_seekSlider, &QSlider::sliderPressed, this, &MainWindow::seekBySliderPressed);
    connect(m_seekSlider, &QSlider::sliderMoved, this, &MainWindow::seekBySlider);
    connect(m_seekSlider, &QSlider::sliderReleased, this, &MainWindow::seekBySliderReleased);
    ctrlLayout->addWidget(m_seekSlider);

    // Button row
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);

    // ---- Playback cluster ----
    // Back 10s
    m_skipBackBtn = makeIconButton(LunarIcons::seekBack(), "Back 10s");
    connect(m_skipBackBtn, &QPushButton::clicked, this, [this]() {
        if (!m_session.isOpen()) return;
        seekAndResume(qMax(0.0, m_currentPosSec - 10.0));
    });
    btnLayout->addWidget(m_skipBackBtn);

    // Spacer — separate seek from frame controls
    auto *spacer1 = new QWidget;
    spacer1->setFixedWidth(8);
    btnLayout->addWidget(spacer1);

    // Previous frame
    m_prevFrameBtn = makeIconButton(LunarIcons::framePrev(), "Previous Frame (,)");
    connect(m_prevFrameBtn, &QPushButton::clicked, this, [this]() {
        if (!m_session.isOpen()) return;
        stepAndResume(-1);
    });
    btnLayout->addWidget(m_prevFrameBtn);

    // Play/Pause (primary action — slightly larger)
    m_playBtn = makeIconButton(LunarIcons::play(), "Play", 32);
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    btnLayout->addWidget(m_playBtn);

    // Next frame
    m_nextFrameBtn = makeIconButton(LunarIcons::frameNext(), "Next Frame (.)");
    connect(m_nextFrameBtn, &QPushButton::clicked, this, [this]() {
        if (!m_session.isOpen()) return;
        stepAndResume(1);
    });
    btnLayout->addWidget(m_nextFrameBtn);

    // Spacer — separate frame from seek controls
    auto *spacer2 = new QWidget;
    spacer2->setFixedWidth(8);
    btnLayout->addWidget(spacer2);

    // Forward 10s
    m_skipForwardBtn = makeIconButton(LunarIcons::seekForward(), "Forward 10s");
    connect(m_skipForwardBtn, &QPushButton::clicked, this, [this]() {
        if (!m_session.isOpen()) return;
        seekAndResume(qMin(m_session.durationSec(), m_currentPosSec + 10.0));
    });
    btnLayout->addWidget(m_skipForwardBtn);

    // Subtle icon-scale animation on playback buttons
    animateIconClick(m_skipBackBtn);
    animateIconClick(m_prevFrameBtn);
    animateIconClick(m_playBtn);
    animateIconClick(m_nextFrameBtn);
    animateIconClick(m_skipForwardBtn);

    // ---- Info labels ----
    m_timeLabel = new QLabel("00:00.000 / 00:00.000");
    m_timeLabel->setStyleSheet("color: #ccc; font-size: 12px; font-family: 'Segoe UI', monospace;");
    btnLayout->addWidget(m_timeLabel);

    m_frameLabel = new QLabel;
    m_frameLabel->setStyleSheet("color: #888; font-size: 12px; font-family: 'Segoe UI', monospace;");
    btnLayout->addWidget(m_frameLabel);

    btnLayout->addStretch();

    // ---- Right-side utility cluster ----
    // Speed label (clickable → popup)
    m_speedLabel = new QLabel("1.0x");
    m_speedLabel->setObjectName("speedLabel");
    m_speedLabel->setStyleSheet(
        "#speedLabel {"
        "  color: #aaa;"
        "  font-size: 12px;"
        "  font-family: 'Segoe UI', monospace;"
        "  padding: 2px 8px;"
        "  border-radius: 4px;"
        "  background: rgba(255,255,255,20);"
        "}"
        "#speedLabel:hover {"
        "  background: rgba(255,255,255,40);"
        "  color: #ccc;"
        "}"
    );
    m_speedLabel->setToolTip("Click for speed options");
    m_speedLabel->setCursor(Qt::PointingHandCursor);
    m_speedLabel->installEventFilter(this);
    btnLayout->addWidget(m_speedLabel);

    // Speed popup menu — built once, cached
    m_speedMenu = new QMenu(this);
    m_speedMenu->setStyleSheet(
        "QMenu {"
        "  background: #2a2a2a;"
        "  color: #ddd;"
        "  border: 1px solid #555;"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  padding: 5px 24px 5px 12px;"
        "  border-radius: 4px;"
        "}"
        "QMenu::item:selected {"
        "  background: rgba(255,255,255,30);"
        "}"
        "QMenu::item:checked {"
        "  color: #4a9eff;"
        "}"
    );
    static const double kSpeeds[] = {0.25, 0.50, 0.75, 1.00, 1.25, 1.50, 1.75, 2.00, 4.00};
    auto *speedGroup = new QActionGroup(this);
    speedGroup->setExclusive(true);
    for (double s : kSpeeds) {
        auto *act = m_speedMenu->addAction(QString("%1x").arg(s, 0, 'f', 2));
        act->setCheckable(true);
        act->setData(s);
        speedGroup->addAction(act);
        if (qFuzzyCompare(s, m_speed)) act->setChecked(true);
    }
    connect(speedGroup, &QActionGroup::triggered, this, [this](QAction *act) {
        double newSpeed = act->data().toDouble();
        m_speed = newSpeed;
        m_speedLabel->setText(QString("%1x").arg(newSpeed, 0, 'f', 2));
        if (m_playing) startPlayback();
    });

    btnLayout->addWidget(createSeparator());

    // Volume button (mute toggle)
    m_volumeBtn = makeIconButton(LunarIcons::volumeHigh(), "Mute (M)");
    connect(m_volumeBtn, &QPushButton::clicked, this, [this]() {
        if (m_muteAction) m_muteAction->toggle();
    });
    btnLayout->addWidget(m_volumeBtn);

    // Volume slider
    m_volumeSlider = new QSlider(Qt::Horizontal);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(72);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: rgba(255,255,255,40); height: 3px; border-radius: 1px; }"
        "QSlider::handle:horizontal { background: white; width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; }"
        "QSlider::sub-page:horizontal { background: #4a9eff; height: 3px; border-radius: 1px; }"
    );
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int val) {
        if (m_audioOutput) m_audioOutput->setVolume(val / 100.0f);
        // Update volume icon
        QIcon volIcon = (val == 0) ? LunarIcons::volumeMute()
                       : (val < 50) ? LunarIcons::volumeLow()
                       : LunarIcons::volumeHigh();
        m_volumeBtn->setIcon(volIcon);
    });
    btnLayout->addWidget(m_volumeSlider);

    // Fullscreen button
    m_fullscreenBtn = makeIconButton(LunarIcons::fullscreen(), "Fullscreen (F)");
    connect(m_fullscreenBtn, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    btnLayout->addWidget(m_fullscreenBtn);

    ctrlLayout->addLayout(btnLayout);

    m_controlsOverlay->setVisible(false);
    m_controlsOverlay->show();
    positionControls();

    // ================================================================
    // Fullscreen overlay — minimal strip at bottom
    // ================================================================
    m_fsOverlay = new QWidget(this);
    m_fsOverlay->setAttribute(Qt::WA_NativeWindow);
    m_fsOverlay->setObjectName("fsOverlay");
    m_fsOverlay->setStyleSheet(
        "#fsOverlay {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 rgba(18,18,18,0),"
        "    stop:0.2 rgba(18,18,18,50),"
        "    stop:0.6 rgba(18,18,18,150),"
        "    stop:1 rgba(18,18,18,230));"
        "  border-top-left-radius: 12px;"
        "  border-top-right-radius: 12px;"
        "}"
    );
    m_fsOverlay->setFixedHeight(48);
    m_fsOverlay->hide();

    auto *fsLayout = new QHBoxLayout(m_fsOverlay);
    fsLayout->setContentsMargins(16, 6, 16, 6);
    fsLayout->setSpacing(12);

    m_fsPlayBtn = makeIconButton(LunarIcons::play(), "Play");
    connect(m_fsPlayBtn, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    fsLayout->addWidget(m_fsPlayBtn);

    m_fsSeekBar = new SeekSlider(Qt::Horizontal);
    m_fsSeekBar->setRange(0, 1000);
    m_fsSeekBar->setMouseTracking(true);
    m_fsSeekBar->setStyleSheet(sliderStyle());
    connect(m_fsSeekBar, &QSlider::sliderPressed, this, &MainWindow::seekBySliderPressed);
    connect(m_fsSeekBar, &QSlider::sliderMoved, this, &MainWindow::seekBySlider);
    connect(m_fsSeekBar, &QSlider::sliderReleased, this, &MainWindow::seekBySliderReleased);
    fsLayout->addWidget(m_fsSeekBar, 1);

    m_fsFullscreenBtn = makeIconButton(LunarIcons::fullscreenExit(), "Exit Fullscreen (F)");
    connect(m_fsFullscreenBtn, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    fsLayout->addWidget(m_fsFullscreenBtn);

    // ================================================================
    // Overlay fade animation — MUST be created before setGraphicsEffect
    // ================================================================
    m_overlayOpacity = new QGraphicsOpacityEffect(this);
    m_overlayOpacity->setOpacity(1.0);
    m_overlayAnim = new QPropertyAnimation(m_overlayOpacity, "opacity", this);
    m_overlayAnim->setDuration(200);

    connect(m_overlayAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_isFullscreen && m_overlayOpacity->opacity() < 0.01)
            m_fsOverlay->hide();
    });

    // Set the permanent opacity effect on fsOverlay (never replace this)
    m_fsOverlay->setGraphicsEffect(m_overlayOpacity);
}

void MainWindow::positionControls()
{
    if (!m_controlsOverlay || !m_fsOverlay) return;
    int cw = width();
    if (m_isFullscreen) {
        int ch = m_fsOverlay->height();
        m_fsOverlay->move(0, height() - ch);
        m_fsOverlay->resize(cw, ch);
    } else {
        int ch = m_controlsOverlay->height();
        m_controlsOverlay->move(0, height() - ch);
        m_controlsOverlay->resize(cw, ch);
    }
}

void MainWindow::fadeOverlayIn()
{
    if (m_isFullscreen) {
        if (m_overlayAnim->state() == QPropertyAnimation::Running)
            m_overlayAnim->stop();
        m_fsOverlay->show();
        m_fsOverlay->raise();
        m_overlayOpacity->setOpacity(1.0);
    }

    // Status bar
    statusBar()->setStyleSheet(
        "QStatusBar { background: #1a1a1a; color: #888; font-size: 11px; border-top: 1px solid #333; }"
    );
    statusBar()->setSizeGripEnabled(false);
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: #888; padding: 0 8px;");
    statusBar()->addPermanentWidget(m_statusLabel);
    statusBar()->hide();
}

void MainWindow::fadeOverlayOut()
{
    if (m_isFullscreen) {
        if (m_overlayAnim->state() == QPropertyAnimation::Running)
            m_overlayAnim->stop();
        m_overlayAnim->setStartValue(m_overlayOpacity->opacity());
        m_overlayAnim->setEndValue(0.0);
        m_overlayAnim->start();
    }
}

void MainWindow::updateOverlayMode()
{
    if (m_isFullscreen) {
        m_controlsOverlay->hide();
        // Sync fullscreen play button state
        QIcon fsPlayIcon(m_playing ? LunarIcons::pause() : LunarIcons::play());
        m_fsPlayBtn->setIcon(fsPlayIcon);
        m_fsPlayBtn->setToolTip(m_playing ? "Pause" : "Play");
        // Sync fullscreen seek bar
        m_fsSeekBar->blockSignals(true);
        m_fsSeekBar->setValue(m_seekSlider->value());
        m_fsSeekBar->blockSignals(false);
        positionControls();
    } else {
        m_controlsOverlay->show();
        m_fsOverlay->hide();
        // Sync windowed play button
        QIcon winPlayIcon(m_playing ? LunarIcons::pause() : LunarIcons::play());
        m_playBtn->setIcon(winPlayIcon);
        m_playBtn->setToolTip(m_playing ? "Pause" : "Play");
        // Sync windowed seek slider
        m_seekSlider->blockSignals(true);
        m_seekSlider->setValue(m_fsSeekBar->value());
        m_seekSlider->blockSignals(false);
        positionControls();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionControls();
}

// ============================================================
// Fullscreen
// ============================================================

void MainWindow::toggleFullscreen()
{
    if (m_isFullscreen) {
        if (m_overlayAnim->state() == QPropertyAnimation::Running)
            m_overlayAnim->stop();
        m_fullscreenHideTimer->stop();
        m_overlayOpacity->setOpacity(1.0);
        m_fsOverlay->hide();
        showNormal();
        m_isFullscreen = false;
        m_controlsVisible = false;
        menuBar()->show();
        unsetCursor();
        m_videoWidget->setBottomPadding(56);
        if (m_hoverPreview) m_hoverPreview->hidePreview();
        updateOverlayMode();
    } else {
        if (m_hoverPreview) m_hoverPreview->hidePreview();
        showFullScreen();
        m_isFullscreen = true;
        m_controlsVisible = true;
        menuBar()->hide();
        m_videoWidget->setBottomPadding(0);
        updateOverlayMode();
        m_fsOverlay->show();
        m_fsOverlay->raise();
        m_overlayOpacity->setOpacity(1.0);
        setCursor(Qt::ArrowCursor);
        m_fullscreenHideTimer->start();
    }
}

// ============================================================
// Menus
// ============================================================

void MainWindow::setupMenus()
{
    // Fluent-style QSS for all menus — QMenuBar, QMenu, QAction only
    qApp->setStyleSheet(qApp->styleSheet() + R"(
        QMenuBar {
            background: #202020;
            color: #F5F5F5;
            border: none;
            padding: 4px;
            spacing: 8px;
            font-size: 13px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 10px;
            border-radius: 6px;
        }
        QMenuBar::item:selected {
            background: rgba(255,255,255,0.08);
        }
        QMenuBar::item:pressed {
            background: rgba(255,255,255,0.12);
        }
        QMenu {
            background: #2B2B2B;
            color: #F5F5F5;
            border: 1px solid rgba(255,255,255,0.08);
            padding: 6px;
            border-radius: 10px;
        }
        QMenu::item {
            padding-top: 8px;
            padding-bottom: 8px;
            padding-left: 14px;
            padding-right: 40px;
            border-radius: 6px;
            min-height: 26px;
        }
        QMenu::item:selected {
            background: rgba(255,255,255,0.10);
        }
        QMenu::separator {
            height: 1px;
            background: rgba(255,255,255,0.08);
            margin-left: 8px;
            margin-right: 8px;
            margin-top: 6px;
            margin-bottom: 6px;
        }
        QMenu::right-arrow {
            width: 10px;
            height: 10px;
        }
    )");

    auto *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction(LunarIcons::open(), "&Open File...", this, &MainWindow::openFile, QKeySequence::Open);
    fileMenu->addAction("Open Image &Sequence...", this, &MainWindow::openImageSequence);
    fileMenu->addSeparator();
    fileMenu->addAction("&Close", this, &MainWindow::closeFile, QKeySequence("Ctrl+W"));
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close, QKeySequence::Quit);

    m_audioMenu = menuBar()->addMenu("&Audio");
    buildAudioMenu();

    m_videoMenu = menuBar()->addMenu("&Video");
    buildVideoMenu();

    m_subtitleMenu = menuBar()->addMenu("Su&btitle");
    buildSubtitleMenu();

    auto *toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Media &Information...", this, [this]() {
        MediaInfoDialog dlg(m_session.mediaInfo(), this);
        dlg.exec();
    });
    toolsMenu->addAction("Performance Information", this, [this]() {
        m_showPerformance = !m_showPerformance;
        m_videoWidget->setShowPerformance(m_showPerformance);
        if (m_showPerformance) updatePerformanceOverlay();
    });
    toolsMenu->addAction("Renderer Information");
    toolsMenu->addAction("Debug Console");

    auto *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About Lunar Player", this, [this]() {
        QMessageBox::about(this, "About Lunar Player",
            "Lunar Player\n\nA modern media player for animation, VFX, and editorial review.");
    });
    helpMenu->addAction("&Keyboard Shortcuts", this, [this]() {
        QMessageBox::information(this, "Keyboard Shortcuts",
            "Space       Play/Pause\n"
            "S           Stop\n"
            "F / F11     Fullscreen\n"
            "Esc         Exit Fullscreen\n"
            "J           Reverse\n"
            "K           Pause\n"
            "L           Forward\n"
            "E / .       Next Frame\n"
            ",           Previous Frame\n"
            "Left        -10 sec\n"
            "Right       +10 sec\n"
            "Shift+Left  -3 sec\n"
            "Shift+Right +3 sec\n"
            "Ctrl+Left   -1 min\n"
            "Ctrl+Right  +1 min\n"
            "Home        Start\n"
            "End         End\n"
            "[  / -      Slower\n"
            "]  / +      Faster\n"
            "0  / =      Normal Speed\n"
            "I  O        Loop In / Out\n"
            "Delete      Clear Loop\n"
            "M           Mute\n"
            "Up/Down     Volume\n"
            "N           Next File\n"
            "P           Previous File\n"
            "Ctrl+O      Open File\n"
            "Ctrl+W      Close File\n");
    });
}

void MainWindow::buildAudioMenu()
{
    m_audioMenu->clear();
    auto *trackMenu = m_audioMenu->addMenu("Audio &Track");
    m_audioTrackGroup = new QActionGroup(this);
    m_audioTrackGroup->setExclusive(true);
    if (m_session.audioStreamCount() > 0) {
        for (int i = 0; i < m_session.audioStreamCount(); ++i) {
            auto *act = trackMenu->addAction(QString("Track %1").arg(i + 1));
            act->setCheckable(true);
            act->setChecked(i == 0);
            m_audioTrackGroup->addAction(act);
        }
    } else {
        trackMenu->addAction("No Audio Tracks")->setEnabled(false);
    }
    m_audioMenu->addSeparator();
    auto *stereoMenu = m_audioMenu->addMenu("Stereo &Mode");
    m_stereoModeGroup = new QActionGroup(this);
    m_stereoModeGroup->setExclusive(true);
    for (const char *m : {"Stereo", "Left", "Right", "Mono"}) {
        auto *act = stereoMenu->addAction(QString(m));
        act->setCheckable(true);
        act->setChecked(QString(m) == "Stereo");
        m_stereoModeGroup->addAction(act);
    }
    m_audioMenu->addSeparator();
    m_muteAction = m_audioMenu->addAction("&Mute");
    m_muteAction->setCheckable(true);
    m_muteAction->setShortcut(QKeySequence("M"));
    connect(m_muteAction, &QAction::toggled, this, [this](bool muted) {
        if (m_audioOutput) m_audioOutput->setVolume(muted ? 0.0f : m_volumeSlider->value() / 100.0f);
        m_volumeBtn->setIcon(muted ? LunarIcons::volumeMute() : LunarIcons::volumeHigh());
    });
    m_audioMenu->addSeparator();
    m_audioMenu->addAction("Volume &Up", this, [this]() {
        m_volumeSlider->setValue(qMin(100, m_volumeSlider->value() + 10));
    }, QKeySequence("Up"));
    m_audioMenu->addAction("Volume &Down", this, [this]() {
        m_volumeSlider->setValue(qMax(0, m_volumeSlider->value() - 10));
    }, QKeySequence("Down"));
}

void MainWindow::buildVideoMenu()
{
    m_videoMenu->clear();
    auto *trackMenu = m_videoMenu->addMenu("Video &Track");
    auto *tg = new QActionGroup(this); tg->setExclusive(true);
    if (m_session.videoStreamCount() > 0) {
        for (int i = 0; i < m_session.videoStreamCount(); ++i) {
            auto *a = trackMenu->addAction(QString("Track %1").arg(i + 1));
            a->setCheckable(true); a->setChecked(i == 0); tg->addAction(a);
        }
    } else { trackMenu->addAction("No Video Tracks")->setEnabled(false); }
    m_videoMenu->addSeparator();
    m_videoMenu->addAction("&Fullscreen", this, &MainWindow::toggleFullscreen, QKeySequence("F11"));
    m_videoMenu->addSeparator();
    auto *zoomMenu = m_videoMenu->addMenu("&Zoom");
    m_zoomGroup = new QActionGroup(this); m_zoomGroup->setExclusive(true);
    for (const char *z : {"50%", "100%", "150%", "200%"}) {
        auto *a = zoomMenu->addAction(QString(z)); a->setCheckable(true);
        a->setChecked(QString(z) == "100%"); m_zoomGroup->addAction(a);
    }
    auto *aspectMenu = m_videoMenu->addMenu("&Aspect Ratio");
    m_aspectGroup = new QActionGroup(this); m_aspectGroup->setExclusive(true);
    for (const char *a : {"Auto", "16:9", "21:9", "4:3", "Original"}) {
        auto *act = aspectMenu->addAction(QString(a)); act->setCheckable(true);
        act->setChecked(QString(a) == "Auto"); m_aspectGroup->addAction(act);
    }
    m_videoMenu->addSeparator();
    m_videoMenu->addAction("Crop")->setEnabled(false);
    m_videoMenu->addSeparator();
    m_decoderInfoAction = m_videoMenu->addAction("Decoder: -");
    m_decoderInfoAction->setEnabled(false);
    m_rendererInfoAction = m_videoMenu->addAction("Renderer: OpenGL YUV");
    m_rendererInfoAction->setEnabled(false);
}

void MainWindow::buildSubtitleMenu()
{
    m_subtitleMenu->clear();
    m_subtitleMenu->addAction("&Load Subtitle File...", this, [this]() {
        QFileDialog::getOpenFileName(this, "Load Subtitle", QString(),
            "Subtitle Files (*.srt *.ass *.ssa *.sub *.vtt);;All Files (*)");
    });
    m_subtitleMenu->addSeparator();
    auto *trackMenu = m_subtitleMenu->addMenu("Subtitle &Track");
    if (m_session.subtitleStreamCount() > 0) {
        for (int i = 0; i < m_session.subtitleStreamCount(); ++i)
            trackMenu->addAction(QString("Track %1").arg(i + 1));
    } else { trackMenu->addAction("No Subtitle Tracks")->setEnabled(false); }
    m_subtitleMenu->addSeparator();
    m_enableSubtitlesAction = m_subtitleMenu->addAction("&Enable Subtitles");
    m_enableSubtitlesAction->setCheckable(true);
}

// ============================================================
// Context Menu
// ============================================================

void MainWindow::showContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    // Context menu inherits global Fluent QSS from qApp stylesheet
    QIcon playIcon(m_playing ? LunarIcons::pause() : LunarIcons::play());
    menu.addAction(playIcon, m_playing ? "Pause" : "Play", this, &MainWindow::togglePlayPause);
    menu.addSeparator();
    auto *audioSub = menu.addMenu(LunarIcons::volumeHigh(), "Audio");
    audioSub->addAction(LunarIcons::volumeMute(), "Mute", this, [this]() { m_muteAction->toggle(); });
    audioSub->addSeparator();
    audioSub->addAction("Volume Up", this, [this]() { m_volumeSlider->setValue(qMin(100, m_volumeSlider->value() + 10)); });
    audioSub->addAction("Volume Down", this, [this]() { m_volumeSlider->setValue(qMax(0, m_volumeSlider->value() - 10)); });
    auto *videoSub = menu.addMenu(LunarIcons::video(), "Video");
    videoSub->addAction(LunarIcons::fullscreen(), "Fullscreen", this, &MainWindow::toggleFullscreen);
    auto *subSub = menu.addMenu(LunarIcons::subtitle(), "Subtitle");
    subSub->addAction("Load Subtitle File...");
    menu.addSeparator();
    menu.addAction("Quit", this, &QWidget::close, QKeySequence::Quit);
    menu.exec(m_videoWidget->mapToGlobal(pos));
}

// ============================================================
// File Operations
// ============================================================

void MainWindow::closeFile()
{
    m_playing = false; stopPlayback();
    if (m_hoverPreview) m_hoverPreview->hidePreview();
    if (m_audioOutput) m_audioOutput->close();
    if (m_audioDecoder) m_audioDecoder->close();
    m_session.close();
    m_currentFilePath.clear();
    m_seqBaseName.clear();
    m_videoWidget->clearFrame();
    m_currentFrameNum = 0; m_playDirection = 1; m_speed = 1.0;
    m_loopIn = -1; m_loopOut = -1;
    m_markers.clear();
    m_seekSlider->clearMarkers();
    m_fsSeekBar->clearMarkers();
    if (m_decoderInfoAction) m_decoderInfoAction->setText("Decoder: -");
    statusBar()->hide();
    updatePlaybackState(); updateTitle(); updateInfoLabel();
}

void MainWindow::openFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Media", QString(),
        "All Media (*.mp4 *.mov *.avi *.mkv *.webm *.m4v *.ts *.mts *.png *.jpg *.jpeg *.tif *.tiff *.dpx *.exr *.hdr *.webp *.jxl);;"
        "Video Files (*.mp4 *.mov *.avi *.mkv *.webm *.m4v *.ts *.mts);;"
        "Image Files (*.png *.jpg *.jpeg *.tif *.tiff *.dpx *.exr *.hdr *.webp *.jxl);;"
        "All Files (*)");
    if (!path.isEmpty()) loadFile(path);
}

void MainWindow::openImageSequence()
{
    QString firstFile = QFileDialog::getOpenFileName(this, "Open Image Sequence — First Frame", QString(),
        "Image Files (*.png *.jpg *.jpeg *.tif *.tiff *.dpx *.exr *.hdr *.webp *.jxl);;All Files (*)");
    if (firstFile.isEmpty()) return;

    QFileInfo fi(firstFile);
    QString base = fi.completeBaseName();
    QString ext = fi.suffix();
    QString dir = fi.absolutePath();

    static QRegularExpression seqRe(R"(^(.*?)(\d+)$)");
    QRegularExpressionMatch m = seqRe.match(base);
    if (!m.hasMatch()) {
        QMessageBox::information(this, "Image Sequence",
            "File name does not end with a frame number.\n"
            "Expected pattern like: shot_0001.png");
        return;
    }

    QString prefix = m.captured(1);
    QString numStr = m.captured(2);
    int padding = numStr.length();
    int startNum = numStr.toInt();

    QString pattern = dir + "/" + prefix + "%0" + QString::number(padding) + "d." + ext;

    // Count matching files
    QStringList filters;
    filters << (prefix + "*." + ext);
    QDir directory(dir);
    QFileInfoList files = directory.entryInfoList(filters, QDir::Files, QDir::Name);

    int count = 0;
    int minNum = INT_MAX, maxNum = INT_MIN;
    for (const QFileInfo &f : files) {
        QString fn = f.completeBaseName();
        QRegularExpressionMatch fm = seqRe.match(fn);
        if (fm.hasMatch() && fm.captured(1) == prefix) {
            int num = fm.captured(2).toInt();
            minNum = qMin(minNum, num);
            maxNum = qMax(maxNum, num);
            count++;
        }
    }

    if (count < 2) {
        QMessageBox::information(this, "Image Sequence",
            "Only one matching frame found. Need at least 2 files for a sequence.");
        return;
    }

    int frameRate = 24;
    m_seqBaseName = prefix + "*." + ext;

    m_playing = false; m_updateTimer->stop(); m_decodeTimer->stop();
    if (!m_currentFilePath.isEmpty()) m_altFilePath = m_currentFilePath;
    m_session.close(); m_currentFilePath = firstFile;

    if (!m_session.openImageSequence(pattern, startNum, count, frameRate)) {
        QMessageBox::warning(this, "Error", "Could not open image sequence:\n" + pattern);
        updatePlaybackState(); updateTitle(); return;
    }

    updateTitle(); m_seekSlider->setValue(0);
    m_currentPosSec = 0.0; m_currentFrameNum = 0;
    m_playDirection = 1; m_speed = 1.0; m_loopIn = -1; m_loopOut = -1;
    m_session.readFrame();
    m_currentFrameNum = m_session.currentFrameNumber();
    m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.fps(), 1));
    applyCurrentFrame();
    m_thumbnailCache->configure(firstFile, m_session.videoStreamIndex(),
        m_session.width(), m_session.height(), m_session.durationSec());
    m_markers.clear();
    syncMarkersToSlider();
    buildAudioMenu(); buildVideoMenu(); buildSubtitleMenu();
    if (m_decoderInfoAction) {
        DecoderInfo di = m_session.decoderInfo();
        m_decoderInfoAction->setText("Decoder: " + di.decoderName);
    }
    // Image sequences start paused
    m_playing = false;
    updatePlaybackState();
    statusBar()->show();
    updateStatusBarForSequence();
}

void MainWindow::loadFile(const QString &path)
{
    m_playing = false; m_updateTimer->stop(); m_decodeTimer->stop();
    if (!m_currentFilePath.isEmpty()) m_altFilePath = m_currentFilePath;
    m_session.close(); m_currentFilePath = path;
    if (!m_session.open(path)) {
        QMessageBox::warning(this, "Error", "Could not open file:\n" + path);
        updatePlaybackState(); updateTitle(); return;
    }
    updateTitle(); m_seekSlider->setValue(0);
    m_currentPosSec = 0.0; m_currentFrameNum = 0;
    m_playDirection = 1; m_speed = 1.0; m_loopIn = -1; m_loopOut = -1;
    m_session.readFrame();
    m_currentFrameNum = m_session.currentFrameNumber();
    m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.fps(), 1));
    applyCurrentFrame();
    if (m_session.hasAudio()) {
        if (!m_audioDecoder) m_audioDecoder = new AudioDecoder();
        if (!m_audioOutput) m_audioOutput = new AudioOutput();
        m_audioDecoder->open(m_session.formatContext(), m_session.audioStreamIndex());
        m_audioOutput->open(m_audioDecoder->sampleRate(), m_audioDecoder->channels());
        m_audioOutput->setVolume(m_volumeSlider->value() / 100.0f);
    }
    m_thumbnailCache->configure(path, m_session.videoStreamIndex(),
        m_session.width(), m_session.height(), m_session.durationSec());
    m_markers.clear();
    syncMarkersToSlider();
    buildAudioMenu(); buildVideoMenu(); buildSubtitleMenu();
    if (m_decoderInfoAction) {
        DecoderInfo di = m_session.decoderInfo();
        m_decoderInfoAction->setText("Decoder: " + di.decoderName + (di.hardwareAccelerated && !di.gpuName.isEmpty() ? " (" + di.gpuName + ")" : ""));
    }
    m_playing = true; updatePlaybackState(); startPlayback();
}

void MainWindow::navigateFile(int direction)
{
    if (m_currentFilePath.isEmpty()) return;
    QFileInfo currentFile(m_currentFilePath);
    QDir dir = currentFile.absoluteDir();
    QStringList filters;
    filters << "*.mp4" << "*.mov" << "*.avi" << "*.mkv" << "*.webm" << "*.m4v" << "*.ts" << "*.mts" << "*.wmv" << "*.flv"
            << "*.png" << "*.jpg" << "*.jpeg" << "*.tif" << "*.tiff" << "*.dpx" << "*.exr" << "*.hdr" << "*.webp" << "*.jxl";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty()) return;

    // Build version-aware flat list
    // Each entry: (baseName, version, filePath) sorted by baseName then version
    struct NavEntry { QString base; int ver; QString path; };
    QVector<NavEntry> entries;
    QRegularExpression re(R"(^(.+?)[._-]v?(\d+)$)");
    QRegularExpression reRev(R"(^(.+?)[._-]rev(\d+)$)");
    QRegularExpression reR(R"(^(.+?)[._-]r(\d+)$)");

    for (const QFileInfo &fi : files) {
        QString base = fi.completeBaseName();
        int ver = 0;
        auto m = reRev.match(base);
        if (m.hasMatch()) { base = m.captured(1); ver = m.captured(2).toInt(); }
        else {
            m = reR.match(base);
            if (m.hasMatch()) { base = m.captured(1); ver = m.captured(2).toInt(); }
            else {
                m = re.match(base);
                if (m.hasMatch()) { base = m.captured(1); ver = m.captured(2).toInt(); }
            }
        }
        entries.append({base, ver, fi.absoluteFilePath()});
    }

    std::sort(entries.begin(), entries.end(), [](const NavEntry &a, const NavEntry &b) {
        int cmp = QString::compare(a.base, b.base, Qt::CaseInsensitive);
        if (cmp != 0) return cmp < 0;
        return a.ver < b.ver;
    });

    int idx = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].path == currentFile.absoluteFilePath()) { idx = i; break; }
    }
    if (idx < 0) return;
    int next = idx + direction;
    if (next < 0) next = entries.size() - 1;
    if (next >= entries.size()) next = 0;
    loadFile(entries[next].path);
}

void MainWindow::updatePerformanceOverlay()
{
    if (!m_showPerformance) return;
    FrameStats stats;
    if (m_videoWidget && m_videoWidget->renderer())
        stats = m_videoWidget->renderer()->frameStats();

    int thumbHits = m_thumbnailCache->cacheHits();
    int thumbMisses = m_thumbnailCache->cacheMisses();
    int thumbTotal = thumbHits + thumbMisses;
    double hitRate = (thumbTotal > 0) ? (100.0 * thumbHits / thumbTotal) : 0.0;

    QString text;
    text += QString("Decode: %1 ms\n").arg(m_session.lastDecodeMs(), 0, 'f', 1);
    text += QString("Render: %1 ms\n").arg(stats.lastFrameMs, 0, 'f', 1);
    text += QString("FPS: %1\n").arg(stats.fps, 0, 'f', 1);
    text += QString("Frame: %1\n").arg(m_currentFrameNum);
    text += QString("Thumb Hit: %1/%2 (%3%)\n").arg(thumbHits).arg(thumbTotal).arg(hitRate, 0, 'f', 0);
    text += QString("Thumb Decode: %1 ms\n").arg(m_thumbnailCache->lastBatchTimeMs(), 0, 'f', 1);
    text += QString("Cache: %1 entries\n").arg(m_thumbnailCache->cacheEntryCount());
    text += QString("Hover Handler: %1 ms (peak %2)")
        .arg(m_lastHoverHandlerMs, 0, 'f', 1)
        .arg(m_peakHoverHandlerMs, 0, 'f', 1);

    m_videoWidget->setPerformanceOverlay(text);
}

// ============================================================
// Markers
// ============================================================

void MainWindow::addMarker()
{
    if (!m_session.isOpen()) return;
    m_markers.append(m_currentPosSec);
    std::sort(m_markers.begin(), m_markers.end());
    syncMarkersToSlider();
}

void MainWindow::removeNearestMarker()
{
    if (!m_session.isOpen() || m_markers.isEmpty()) return;
    double bestDiff = 1e18;
    int bestIdx = -1;
    for (int i = 0; i < m_markers.size(); ++i) {
        double diff = qAbs(m_markers[i] - m_currentPosSec);
        if (diff < bestDiff) { bestDiff = diff; bestIdx = i; }
    }
    if (bestIdx >= 0) {
        m_markers.removeAt(bestIdx);
        syncMarkersToSlider();
    }
}

void MainWindow::seekNextMarker()
{
    if (!m_session.isOpen() || m_markers.isEmpty()) return;
    double best = 1e18;
    for (double t : m_markers) {
        if (t > m_currentPosSec + 0.001 && t < best) best = t;
    }
    if (best > 1e17) best = m_markers.first();
    seekAndResume(best);
}

void MainWindow::seekPrevMarker()
{
    if (!m_session.isOpen() || m_markers.isEmpty()) return;
    double best = -1.0;
    for (double t : m_markers) {
        if (t < m_currentPosSec - 0.001 && t > best) best = t;
    }
    if (best < 0) best = m_markers.last();
    seekAndResume(best);
}

void MainWindow::syncMarkersToSlider()
{
    double dur = m_session.durationSec();
    m_seekSlider->setMarkers(m_markers, dur);
    m_fsSeekBar->setMarkers(m_markers, dur);
}

// ============================================================
// Playback
// ============================================================

void MainWindow::startPlayback()
{
    double fd = (m_session.fps() > 0) ? (1.0 / m_session.fps()) : (1.0 / 24.0);
    m_decodeTimer->start(qMax(1, static_cast<int>((fd / m_speed) * 1000.0)));
    m_updateTimer->start(33);
}

void MainWindow::stopPlayback() { m_decodeTimer->stop(); m_updateTimer->stop(); }

void MainWindow::togglePlayPause()
{
    if (!m_session.isOpen()) { openFile(); return; }
    m_playing = !m_playing;
    if (m_playing) { m_playDirection = 1; startPlayback(); }
    else { stopPlayback(); if (m_audioOutput) m_audioOutput->reset(); }
    updatePlaybackState();
}

void MainWindow::seekBySliderPressed()
{
    if (!m_session.isOpen()) return;
    m_isScrubbing = true;
    m_wasPlayingBeforeScrub = m_playing;
    if (m_playing) {
        m_playing = false;
        stopPlayback();
        updatePlaybackState();
    }
    // Show video frame at the clicked position immediately
    double posSec = (static_cast<double>(m_seekSlider->value()) / 1000.0) * m_session.durationSec();
    m_currentPosSec = posSec;
    m_session.seekSec(posSec);
    if (m_session.readFrame()) {
        applyCurrentFrame();
        m_currentFrameNum = m_session.currentFrameNumber();
    }
    // Invalidate scrub timer so first drag ALWAYS decodes (immediate feedback)
    m_scrubTimer.invalidate();
}

void MainWindow::seekBySlider(int value)
{
    if (!m_session.isOpen()) return;
    double posSec = (static_cast<double>(value) / 1000.0) * m_session.durationSec();
    m_currentPosSec = posSec;

    // Update labels on every move
    int totalMs = static_cast<int>(m_session.durationSec() * 1000.0);
    int curMs = static_cast<int>(posSec * 1000.0);
    auto fmt = [](int ms) -> QString {
        int h = ms / 3600000, m = (ms % 3600000) / 60000, s = (ms % 60000) / 1000, ml = ms % 1000;
        if (h > 0) return QString("%1:%2:%3.%4").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(ml,3,10,QChar('0'));
        return QString("%1:%2.%3").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(ml,3,10,QChar('0'));
    };
    m_timeLabel->setText(fmt(curMs) + " / " + fmt(totalMs));
    m_frameLabel->setText(QString("F%1").arg(qRound(posSec * qMax(m_session.fps(), 1))));

    // Trigger thumbnail request
    int totalMs2 = static_cast<int>(m_session.durationSec() * 1000.0);
    int h = curMs / 3600000, m = (curMs % 3600000) / 60000, s = (curMs % 60000) / 1000;
    m_hoverTargetTimestamp = (h > 0)
        ? QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'))
        : QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    m_hoverTargetTime = posSec;
    if (!m_hoverThrottleTimer->isActive()) {
        m_hoverPending = true;
        m_hoverThrottleTimer->start();
    }

    // Throttled seek+decode: first drag always decodes, then 100ms throttle (10 FPS)
    if (m_scrubTimer.isValid()) {
        if (m_scrubTimer.elapsed() < 100)
            return;  // Too soon since last decode — labels + thumb only
        m_scrubTimer.restart();
    } else {
        m_scrubTimer.start();  // First call after press — always decodes
    }

    m_session.seekSec(posSec);
    if (m_session.readFrame()) {
        applyCurrentFrame();
        m_currentFrameNum = m_session.currentFrameNumber();
    }
}

void MainWindow::seekBySliderReleased()
{
    if (!m_session.isOpen()) return;
    m_scrubTimer.invalidate();
    m_session.seekSec(m_currentPosSec);
    if (m_session.readFrame()) {
        applyCurrentFrame();
        m_currentFrameNum = m_session.currentFrameNumber();
    }
    if (m_audioDecoder) m_audioDecoder->flush();
    if (m_audioOutput) m_audioOutput->reset();
    if (m_wasPlayingBeforeScrub && !m_playing) {
        m_playing = true;
        startPlayback();
        updatePlaybackState();
    }
    m_isScrubbing = false;
    m_wasPlayingBeforeScrub = false;
}

void MainWindow::applyCurrentFrame()
{
    if (m_videoWidget->rendererSupportsAVFrame() && m_session.displayFrame())
        m_videoWidget->setAVFrame(m_session.displayFrame());
    else if (!m_session.currentFrame().isNull())
        m_videoWidget->setFrame(m_session.currentFrame());
    syncSeekUI();
}

// ============================================================
// Seek UI Sync — single source of truth for seek-dependent UI
// ============================================================

void MainWindow::syncSeekUI()
{
    if (!m_session.isOpen()) return;
    int totalMs = static_cast<int>(m_session.durationSec() * 1000.0);
    int curMs = static_cast<int>(m_currentPosSec * 1000.0);
    int sv = (m_session.durationSec() > 0)
        ? static_cast<int>((m_currentPosSec / m_session.durationSec()) * 1000.0) : 0;
    // Only update the visible slider
    if (m_isFullscreen) {
        m_fsSeekBar->blockSignals(true);
        m_fsSeekBar->setValue(qBound(0, sv, 1000));
        m_fsSeekBar->blockSignals(false);
    } else {
        m_seekSlider->blockSignals(true);
        m_seekSlider->setValue(qBound(0, sv, 1000));
        m_seekSlider->blockSignals(false);
    }
    auto fmt = [](int ms) -> QString {
        int h = ms / 3600000, m = (ms % 3600000) / 60000, s = (ms % 60000) / 1000, ml = ms % 1000;
        if (h > 0) return QString("%1:%2:%3.%4").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(ml,3,10,QChar('0'));
        return QString("%1:%2.%3").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0')).arg(ml,3,10,QChar('0'));
    };
    m_timeLabel->setText(fmt(curMs) + " / " + fmt(totalMs));
    m_frameLabel->setText(QString("F%1").arg(m_currentFrameNum));
    updateInfoLabel();
    if (m_session.isImageSequence()) updateStatusBarForSequence();
    updatePerformanceOverlay();
}

// ============================================================
// Seek + Frame Step — preserve playback state
// ============================================================

void MainWindow::seekAndResume(double newPos)
{
    if (!m_session.isOpen()) return;
    bool wasPlaying = m_playing;
    if (wasPlaying) {
        m_playing = false;
        stopPlayback();
        updatePlaybackState();
    }
    m_session.seekSec(newPos);
    if (m_session.readFrame()) {
        m_currentFrameNum = m_session.currentFrameNumber();
        m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.fps(), 1));
        applyCurrentFrame();
    }
    if (m_audioDecoder) m_audioDecoder->flush();
    if (m_audioOutput) m_audioOutput->reset();
    if (wasPlaying) {
        m_playing = true;
        startPlayback();
        updatePlaybackState();
    }
}

void MainWindow::stepAndResume(int direction)
{
    if (!m_session.isOpen()) return;
    bool wasPlaying = m_playing;
    if (wasPlaying) {
        m_playing = false;
        stopPlayback();
        updatePlaybackState();
    }
    bool ok = (direction > 0) ? m_session.stepForward() : m_session.stepBackward();
    if (ok) {
        m_currentFrameNum = m_session.currentFrameNumber();
        m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.fps(), 1));
        applyCurrentFrame();
    }
    if (wasPlaying) {
        m_playing = true;
        startPlayback();
        updatePlaybackState();
    }
}

// ============================================================
// Timer Tick
// ============================================================

void MainWindow::updateTimerTick()
{
    if (!m_session.isOpen() || !m_playing) return;
    double fps = m_session.fps();
    double fd = (fps > 0) ? (1.0 / fps) : (1.0 / 24.0);
    m_currentPosSec = m_currentFrameNum * fd;
    syncSeekUI();
    QIcon playIcon(m_playing ? LunarIcons::pause() : LunarIcons::play());
    m_playBtn->setIcon(playIcon);
    m_fsPlayBtn->setIcon(playIcon);
    m_speedLabel->setText(QString("%1x").arg(m_speed, 0, 'f', 1));
}

void MainWindow::updateStatusBarForSequence()
{
    if (m_session.isImageSequence()) {
        int total = m_session.frameCount();
        int cur = m_currentFrameNum;
        m_statusLabel->setText(QString("%1  |  Frame %2 / %3  |  %4 FPS")
            .arg(m_seqBaseName)
            .arg(qMax(1, cur))
            .arg(qMax(1, total))
            .arg(m_session.fps()));
    }
}

void MainWindow::updateInfoLabel()
{
    // Frame number now shown in m_frameLabel, speed in m_speedLabel
    // This method is kept for compatibility but does nothing
}

void MainWindow::updatePlaybackState()
{
    if (!m_session.isOpen()) {
        m_playBtn->setIcon(LunarIcons::play());
        m_playBtn->setToolTip("Open File");
        if (m_fsPlayBtn) { m_fsPlayBtn->setIcon(LunarIcons::play()); m_fsPlayBtn->setToolTip("Open File"); }
        return;
    }
    QIcon playIcon(m_playing ? LunarIcons::pause() : LunarIcons::play());
    m_playBtn->setIcon(playIcon);
    m_playBtn->setToolTip(m_playing ? "Pause" : "Play");
    if (m_fsPlayBtn) {
        m_fsPlayBtn->setIcon(playIcon);
        m_fsPlayBtn->setToolTip(m_playing ? "Pause" : "Play");
    }
}

void MainWindow::updateTitle()
{
    if (m_session.isOpen()) {
        if (m_session.isImageSequence()) {
            setWindowTitle(QString("Lunar Player - %1 [Image Sequence]").arg(m_seqBaseName));
        } else {
            QString fn = QFileInfo(m_currentFilePath).fileName();
            setWindowTitle(QString("Lunar Player - %1").arg(fn.isEmpty() ? "Untitled" : fn));
        }
    } else {
        setWindowTitle("Lunar Player - No File");
}
}

// ============================================================
// Keyboard
// ============================================================

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    bool shift = event->modifiers() & Qt::ShiftModifier;
    bool ctrl = event->modifiers() & Qt::ControlModifier;

    switch (event->key()) {
    case Qt::Key_F11:
    case Qt::Key_F:
        if (!ctrl) { toggleFullscreen(); break; }
        QMainWindow::keyPressEvent(event); break;
    case Qt::Key_Space:
        togglePlayPause(); break;
    case Qt::Key_S:
        if (!ctrl) {
            m_playing = false; m_playDirection = 0;
            updatePlaybackState(); stopPlayback();
            if (m_audioOutput) m_audioOutput->reset();
        } break;
    case Qt::Key_J:
        if (ctrl) { m_showPerformance = !m_showPerformance; m_videoWidget->setShowPerformance(m_showPerformance); break; }
        if (m_session.isOpen()) { m_playing = true; m_playDirection = -1; updatePlaybackState(); startPlayback(); }
        break;
    case Qt::Key_K:
        m_playing = false; m_playDirection = 0; updatePlaybackState(); stopPlayback(); break;
    case Qt::Key_L:
        if (m_session.isOpen()) { m_playing = true; m_playDirection = 1; updatePlaybackState(); startPlayback(); }
        break;
    case Qt::Key_E:
        if (m_session.isOpen()) { stepAndResume(1); } break;
    case Qt::Key_Period:
        if (m_session.isOpen()) { stepAndResume(1); } break;
    case Qt::Key_Comma:
        if (m_session.isOpen()) { stepAndResume(-1); } break;
    case Qt::Key_Left:
        if (m_session.isOpen()) {
            double seekAmt = shift ? 3.0 : (ctrl ? 60.0 : 10.0);
            seekAndResume(qMax(0.0, m_currentPosSec - seekAmt));
        } break;
    case Qt::Key_Right:
        if (m_session.isOpen()) {
            double seekAmt = shift ? 3.0 : (ctrl ? 60.0 : 10.0);
            seekAndResume(qMin(m_session.durationSec(), m_currentPosSec + seekAmt));
        } break;
    case Qt::Key_Home:
        if (m_session.isOpen()) { seekAndResume(0.0); } break;
    case Qt::Key_End:
        if (m_session.isOpen()) { seekAndResume(m_session.durationSec()); } break;
    case Qt::Key_BracketLeft:
    case Qt::Key_Minus:
        if (ctrl) { seekPrevMarker(); break; }
        m_speed = qMax(0.25, m_speed / 1.5); if (m_playing) startPlayback(); break;
    case Qt::Key_BracketRight:
    case Qt::Key_Plus:
        if (ctrl) { seekNextMarker(); break; }
        m_speed = qMin(4.0, m_speed * 1.5); if (m_playing) startPlayback(); break;
    case Qt::Key_Equal:
    case Qt::Key_0:
        m_speed = 1.0; if (m_playing) startPlayback(); break;
    case Qt::Key_I:
        m_loopIn = m_currentFrameNum; updateInfoLabel(); break;
    case Qt::Key_O:
        m_loopOut = m_currentFrameNum; updateInfoLabel(); break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        m_loopIn = -1; m_loopOut = -1; updateInfoLabel(); break;
    case Qt::Key_Escape:
        if (m_isFullscreen) toggleFullscreen(); break;
    case Qt::Key_M:
        if (shift && ctrl) { removeNearestMarker(); break; }
        if (shift) { addMarker(); break; }
        if (m_muteAction) m_muteAction->toggle(); break;
    case Qt::Key_Up:
        m_volumeSlider->setValue(qMin(100, m_volumeSlider->value() + 5)); break;
    case Qt::Key_Down:
        m_volumeSlider->setValue(qMax(0, m_volumeSlider->value() - 5)); break;
    case Qt::Key_Tab:
        if (!m_altFilePath.isEmpty()) {
            double savedPos = m_currentPosSec;
            m_playing = false; m_updateTimer->stop(); m_decodeTimer->stop();
            if (m_audioOutput) m_audioOutput->close();
            if (m_audioDecoder) m_audioDecoder->close();
            m_session.close();
            std::swap(m_currentFilePath, m_altFilePath);
            if (m_session.open(m_currentFilePath)) {
                updateTitle(); m_seekSlider->setValue(0);
                m_playDirection = 1; m_speed = 1.0; m_loopIn = -1; m_loopOut = -1;
                m_session.seekSec(savedPos);
                if (m_session.readFrame()) {
                    m_currentFrameNum = m_session.currentFrameNumber();
                    m_currentPosSec = savedPos;
                    applyCurrentFrame();
                }
                if (m_session.hasAudio()) {
                    if (!m_audioDecoder) m_audioDecoder = new AudioDecoder();
                    if (!m_audioOutput) m_audioOutput = new AudioOutput();
                    m_audioDecoder->open(m_session.formatContext(), m_session.audioStreamIndex());
                    m_audioOutput->open(m_audioDecoder->sampleRate(), m_audioDecoder->channels());
                    m_audioOutput->setVolume(m_volumeSlider->value() / 100.0f);
                }
                m_thumbnailCache->configure(m_currentFilePath, m_session.videoStreamIndex(),
                    m_session.width(), m_session.height(), m_session.durationSec());
                m_markers.clear(); syncMarkersToSlider();
                buildAudioMenu(); buildVideoMenu(); buildSubtitleMenu();
                if (m_decoderInfoAction) {
                    DecoderInfo di = m_session.decoderInfo();
                    m_decoderInfoAction->setText("Decoder: " + di.decoderName + (di.hardwareAccelerated && !di.gpuName.isEmpty() ? " (" + di.gpuName + ")" : ""));
                }
                updatePlaybackState();
            }
        }
        break;
    case Qt::Key_N:
        if (!ctrl && !shift) { navigateFile(1); } break;
    case Qt::Key_P:
        if (!ctrl && !shift) { navigateFile(-1); } break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

// ============================================================
// Events
// ============================================================

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_playing = false; stopPlayback();
    if (m_hoverPreview) m_hoverPreview->hidePreview();
    if (m_audioOutput) m_audioOutput->close();
    if (m_audioDecoder) m_audioDecoder->close();
    m_session.close();
    event->accept();
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    showContextMenuAt(event->globalPos());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        QString path = urls.first().toLocalFile();
        if (!path.isEmpty()) loadFile(path);
    }
}

void MainWindow::showContextMenuAt(const QPoint &globalPos)
{
    QMenu cm;
    for (auto *action : menuBar()->actions()) {
        if (action->menu())
            cm.addAction(action);
    }
    cm.exec(globalPos);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // Video widget events — fullscreen mouse tracking + double-click
    if (obj == m_videoWidget) {
        if (m_isFullscreen && (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove)) {
            m_controlsVisible = true;
            // Only fade in (show+raise native window) if overlay is actually hidden
            if (m_fsOverlay->isHidden()) {
                fadeOverlayIn();
            }
            unsetCursor();
            // Hide hover preview when mouse is over video area (not timeline)
            if (m_hoverActive) {
                m_hoverActive = false;
                m_hoverPreview->hidePreview();
                m_hoverThrottleTimer->stop();
                m_hoverPending = false;
            }
            m_fullscreenHideTimer->start();
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                toggleFullscreen();
                return true;
            }
        }
        if (event->type() == QEvent::ContextMenu) {
            auto *ce = static_cast<QContextMenuEvent*>(event);
            showContextMenuAt(ce->globalPos());
            return true;
        }
    }

    // Fullscreen overlay — Enter/Leave + mouse move + context menu
    if (obj == m_fsOverlay) {
        if (event->type() == QEvent::ContextMenu) {
            auto *ce = static_cast<QContextMenuEvent*>(event);
            showContextMenuAt(ce->globalPos());
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::RightButton) {
                showContextMenuAt(me->globalPosition().toPoint());
                return true;
            }
        }
        if (m_isFullscreen && event->type() == QEvent::Enter) {
            m_fullscreenHideTimer->stop();
        }
        if (m_isFullscreen && event->type() == QEvent::Leave) {
            m_fullscreenHideTimer->start();
        }
        if (m_isFullscreen && (event->type() == QEvent::HoverMove || event->type() == QEvent::MouseMove)) {
            m_hoverTimer.start();
            auto *me = static_cast<QMouseEvent*>(event);
            m_controlsVisible = true;
            // Set overlay fully visible instantly — no animation restart churn
            if (m_overlayOpacity->opacity() < 1.0) {
                if (m_overlayAnim->state() == QPropertyAnimation::Running)
                    m_overlayAnim->stop();
                m_overlayOpacity->setOpacity(1.0);
            }
            // Only unset cursor when it was blanked by the hide timer
            if (cursor().shape() == Qt::BlankCursor)
                unsetCursor();
            // Show thumbnail preview above seek bar (skip during slider drag — seekBySlider handles it)
            if (m_session.isOpen() && !m_isScrubbing) {
                int overlayW = m_fsOverlay->width();
                int margin = 30;
                double ratio = static_cast<double>(me->globalPosition().x() - margin) /
                               qMax(1, overlayW - 2 * margin);
                ratio = qBound(0.0, ratio, 1.0);
                double timeSec = ratio * m_session.durationSec();
                int totalMs = static_cast<int>(timeSec * 1000.0);
                int h = totalMs / 3600000, m = (totalMs % 3600000) / 60000, s = (totalMs % 60000) / 1000;
                QString ts = (h > 0)
                    ? QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'))
                    : QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
                int thumbW = m_hoverPreview->width();
                QPoint localMouse = mapFromGlobal(me->globalPosition().toPoint());
                int px = localMouse.x() - thumbW / 2;
                int py = height() - m_fsOverlay->height() - m_hoverPreview->height() - 8;
                if (!m_hoverPreview->isVisible())
                    m_hoverPreview->raise();
                if (px != m_lastHoverX) {
                    m_hoverPreview->move(px, py);
                    m_lastHoverX = px;
                }
                if (!m_hoverPreview->isVisible())
                    m_hoverPreview->show();
                m_hoverActive = true;
                m_hoverTargetTime = timeSec;
                m_hoverTargetTimestamp = ts;
                if (!m_hoverThrottleTimer->isActive()) {
                    m_hoverPending = true;
                    m_hoverThrottleTimer->start();
                }
            }
            double elapsedMs = m_hoverTimer.elapsed();
            m_lastHoverHandlerMs = elapsedMs;
            if (elapsedMs > m_peakHoverHandlerMs)
                m_peakHoverHandlerMs = elapsedMs;
        }
    }

    // Seek slider hover preview — throttled to 10 FPS
    if (obj == m_seekSlider && m_session.isOpen()) {
        if (event->type() == QEvent::HoverMove || event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent*>(event);
            int sw = m_seekSlider->width();
            int margin = m_seekSlider->style()->pixelMetric(QStyle::PM_SliderLength) / 2;
            double ratio = static_cast<double>(me->pos().x() - margin) / qMax(1, sw - 2 * margin);
            ratio = qBound(0.0, ratio, 1.0);
            double timeSec = ratio * m_session.durationSec();

            int totalMs = static_cast<int>(timeSec * 1000.0);
            int h = totalMs / 3600000, m = (totalMs % 3600000) / 60000, s = (totalMs % 60000) / 1000;
            QString ts = (h > 0)
                ? QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'))
                : QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));

            int thumbW = m_hoverPreview->width();
            QPoint localMouse3 = mapFromGlobal(me->globalPosition().toPoint());
            int px = localMouse3.x() - thumbW / 2;
            int py = localMouse3.y() - m_hoverPreview->height() - 8;
            m_hoverPreview->raise();
            if (px != m_lastHoverX) {
                m_hoverPreview->move(px, py);
                m_lastHoverX = px;
            }
            m_hoverPreview->show();

            m_hoverActive = true;
            m_hoverTargetTime = timeSec;
            m_hoverTargetTimestamp = ts;
            if (!m_hoverThrottleTimer->isActive()) {
                m_hoverPending = true;
                m_hoverThrottleTimer->start();
            }
        } else if (event->type() == QEvent::Leave) {
            m_hoverActive = false;
            m_lastHoverX = -999999;
            m_hoverPreview->hidePreview();
            m_hoverThrottleTimer->stop();
            m_hoverPending = false;
        }
    }

    // Speed label click — show speed popup
    if (obj == m_speedLabel && event->type() == QEvent::MouseButtonPress) {
        // Update checkmark to current speed
        for (QAction *act : m_speedMenu->actions()) {
            act->setChecked(qFuzzyCompare(act->data().toDouble(), m_speed));
        }
        // Popup open upward from label
        QPoint pos = m_speedLabel->mapToGlobal(QPoint(0, 0));
        pos.ry() -= m_speedMenu->sizeHint().height();
        m_speedMenu->exec(pos);
        return true;
    }

    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::event(QEvent *e)
{
    // Window activate — restore preview after Alt-Tab / screenshot / focus loss
    if (e->type() == QEvent::WindowActivate && m_hoverActive && m_session.isOpen()) {
        QCursor cursor;
        QPoint glob = cursor.pos();

        // Check which widget the cursor is over
        QSlider *activeSlider = nullptr;
        if (m_isFullscreen && m_fsOverlay->isVisible()) {
            QPoint fsLocal = m_fsOverlay->mapFromGlobal(glob);
            if (fsLocal.x() >= 0 && fsLocal.x() < m_fsOverlay->width() &&
                fsLocal.y() >= 0 && fsLocal.y() < m_fsOverlay->height()) {
                activeSlider = m_fsSeekBar;
            }
        }
        if (!activeSlider) {
            QPoint seekLocal = m_seekSlider->mapFromGlobal(glob);
            if (seekLocal.x() >= 0 && seekLocal.x() < m_seekSlider->width() &&
                seekLocal.y() >= 0 && seekLocal.y() < m_seekSlider->height()) {
                activeSlider = m_seekSlider;
            }
        }

        if (activeSlider) {
            int sw = activeSlider->width();
            int margin = activeSlider->style()->pixelMetric(QStyle::PM_SliderLength) / 2;
            QPoint sliderLocal = activeSlider->mapFromGlobal(glob);
            double ratio = static_cast<double>(sliderLocal.x() - margin) / qMax(1, sw - 2 * margin);
            ratio = qBound(0.0, ratio, 1.0);
            double timeSec = ratio * m_session.durationSec();
            int totalMs = static_cast<int>(timeSec * 1000.0);
            int h = totalMs / 3600000, m = (totalMs % 3600000) / 60000, s = (totalMs % 60000) / 1000;
            QString ts = (h > 0)
                ? QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'))
                : QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
            QPoint localMouse = mapFromGlobal(glob);
            int thumbW = m_hoverPreview->width();
            int px = localMouse.x() - thumbW / 2;
            int py = (m_isFullscreen)
                ? height() - m_fsOverlay->height() - m_hoverPreview->height() - 8
                : localMouse.y() - m_hoverPreview->height() - 8;
            m_hoverPreview->raise();
            if (px != m_lastHoverX) {
                m_hoverPreview->move(px, py);
                m_lastHoverX = px;
            }
            m_hoverPreview->show();
            m_hoverTargetTime = timeSec;
            m_hoverTargetTimestamp = ts;
            if (!m_hoverThrottleTimer->isActive()) {
                m_hoverPending = true;
                m_hoverThrottleTimer->start();
            }
        }
    }
    return QMainWindow::event(e);
}












