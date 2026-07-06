#include "MainWindow.h"
#include "GradientOverlay.h"
#include "VideoWidget.h"
#include "ThumbnailCache.h"
#include "HoverPreviewWidget.h"
#include "FullscreenCommandPanel.h"
#include "Icons.h"
#include "MediaInfoDialog.h"
#include "SequenceFrameCache.h"
#include "CompareController.h"
#include "CompareWidget.h"
#include "subtitles/SubtitleManager.h"
#include "subtitles/SubtitleFrame.h"
#include "renderer/Renderer.h"
#include "renderer/ColorManager.h"
#include "ui/SeekStressTest.h"
#include <cinttypes>
#include <QRandomGenerator>
#include "decoder/AudioDecoder.h"
#include "audio/AudioEngine.h"
#include "audio/AudioDeviceManager.h"
#include "decoder/DecoderManager.h"
#include "decoder/DecodeThread.h"
#include "audio/AudioOutput.h"
#include "update/UpdateManager.h"
#include "update/UpdateDialog.h"
#include <QApplication>
#include <QGuiApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QMenu>
#include <QToolButton>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSettings>
#include <QMimeData>
#include <numeric>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavformat/avformat.h>
}
#include <QCloseEvent>
#include <QFileInfo>
#include <QStyle>
#include <QMessageBox>
#include <QEvent>
#include <QMouseEvent>
#include <QCheckBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QDialogButtonBox>
#include <QScreen>
#include <QActionGroup>
#include <QStackedLayout>
#include <QPropertyAnimation>
#include <QElapsedTimer>
#include <QStatusBar>
#include <QRegularExpression>
#include <QDir>
#include <QDebug>
#include <climits>
#include <cmath>
#include "network/NetworkMediaSession.h"
#include "StreamingOverlay.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif


// ============================================================
// Dynamic FFmpeg-based file filters
// ============================================================

static QStringList s_mediaFilters;
static QStringList s_navFilters;
static bool s_filtersBuilt = false;

static void buildFiltersOnce()
{
    if (s_filtersBuilt) return;
    s_filtersBuilt = true;

    QSet<QString> allExts;

    // Primary source: all extensions advertised by FFmpeg demuxers
    const AVInputFormat *fmt = nullptr;
    void *opaque = nullptr;
    while ((fmt = av_demuxer_iterate(&opaque))) {
        if (!fmt->extensions) continue;
        for (const QString &ext : QString::fromUtf8(fmt->extensions).split(',')) {
            QString e = ext.trimmed().toLower();
            if (!e.isEmpty()) allExts.insert(e);
        }
    }

    // Supplemental: extensions FFmpeg demuxers may not advertise
    static const char *kSupplement[] = {
        "webm", "m4v", "jxl", "qoi", "heic", "heif", "avif", "opus",
        "png", "jpg", "jpeg", "bmp", "gif", "tif", "tiff",
        "webp", "dpx", "exr", "hdr", "tga"
    };
    for (const char *s : kSupplement) allExts.insert(QString::fromLatin1(s));

    // Build single universal filter
    QStringList allSorted = allExts.values();
    allSorted.sort();
    QString filter = "Supported Media Files (*." + allSorted.join(" *.") + ")";
    s_mediaFilters = {filter, "All Files (*)"};

    // Nav filter: all extensions for directory navigation
    for (const QString &e : allSorted)
        s_navFilters << ("*." + e);
}

void MainWindow::initMediaFilters()
{
    buildFiltersOnce();
}

QStringList MainWindow::buildMediaFilters()
{
    buildFiltersOnce();
    return s_mediaFilters;
}

QStringList MainWindow::buildNavFilters()
{
    buildFiltersOnce();
    return s_navFilters;
}

// ============================================================
// MediaSession
// ============================================================

MediaSession::~MediaSession() { close(); }

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

bool MediaSession::open(const QString &path)
{
    close();
    m_lastError.clear();
    m_decodeState = DecodeState::Reading;
    m_packetsSkipped = 0;
    m_framesDropped = 0;
    m_totalFramesDecoded = 0;
    m_totalDecodeMs = 0.0;
    m_peakDecodeMs = 0.0;

    QByteArray pathBytes = path.toUtf8();

    // Stage 1: avformat_open_input
    int ret = avformat_open_input(&m_fmtCtx, pathBytes.constData(), nullptr, nullptr);
    if (ret < 0) {
        char errBuf[256] = {};
        av_strerror(ret, errBuf, sizeof(errBuf));
        m_lastError = QString("Failed to open input: %1").arg(QString::fromUtf8(errBuf));
        return false;
    }

    // Stage 2: avformat_find_stream_info
    ret = avformat_find_stream_info(m_fmtCtx, nullptr);
    if (ret < 0) {
        char errBuf[256] = {};
        av_strerror(ret, errBuf, sizeof(errBuf));
        m_lastError = QString("Failed to read stream info: %1").arg(QString::fromUtf8(errBuf));
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    // Stage 3: Find best video stream
    m_videoStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &m_codec, 0);
    if (m_videoStreamIdx < 0) {
        m_lastError = "No video stream found in file";
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    if (!m_codec) {
        AVCodecParameters *par = m_fmtCtx->streams[m_videoStreamIdx]->codecpar;
        m_lastError = QString("No decoder available for codec: %1")
            .arg(QString(avcodec_get_name(par->codec_id)));
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    // Stage 4: Allocate codec context
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) {
        m_lastError = "Failed to allocate decoder context";
        avformat_close_input(&m_fmtCtx);
        return false;
    }
    avcodec_parameters_to_context(m_codecCtx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);

    // Stage 5: GPU detection
    m_gpuInfo = detectGPU();

    // Stage 6: Decoder selection (HW + SW probing)
    DecoderConfig decCfg;
    decCfg.codecpar = m_fmtCtx->streams[m_videoStreamIdx]->codecpar;
    decCfg.codecId = m_codec->id;
    decCfg.width = m_codecCtx->width;
    decCfg.height = m_codecCtx->height;
    decCfg.bitrate = m_codecCtx->bit_rate;

    DecoderSelection sel = DecoderManager::selectDecoder(decCfg);
    if (!sel.valid) {
        m_lastError = QString("Failed to initialize decoder for %1 (all backends failed)")
            .arg(m_codecName);
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    avcodec_free_context(&m_codecCtx);
    m_codecCtx = sel.codecCtx;
    m_codec = sel.codec;
    m_hwDeviceCtx = sel.hwDeviceCtx;
    m_hwPixFmt = sel.hwPixFmt;
    m_decoderInfo = sel.info;

    fprintf(stderr, "[MEDIA] Decoder: %s, hw=%d, pixfmt=%d, hwPixFmt=%d\n",
            m_codec ? m_codec->name : "null", sel.info.hardwareAccelerated,
            m_codecCtx->pix_fmt, m_hwPixFmt);

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
    m_containerFormat = m_fmtCtx->iformat ? QString::fromUtf8(m_fmtCtx->iformat->name) : QString();
    m_containerLongName = m_fmtCtx->iformat ? QString::fromUtf8(m_fmtCtx->iformat->long_name) : QString();
    m_rendererName = "OpenGL YUV";
    double dur = static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE;
    if (dur <= 0) {
        AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
        dur = static_cast<double>(m_fmtCtx->streams[m_videoStreamIdx]->duration) * tb.num / tb.den;
    }
    m_durationSec = dur;
    AVRational avg_fr = m_fmtCtx->streams[m_videoStreamIdx]->avg_frame_rate;
    m_fps = (avg_fr.den > 0) ? qRound(static_cast<double>(avg_fr.num) / avg_fr.den) : 24;

    // Stage 7: Create SWS context
    AVPixelFormat swsSrcFmt = m_codecCtx->pix_fmt;
    bool isHW = isHWAccelPixelFormat(swsSrcFmt);
    if (isHW) {
        swsSrcFmt = AV_PIX_FMT_NV12;
    }

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

    // Stage 8: Count streams
    m_audioStreamCount = 0; m_subtitleStreamCount = 0; m_videoStreamCount = 0;
    for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
        AVMediaType t = m_fmtCtx->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_AUDIO) m_audioStreamCount++;
        else if (t == AVMEDIA_TYPE_SUBTITLE) m_subtitleStreamCount++;
        else if (t == AVMEDIA_TYPE_VIDEO) m_videoStreamCount++;
    }

    // Stage 9: Find best audio stream
    m_audioStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_audioStreamIdx >= 0) {
        AVStream *ast = m_fmtCtx->streams[m_audioStreamIdx];
        AVCodecParameters *ap = ast->codecpar;
        m_audioSampleRate = ap->sample_rate;
        m_audioChannels = ap->ch_layout.nb_channels;
        m_audioCodecName = QString(avcodec_get_name(ap->codec_id));
        const AVCodecDescriptor *cd = avcodec_descriptor_get(ap->codec_id);
        m_audioCodecLongName = cd ? QString::fromUtf8(cd->long_name) : m_audioCodecName;
        char chlBuf[128] = {};
        av_channel_layout_describe(&ap->ch_layout, chlBuf, sizeof(chlBuf));
        m_audioChannelLayout = QString::fromUtf8(chlBuf);
        m_audioSampleFormat = QString(av_get_sample_fmt_name(static_cast<AVSampleFormat>(ap->format)));
        m_audioBitDepth = av_get_bytes_per_sample(static_cast<AVSampleFormat>(ap->format)) * 8;
        m_audioBitrate = ap->bit_rate;
        m_audioFrameSize = ap->frame_size;
        m_audioBlockAlign = ap->block_align;

        const AVCodecDescriptor *desc = avcodec_descriptor_get(ap->codec_id);
        if (desc && desc->profiles) {
            for (int i = 0; desc->profiles[i].profile != -99; i++) {
                if (desc->profiles[i].profile == ap->profile) {
                    m_audioProfile = QString::fromUtf8(desc->profiles[i].name);
                    break;
                }
            }
        }
    }

    return true;
}

void MediaSession::close()
{
    av_frame_free(&m_decodedFrame); av_packet_free(&m_pkt);
    sws_freeContext(m_swsCtx); m_swsCtx = nullptr;
    sws_freeContext(m_convertCtx); m_convertCtx = nullptr;
    av_frame_free(&m_convertedFrame);
    avcodec_free_context(&m_codecCtx);
    avformat_close_input(&m_fmtCtx);
    av_buffer_unref(&m_hwDeviceCtx);
    m_videoStreamIdx = -1; m_audioStreamIdx = -1;
    m_audioSampleRate = 0; m_audioChannels = 0;
    m_width = m_height = 0; m_durationSec = 0.0; m_fps = 24;
    m_frame = QImage();
    m_audioStreamCount = 0; m_subtitleStreamCount = 0; m_videoStreamCount = 0;
    m_codecName.clear(); m_codecLongName.clear(); m_profileName.clear();
    m_pixFmtName.clear(); m_colorSpace.clear(); m_hdrType.clear();
    m_bitrate = 0; m_audioCodecName.clear(); m_audioCodecLongName.clear();
    m_audioChannelLayout.clear(); m_audioSampleFormat.clear(); m_audioProfile.clear();
    m_audioBitDepth = 0; m_audioBitrate = 0; m_audioFrameSize = 0; m_audioBlockAlign = 0;
    m_rendererName.clear();
    m_containerFormat.clear(); m_containerLongName.clear();
    m_isImageSequence = false;
    m_decodeState = DecodeState::Reading;
    m_totalFramesDecoded = 0; m_packetsSkipped = 0; m_framesDropped = 0;
    m_seeksCount = 0; m_seekLatencyMs = 0.0;
    m_totalDecodeMs = 0.0; m_peakDecodeMs = 0.0;
    m_decoderInfo = {};
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_codecCtx = nullptr;
    m_codec = nullptr;
    flushAudioQueue();
    flushFrameQueue();
}

// Shared: process a just-decoded frame into m_frame
static bool processDecodedFrame(MediaSession *s, AVFrame *frame, int w, int h,
                                AVBufferRef *hwDev, SwsContext *sws, SwsContext *convertCtx,
                                AVFrame *convertedFrame, QImage &out,
                                bool skipRGBConversion = false)
{
    if (hwDev && frame->hw_frames_ctx) {
        // Save metadata before HW transfer (av_hwframe_transfer_data doesn't copy PTS)
        int64_t savedPts = frame->pts;
        int64_t savedBestEffort = frame->best_effort_timestamp;
        AVRational savedTimeBase = s->formatContext()->streams[s->videoStreamIndex()]->time_base;

        AVFrame *swFrame = av_frame_alloc();
        if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
            av_frame_free(&swFrame);
            return false;
        }
        // Restore metadata that av_hwframe_transfer_data doesn't copy
        swFrame->pts = savedPts;
        swFrame->best_effort_timestamp = savedBestEffort;

        av_frame_unref(frame);
        av_frame_move_ref(frame, swFrame);
        av_frame_free(&swFrame);
    }
    if (convertCtx && convertedFrame) {
        sws_scale(convertCtx, frame->data, frame->linesize, 0, h,
                  convertedFrame->data, convertedFrame->linesize);
    }
    if (skipRGBConversion)
        return true;
    QImage img(w, h, QImage::Format_RGB888);
    uint8_t *dstData[1] = { img.bits() };
    int dstLinesize[1] = { static_cast<int>(img.bytesPerLine()) };
    sws_scale(sws, frame->data, frame->linesize, 0, h, dstData, dstLinesize);
    out = img.copy();
    return true;
}

bool MediaSession::readFrame()
{
    if (!m_fmtCtx) { fprintf(stderr, "[PIPE] readFrame: fmtCtx is null\n"); return false; }

    static int64_t s_frameNum = 0;

    auto updatePtsFromDecodedFrame = [this]() {
        if (m_decodedFrame) {
            if (m_decodedFrame->pts != AV_NOPTS_VALUE) {
                AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
                m_lastDecodedPtsSec = static_cast<double>(m_decodedFrame->pts) * av_q2d(tb);
            } else if (m_decodedFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
                AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
                m_lastDecodedPtsSec = static_cast<double>(m_decodedFrame->best_effort_timestamp) * av_q2d(tb);
            }
        }
    };

    // ---- Phase 1: Read and decode packets until we get a frame or hit EOF ----
    if (m_decodeState == DecodeState::Reading) {
        int packetCount = 0;
        static const int MAX_PACKETS_PER_TICK = 500;
        while (av_read_frame(m_fmtCtx, m_pkt) >= 0) {
            ++packetCount;
            if (packetCount > MAX_PACKETS_PER_TICK) {
                fprintf(stderr, "[PIPE] safety: hit %d packets in one readFrame call, yielding\n", MAX_PACKETS_PER_TICK);
                break;
            }
            if (m_pkt->stream_index == m_videoStreamIdx) {
                int sendRet = avcodec_send_packet(m_codecCtx, m_pkt);
                av_packet_unref(m_pkt);
                if (sendRet == AVERROR(EAGAIN)) {
                    // Decoder input buffer full — must drain output before sending more
                    m_decodePerfTimer.start();
                    int drainRet = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
                    m_lastDecodeMs = m_decodePerfTimer.nsecsElapsed() / 1000000.0;
                    if (drainRet == 0) {
                        m_totalDecodeMs += m_lastDecodeMs;
                        if (m_lastDecodeMs > m_peakDecodeMs) m_peakDecodeMs = m_lastDecodeMs;
                        ++m_totalFramesDecoded;
                        ++s_frameNum;
                        {
                            QElapsedTimer swsTimer; swsTimer.start();
                            if (processDecodedFrame(this, m_decodedFrame, m_width, m_height,
                                                    m_hwDeviceCtx, m_swsCtx, m_convertCtx,
                                                    m_convertedFrame, m_frame,
                                                    m_skipRGBConversion))
                            {
                                m_lastSwsScaleMs = swsTimer.nsecsElapsed() / 1000000.0;
                                updatePtsFromDecodedFrame();
                                return true;
                            }
                        }
                        ++m_framesDropped;
                        fprintf(stderr, "[PIPE] F%" PRId64 " EAGAIN-drain: processDecodedFrame failed (drop #%d)\n", s_frameNum, m_framesDropped);
                    } else {
                        fprintf(stderr, "[PIPE] F%" PRId64 " send_packet=EAGAIN, drain=%d\n", s_frameNum, drainRet);
                    }
                    // Now retry sending the packet we just read — but we already unreffed it.
                    // Since the decoder buffer has space now, the next av_read_frame will give us the next packet.
                    // To resend this same packet, we need to NOT unref it before retry.
                    // Actually per FFmpeg docs: after draining, the NEXT send_packet will succeed.
                    // We already consumed the packet, so continue to read the next one.
                    continue;
                }
                if (sendRet < 0) {
                    // Other send error (corrupt, etc.) — skip
                    ++m_packetsSkipped;
                    continue;
                }
                m_decodePerfTimer.start();
                int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
                m_lastDecodeMs = m_decodePerfTimer.nsecsElapsed() / 1000000.0;
                if (ret == 0) {
                    m_totalDecodeMs += m_lastDecodeMs;
                    if (m_lastDecodeMs > m_peakDecodeMs) m_peakDecodeMs = m_lastDecodeMs;
                    ++m_totalFramesDecoded;
                    ++s_frameNum;
                    {
                        QElapsedTimer swsTimer; swsTimer.start();
                        if (processDecodedFrame(this, m_decodedFrame, m_width, m_height,
                                                m_hwDeviceCtx, m_swsCtx, m_convertCtx,
                                                m_convertedFrame, m_frame,
                                                m_skipRGBConversion))
                        {
                            m_lastSwsScaleMs = swsTimer.nsecsElapsed() / 1000000.0;
                            updatePtsFromDecodedFrame();
                            return true;
                        }
                    }
                    ++m_framesDropped; // conversion failure
                    fprintf(stderr, "[PIPE] F%" PRId64 " processDecodedFrame failed (drop #%d)\n", s_frameNum, m_framesDropped);
                    continue;
                }
                if (ret == AVERROR(EAGAIN)) continue; // need more packets — normal
                // Other errors — drop frame
                ++m_framesDropped;
                continue;
            } else if (m_audioStreamIdx >= 0 && m_pkt->stream_index == m_audioStreamIdx) {
                AVPacket *copy = av_packet_alloc();
                av_packet_ref(copy, m_pkt);
                { std::lock_guard<std::mutex> lock(m_audioPacketMutex); m_audioPacketQueue.push(copy); }
                av_packet_unref(m_pkt);
            } else {
                av_packet_unref(m_pkt);
            }
        }
        // EOF reached — signal decoder to flush buffered frames
        fprintf(stderr, "[PIPE] EOF after %d packets, entering Flushing. decoded=%" PRId64 " skipped=%d dropped=%d\n",
                packetCount, m_totalFramesDecoded, m_packetsSkipped, m_framesDropped);
        m_decodeState = DecodeState::Flushing;
        avcodec_send_packet(m_codecCtx, nullptr);
    }

    // ---- Phase 2: Drain remaining frames from decoder buffer ----
    if (m_decodeState == DecodeState::Flushing) {
        int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
        if (ret == 0) {
            ++m_totalFramesDecoded;
            ++s_frameNum;
            if (processDecodedFrame(this, m_decodedFrame, m_width, m_height,
                                    m_hwDeviceCtx, m_swsCtx, m_convertCtx,
                                    m_convertedFrame, m_frame,
                                    m_skipRGBConversion))
            {
                updatePtsFromDecodedFrame();
                return true;
            }
            ++m_framesDropped;
            fprintf(stderr, "[PIPE] F%" PRId64 " flushing: processDecodedFrame failed\n", s_frameNum);
            return true;
        }
        fprintf(stderr, "[PIPE] Flushing done: ret=%d. transitioning to Finished.\n", ret);
        // No more frames — done
        m_decodeState = DecodeState::Finished;
    }

    fprintf(stderr, "[PIPE] readFrame returning false. decodeState=%d totalDecoded=%" PRId64 "\n",
            static_cast<int>(m_decodeState), m_totalFramesDecoded);
    return false;
}

// ---- Frame queue: decode one raw packet into m_decodedFrame ----
bool MediaSession::decodeOnePacket()
{
    if (!m_fmtCtx || !m_codecCtx) return false;

    while (av_read_frame(m_fmtCtx, m_pkt) >= 0) {
        if (m_pkt->stream_index == m_videoStreamIdx) {
            int sendRet = avcodec_send_packet(m_codecCtx, m_pkt);
            av_packet_unref(m_pkt);
            if (sendRet == AVERROR(EAGAIN)) {
                // Decoder full — drain one frame, then retry
                int drainRet = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
                if (drainRet == 0) {
                    return true; // got a frame from draining
                }
                continue;
            }
            if (sendRet < 0) {
                ++m_packetsSkipped;
                continue;
            }
            int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
            if (ret == 0) {
                ++m_totalFramesDecoded;
                return true;
            }
            if (ret == AVERROR(EAGAIN)) continue;
            ++m_framesDropped;
            continue;
        } else if (m_audioStreamIdx >= 0 && m_pkt->stream_index == m_audioStreamIdx) {
            AVPacket *copy = av_packet_alloc();
            av_packet_ref(copy, m_pkt);
            { std::lock_guard<std::mutex> lock(m_audioPacketMutex); m_audioPacketQueue.push(copy); }
            av_packet_unref(m_pkt);
        } else {
            av_packet_unref(m_pkt);
        }
    }
    // EOF
    if (m_decodeState == DecodeState::Reading) {
        m_decodeState = DecodeState::Flushing;
        avcodec_send_packet(m_codecCtx, nullptr);
    }
    if (m_decodeState == DecodeState::Flushing) {
        int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
        if (ret == 0) {
            ++m_totalFramesDecoded;
            return true;
        }
        m_decodeState = DecodeState::Finished;
    }
    return false;
}

// ---- Frame queue: decode one frame and push to queue ----
bool MediaSession::decodeNextFrame()
{
    if (static_cast<int>(m_frameQueue.size()) >= m_frameQueueMax)
        return false;
    if (!decodeOnePacket())
        return false;

    // Transfer HW frame to CPU if needed (CUDA → P010/AVFrame)
    if (m_hwDeviceCtx && m_decodedFrame->hw_frames_ctx) {
        int64_t savedPts = m_decodedFrame->pts;
        int64_t savedBestEffort = m_decodedFrame->best_effort_timestamp;

        AVFrame *swFrame = av_frame_alloc();
        if (av_hwframe_transfer_data(swFrame, m_decodedFrame, 0) >= 0) {
            swFrame->pts = savedPts;
            swFrame->best_effort_timestamp = savedBestEffort;
            av_frame_unref(m_decodedFrame);
            av_frame_move_ref(m_decodedFrame, swFrame);
        } else {
            av_frame_free(&swFrame);
            return false;  // transfer failed, skip frame
        }
    }

    // Compute PTS in seconds
    double ptsSec = 0.0;
    if (m_decodedFrame->pts != AV_NOPTS_VALUE) {
        AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
        ptsSec = static_cast<double>(m_decodedFrame->pts) * av_q2d(tb);
    } else if (m_decodedFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
        AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
        ptsSec = static_cast<double>(m_decodedFrame->best_effort_timestamp) * av_q2d(tb);
    }

    int64_t frameNum = -1;
    if (m_decodedFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
        AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
        frameNum = qRound(ptsSec * m_fps);
    }

    m_lastDecodedPtsSec = ptsSec;

    // Allocate a new frame and copy data
    QueuedFrame qf;
    qf.frame = av_frame_alloc();
    av_frame_ref(qf.frame, m_decodedFrame);
    qf.ptsSec = ptsSec;
    qf.frameNum = frameNum;

    m_frameQueue.push_back(qf);
    return true;
}

bool MediaSession::hasDisplayableFrame(double ptsSec) const
{
    if (m_frameQueue.empty()) return false;
    return m_frameQueue.front().ptsSec <= ptsSec;
}

bool MediaSession::popDisplayFrame(QueuedFrame &out, double ptsSec)
{
    // Drop frames that are too old (more than 2 frame durations behind)
    double frameDur = 1.0 / qMax(m_fps, 1);
    while (!m_frameQueue.empty() && m_frameQueue.front().ptsSec < ptsSec - frameDur * 2) {
        av_frame_free(&m_frameQueue.front().frame);
        m_frameQueue.pop_front();
    }

    if (m_frameQueue.empty()) return false;
    out = m_frameQueue.front();
    m_frameQueue.pop_front();
    return true;
}

bool MediaSession::peekDisplayFrame(QueuedFrame &out) const
{
    if (m_frameQueue.empty()) return false;
    out = m_frameQueue.front();
    return true;
}

double MediaSession::nextFramePtsSec() const
{
    if (m_frameQueue.empty()) return -1.0;
    return m_frameQueue.front().ptsSec;
}

void MediaSession::flushFrameQueue()
{
    while (!m_frameQueue.empty()) {
        av_frame_free(&m_frameQueue.front().frame);
        m_frameQueue.pop_front();
    }
}

void MediaSession::flushAudioQueue()
{
    std::lock_guard<std::mutex> lock(m_audioPacketMutex);
    while (!m_audioPacketQueue.empty()) {
        av_packet_free(&m_audioPacketQueue.front());
        m_audioPacketQueue.pop();
    }
}

bool MediaSession::popAudioPacket(AVPacket **pkt)
{
    std::lock_guard<std::mutex> lock(m_audioPacketMutex);
    if (m_audioPacketQueue.empty()) return false;
    *pkt = m_audioPacketQueue.front();
    m_audioPacketQueue.pop();
    return true;
}

size_t MediaSession::audioQueueSize()
{
    std::lock_guard<std::mutex> lock(m_audioPacketMutex);
    return m_audioPacketQueue.size();
}

void MediaSession::seekSec(double sec)
{
    if (!m_fmtCtx || m_videoStreamIdx < 0) return;
    ++m_seeksCount;
    m_seekTimer.start();
    int64_t ts = static_cast<int64_t>(sec * AV_TIME_BASE);
    if (ts < 0) ts = 0;
    av_seek_frame(m_fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecCtx);
    m_seekLatencyMs = m_seekTimer.nsecsElapsed() / 1000000.0;
    m_decodeState = DecodeState::Reading;
    flushAudioQueue();
    flushFrameQueue();
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
    info.containerFormat = m_containerFormat;
    info.containerLongName = m_containerLongName;
    info.codecName = m_codecName;
    info.codecLongName = m_codecLongName;
    info.profile = m_profileName;

    // Codec level
    if (m_codecCtx) {
        int l = m_codecCtx->level;
        if (l > 0) {
            if (l == AV_LEVEL_UNKNOWN)
                info.level = "Unknown";
            else if (l % 10 == 0)
                info.level = QString("%1.%2").arg(l / 10).arg(l % 10);
            else
                info.level = QString::number(l);
        }
    }

    info.width = m_width;
    info.height = m_height;
    info.fps = m_fps;
    info.durationSec = m_durationSec;
    info.pixelFormat = m_pixFmtName;

    // Bit depth, chroma subsampling, pixel type from AVPixFmtDescriptor
    if (m_codecCtx) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(m_codecCtx->pix_fmt);
        if (desc) {
            info.bitDepth = desc->comp[0].depth;
            info.bitDepth = qMax(info.bitDepth, 8);

            // Chroma subsampling
            if (desc->nb_components == 1)
                info.chromaSubsampling = "Mono";
            else if (desc->flags & AV_PIX_FMT_FLAG_RGB)
                info.chromaSubsampling = "RGB";
            else if (desc->log2_chroma_w == 0 && desc->log2_chroma_h == 0)
                info.chromaSubsampling = "4:4:4";
            else if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 0)
                info.chromaSubsampling = "4:2:2";
            else if (desc->log2_chroma_w == 1 && desc->log2_chroma_h == 1)
                info.chromaSubsampling = "4:2:0";
            else if (desc->log2_chroma_w == 0 && desc->log2_chroma_h == 1)
                info.chromaSubsampling = "4:4:0";
            else
                info.chromaSubsampling = "Unknown";

            if (desc->flags & AV_PIX_FMT_FLAG_ALPHA)
                info.chromaSubsampling += " + Alpha";
            if (desc->flags & AV_PIX_FMT_FLAG_PLANAR)
                info.chromaSubsampling += " Planar";
        }
    }

    info.colorSpace = m_colorSpace;
    info.hdrType = m_hdrType;
    info.bitrate = m_bitrate;

    // Decoder info — single source of truth from DecoderManager
    {
        static const char *backendNames[] = {
            "Software", "D3D11VA", "NVDEC (CUDA)", "QuickSync (QSV)", "AMF"
        };
        int bi = static_cast<int>(m_decoderInfo.backend);
        if (bi >= 0 && bi < 5)
            info.decoderPath = QString::fromLatin1(backendNames[bi]);
        else
            info.decoderPath = "Software";

        info.decoder = m_decoderInfo.decoderName;
        if (!m_decoderInfo.gpuName.isEmpty() && m_decoderInfo.hardwareAccelerated)
            info.decoder += QString(" (%1)").arg(m_decoderInfo.gpuName);
    }

    info.renderer = m_rendererName;
    info.audioFormat = m_audioCodecName;
    info.audioCodecLongName = m_audioCodecLongName;
    info.audioSampleRate = m_audioSampleRate;
    info.audioChannels = m_audioChannels;
    info.audioChannelLayout = m_audioChannelLayout;
    info.audioSampleFormat = m_audioSampleFormat;
    info.audioBitDepth = m_audioBitDepth;
    info.audioBitrate = m_audioBitrate;
    info.audioProfile = m_audioProfile;
    info.audioFrameSize = m_audioFrameSize;
    info.audioBlockAlign = m_audioBlockAlign;

    info.videoStreams = m_videoStreamCount;
    info.audioStreams = m_audioStreamCount;
    info.subtitleStreams = m_subtitleStreamCount;

    // Count other stream types
    info.otherStreams = 0;
    if (m_fmtCtx) {
        for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
            AVMediaType t = m_fmtCtx->streams[i]->codecpar->codec_type;
            if (t != AVMEDIA_TYPE_VIDEO && t != AVMEDIA_TYPE_AUDIO && t != AVMEDIA_TYPE_SUBTITLE)
                info.otherStreams++;
        }
    }

    // Creation time metadata
    if (m_fmtCtx && m_fmtCtx->metadata) {
        AVDictionaryEntry *e = av_dict_get(m_fmtCtx->metadata, "creation_time", nullptr, 0);
        if (e) info.creationTime = QString::fromUtf8(e->value);
    }

    // Playback diagnostics
    info.totalFramesDecoded = m_totalFramesDecoded;
    info.packetsSkipped = m_packetsSkipped;
    info.framesDropped = m_framesDropped;
    info.seeksCount = m_seeksCount;
    info.seekLatencyMs = m_seekLatencyMs;
    info.avgDecodeMs = m_totalFramesDecoded > 0 ? m_totalDecodeMs / m_totalFramesDecoded : 0.0;
    info.peakDecodeMs = m_peakDecodeMs;

    info.isImageSequence = m_isImageSequence;
    info.sequencePattern = m_fmtCtx->url ? QString(m_fmtCtx->url) : QString();
    info.sequenceFrameCount = m_totalFrames;

    return info;
}

HDRMetadata MediaSession::hdrMetadata() const
{
    HDRMetadata md;
    if (!m_codecCtx) return md;

    // Parse transfer characteristics from codec context
    switch (m_codecCtx->color_trc) {
    case AVCOL_TRC_SMPTE2084:
        md.transfer = TransferCharacteristics::PQ;
        md.format = HDRFormat::HDR10;
        break;
    case AVCOL_TRC_ARIB_STD_B67:
        md.transfer = TransferCharacteristics::HLG;
        md.format = HDRFormat::HLG;
        break;
    case AVCOL_TRC_BT709:
        md.transfer = TransferCharacteristics::BT709;
        md.format = HDRFormat::SDR;
        break;
    default:
        md.transfer = TransferCharacteristics::Unknown;
        md.format = HDRFormat::None;
        break;
    }

    // Color primaries
    switch (m_codecCtx->color_primaries) {
    case AVCOL_PRI_BT709:
        md.primaries = ColorPrimaries::BT709;
        break;
    case AVCOL_PRI_BT2020:
        md.primaries = ColorPrimaries::BT2020;
        break;
    case AVCOL_PRI_SMPTE432:
        md.primaries = ColorPrimaries::DCI_P3;
        break;
    default:
        md.primaries = ColorPrimaries::Unknown;
        break;
    }

    return md;
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

    m_rendererName = "OpenGL YUV";
    m_gpuInfo = detectGPU();
    m_decoderInfo = {};
    m_decoderInfo.backend = DecodeBackend::Software;
    m_decoderInfo.decoderName = "Software";
    m_decoderInfo.gpuName = m_gpuInfo.name;

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
    m_sequenceCache = new SequenceFrameCache(this);
    m_compareCtrl = new CompareController(this);
    m_compareCtrl->setSessions(&m_session, &m_sessionB);

    m_subtitleManager = std::make_unique<SubtitleManager>(this);
    connect(m_subtitleManager.get(), &SubtitleManager::tracksChanged,
            this, [this]() { buildSubtitleMenu(); });
    connect(m_subtitleManager.get(), &SubtitleManager::activeTrackChanged,
            this, [this](int) { m_videoWidget->update(); });
    connect(m_subtitleManager.get(), &SubtitleManager::subtitlesToggled,
            this, [this](bool) { m_videoWidget->update(); });

    initMediaFilters();

    setupUI();
    setupMenus();
    loadSettings();

    // Update system
    m_updateManager = new UpdateManager(this);
    QByteArray token = qgetenv("LUNARPLAYER_GITHUB_TOKEN");
    if (!token.isEmpty()) {
        m_updateManager->setToken(QString::fromUtf8(token));
        fprintf(stderr, "[UPDATE] Token loaded from environment\n");
    } else {
        fprintf(stderr, "[UPDATE] No token set (public repo)\n");
    }
    fprintf(stderr, "[UPDATE] UpdateManager initialized\n");
    fflush(stderr);

    updatePlaybackState();
    updateTitle();



    m_updateTimer = new QTimer(this);
    m_updateTimer->setTimerType(Qt::PreciseTimer);
    connect(m_updateTimer, &QTimer::timeout, this, &MainWindow::updateTimerTick);

    m_decodeTimer = new QTimer(this);
    m_decodeTimer->setTimerType(Qt::PreciseTimer);

    // Timeline reliability: coalesce drag events — seek latest value on a timer, not every mouse move
    m_dragCoalesceTimer = new QTimer(this);
    m_dragCoalesceTimer->setTimerType(Qt::PreciseTimer);
    m_dragCoalesceTimer->setSingleShot(true);
    connect(m_dragCoalesceTimer, &QTimer::timeout, this, [this]() {
        if (!m_isScrubbing || !m_session.isOpen()) return;
        double posSec = (static_cast<double>(m_latestSliderValue) / 1000.0) * m_session.durationSec();
        m_currentPosSec = posSec;
        fprintf(stderr, "[DRAG-COALESCED] seekSeq=%d posSec=%.3f\n", m_seekSeq.load(), posSec);
        fflush(stderr);
        m_session.seekSec(posSec);
        if (m_session.readFrame()) {
            applyCurrentFrame();
            m_currentFrameNum = m_session.currentFrameNumber();
        }
    });

    // Startup background update check (5s delay)
    if (m_updateManager->autoCheckEnabled()) {
        m_startupUpdateTimer = new QTimer(this);
        m_startupUpdateTimer->setSingleShot(true);
        connect(m_startupUpdateTimer, &QTimer::timeout, this, &MainWindow::onStartupUpdateCheck);
        m_startupUpdateTimer->start(5000);
        fprintf(stderr, "[UPDATE] Startup check scheduled in 5s\n");
        fflush(stderr);
    }

    connect(m_decodeTimer, &QTimer::timeout, this, [this]() {
        if (!m_playing) return;

        static int64_t s_tickNum = 0;
        ++s_tickNum;
        QElapsedTimer tickTimer; tickTimer.start();
        QElapsedTimer stageTimer; stageTimer.start();
        m_pipelineTimer.start();

        // Network stream path
        if (m_isNetworkStream && m_networkSession) {
            if (m_playDirection > 0) {
                if (!m_networkSession->readFrame()) {
                    if (m_networkSession->isLiveStream()) return;
                    fprintf(stderr, "[TICK %" PRId64 "] network readFrame=false, stopping\n", s_tickNum);
                    m_playing = false; updatePlaybackState(); stopPlayback(); return;
                }
            } else { return; }
            m_currentPosSec = m_networkSession->currentPtsSec();
            m_currentFrameNum = static_cast<int>(m_currentPosSec * m_networkSession->fps());
            applyCurrentFrame();
            return;
        }

        // Compare mode
        if (m_playMode == PlayMode::Compare) {
            if (!m_session.isOpen() && !m_sessionB.isOpen()) return;
            if (m_playDirection > 0) {
                bool aOk = m_session.isOpen() && m_session.readFrame();
                bool bOk = m_sessionB.isOpen() && m_sessionB.readFrame();
                if (!aOk && !bOk) { fprintf(stderr, "[TICK %" PRId64 "] compare: both readFrame=false, stopping\n", s_tickNum); m_playing = false; updatePlaybackState(); stopPlayback(); return; }
            } else if (m_playDirection < 0) {
                bool aOk = m_session.isOpen() && m_session.stepBackward();
                bool bOk = m_sessionB.isOpen() && m_sessionB.stepBackward();
                if (!aOk && !bOk) { m_playing = false; updatePlaybackState(); stopPlayback(); return; }
            } else { return; }
            compareApplyBothFrames();
            m_currentFrameNum = m_session.isOpen() ? m_session.currentFrameNumber() : m_sessionB.currentFrameNumber();
        } else {
            if (!m_session.isOpen()) return;
            m_session.setSkipRGBConversion(m_videoWidget && m_videoWidget->rendererSupportsAVFrame());

            if (m_playDirection > 0) {
                // ---- PTS-driven presentation scheduling ----
                double now = playbackNowSec();
                double frameDur = 1.0 / qMax(m_session.fps(), 1);
                double maxLateness = frameDur * 2.0;

                QElapsedTimer uploadTimer; uploadTimer.start();
                bool presented = false;
                static int64_t s_droppedFrames = 0;
                static double s_lastPresentedPts = -1.0;

                if (m_usingDecodeThread && m_decodeThread) {
                    DecodeFrame front;
                    while (m_decodeThread->peekFrame(front)) {
                        double lateness = now - front.ptsSec;

                        if (lateness < -frameDur) {
                            break;
                        } else if (lateness <= maxLateness) {
                            DecodeFrame df;
                            if (m_decodeThread->popFrame(df)) {
                                av_frame_unref(m_session.decodedFrame());
                                av_frame_ref(m_session.decodedFrame(), df.frame);
                                applyCurrentFrame();
                                m_currentFrameNum = static_cast<int>(df.frameNum);
                                m_currentPosSec = df.ptsSec;
                                presented = true;
                                adjustPlaybackClock(df.ptsSec);

                                if (s_lastPresentedPts >= 0) {
                                    double ptsGap = df.ptsSec - s_lastPresentedPts;
                                    if (ptsGap > frameDur * 3.0) {
                                        fprintf(stderr, "[SCHED] PTS GAP: F%d->F%d gap=%.0fms (decoder drop)\n",
                                                static_cast<int>(qRound(s_lastPresentedPts * m_session.fps())),
                                                m_currentFrameNum, ptsGap * 1000.0);
                                    }
                                }
                                s_lastPresentedPts = df.ptsSec;
                                av_frame_free(&df.frame);
                            }
                            break;
                        } else {
                            m_decodeThread->dropFrontFrame();
                            ++s_droppedFrames;
                            fprintf(stderr, "[SCHED] DROP F%d PTS=%.3f late=%.0fms now=%.3f\n",
                                    static_cast<int>(front.frameNum), front.ptsSec,
                                    lateness * 1000.0, now);
                        }
                    }

                    if (!presented && !m_decodeThread->peekFrame(front) && m_decodeThread->isFinished()) {
                        fprintf(stderr, "[TICK %" PRId64 "] EOF — decode thread finished, stopping\n", s_tickNum);
                        m_playing = false; updatePlaybackState(); stopPlayback(); return;
                    }
                } else {
                    // ---- Synchronous decode path ----
                    m_session.setSkipRGBConversion(m_videoWidget && m_videoWidget->rendererSupportsAVFrame());
                    QElapsedTimer decodeTimer; decodeTimer.start();
                    int decodedThisTick = 0;
                    while (m_session.frameQueueSize() < 4) {
                        if (!m_session.decodeNextFrame()) break;
                        ++decodedThisTick;
                        if (decodeTimer.nsecsElapsed() / 1000000.0 > 30) break;
                    }
                    QueuedFrame qf;
                    if (m_session.popDisplayFrame(qf, now)) {
                        av_frame_unref(m_session.decodedFrame());
                        av_frame_ref(m_session.decodedFrame(), qf.frame);
                        applyCurrentFrame();
                        m_currentFrameNum = static_cast<int>(qf.frameNum);
                        m_currentPosSec = qf.ptsSec;
                        presented = true;
                        av_frame_free(&qf.frame);
                        adjustPlaybackClock(qf.ptsSec);
                    } else {
                        double nextPts = m_session.nextFramePtsSec();
                        if (nextPts < 0 && m_session.decodeState() == MediaSession::DecodeState::Finished) {
                            fprintf(stderr, "[TICK %" PRId64 "] EOF — queue empty, decoder finished, stopping\n", s_tickNum);
                            m_playing = false; updatePlaybackState(); stopPlayback(); return;
                        }
                    }
                }

                double uploadMs = uploadTimer.nsecsElapsed() / 1000000.0;
                double tickTotalMs = tickTimer.nsecsElapsed() / 1000000.0;
                int queueSz = m_usingDecodeThread ? m_decodeThread->queueSize() : m_session.frameQueueSize();

                if (s_tickNum % 60 == 0 || !presented) {
                    double frontPts = -1.0;
                    if (m_usingDecodeThread && m_decodeThread) {
                        DecodeFrame tmp;
                        if (m_decodeThread->peekFrame(tmp)) frontPts = tmp.ptsSec;
                    } else if (m_session.frameQueueSize() > 0) {
                        QueuedFrame qf;
                        if (m_session.peekDisplayFrame(qf)) frontPts = qf.ptsSec;
                    }
                    fprintf(stderr, "[TICK %" PRId64 "] F%d now=%.3f frontPts=%.3f queue=%d upload=%.1f tick=%.1f drops=%" PRId64 "%s\n",
                            s_tickNum, m_currentFrameNum, now, frontPts,
                            queueSz, uploadMs, tickTotalMs, s_droppedFrames,
                            presented ? "" : " (waiting)");
                }
            } else if (m_playDirection < 0) {
                if (!m_session.stepBackward()) { m_playing = false; updatePlaybackState(); stopPlayback(); return; }
                applyCurrentFrame();
                m_currentFrameNum = m_session.currentFrameNumber();
            } else { return; }
        }
        if (m_audioEngine && m_playDirection < 0) m_audioEngine->reset();
        // Loop detection (normal mode only)
        if (m_playMode == PlayMode::Normal && m_loopOut >= 0 && m_currentFrameNum >= m_loopOut && m_loopIn >= 0 && m_playDirection > 0) {
            fprintf(stderr, "[TICK %" PRId64 "] loop: seeking from %d to %d\n", s_tickNum, m_currentFrameNum, m_loopIn);
            m_session.seekToFrame(m_loopIn);
            m_currentFrameNum = m_loopIn;
            applyCurrentFrame();
        }

        // Pipeline profiling
        double tickMs = m_pipelineTimer.nsecsElapsed() / 1000000.0;
        double decodeMs = m_session.lastDecodeMs();
        double swsMs = m_session.lastSwsScaleMs();
        double uploadMs = m_videoWidget && m_videoWidget->renderer()
            ? m_videoWidget->renderer()->lastUploadMs() : 0.0;
        m_profTotalMs += tickMs;
        m_profReadMs += decodeMs;
        m_profConvertMs += swsMs;
        m_profUploadMs += uploadMs;
        if (tickMs > m_profPeakTotalMs) m_profPeakTotalMs = tickMs;
        if (decodeMs > m_profPeakDecodeMs) m_profPeakDecodeMs = decodeMs;
        if (swsMs > m_profPeakConvertMs) m_profPeakConvertMs = swsMs;
        if (uploadMs > m_profPeakUploadMs) m_profPeakUploadMs = uploadMs;
        m_profCount++;
        if (m_profCount >= 60) {
            double avgTotal = m_profTotalMs / m_profCount;
            double avgDecode = m_profReadMs / m_profCount;
            double avgSws = m_profConvertMs / m_profCount;
            double avgUpload = m_profUploadMs / m_profCount;
            fprintf(stderr, "[PROFILE] avg| peak=\n");
            fprintf(stderr, "[PROFILE] total: avg=%.1fms peak=%.1fms\n", avgTotal, m_profPeakTotalMs);
            fprintf(stderr, "[PROFILE] decode: avg=%.1fms peak=%.1fms\n", avgDecode, m_profPeakDecodeMs);
            fprintf(stderr, "[PROFILE] sws_scale: avg=%.1fms peak=%.1fms\n", avgSws, m_profPeakConvertMs);
            fprintf(stderr, "[PROFILE] upload: avg=%.1fms peak=%.1fms\n", avgUpload, m_profPeakUploadMs);
            fprintf(stderr, "[PROFILE] overhead: avg=%.1fms (audio+dispatch+other)\n",
                    avgTotal - avgDecode - avgSws - avgUpload);
            fprintf(stderr, "[PROFILE] ---\n");
            m_profTotalMs = 0; m_profReadMs = 0; m_profConvertMs = 0; m_profUploadMs = 0;
            m_profPeakTotalMs = 0; m_profPeakDecodeMs = 0; m_profPeakConvertMs = 0; m_profPeakUploadMs = 0;
            m_profCount = 0;
        }
    });

    m_fullscreenHideTimer = new QTimer(this);
    m_fullscreenHideTimer->setSingleShot(true);
    m_fullscreenHideTimer->setInterval(3000);
    connect(m_fullscreenHideTimer, &QTimer::timeout, this, [this]() {
        if (!m_isFullscreen || !m_controlsVisible) return;
        m_controlsVisible = false;
        m_fsOverlay->hide();
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

    menuBar()->installEventFilter(this);

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

    // Streaming overlay (hidden by default, shown when network stream loads)
    m_streamingOverlay = new StreamingOverlay(m_videoWidget);
    m_streamingOverlay->setGeometry(8, 8, 280, 140);
    m_streamingOverlay->hide();

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
        if (m_audioEngine) m_audioEngine->setVolume(val / 100.0f);
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
    m_fsOverlay = new GradientOverlay(this);
    m_fsOverlay->setObjectName("fsOverlay");
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
        m_fsOverlay->show();
        m_fsOverlay->raise();
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
        m_fsOverlay->hide();
        setCursor(Qt::BlankCursor);
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
    if (!m_skipReposition)
        positionControls();
}

// ============================================================
// Modal dialog Z-order helpers
// ============================================================
// When the player is in borderless fullscreen it uses HWND_TOPMOST.
// Without this, native dialogs (QFileDialog, QMessageBox, etc.)
// open *behind* the player because they cannot break the TOPMOST
// Z-order.  These helpers temporarily drop TOPMOST around any
// modal dialog call and restore it afterward.

void MainWindow::beforeModalDialog()
{
#ifdef Q_OS_WIN
    if (m_isFullscreen && m_dialogDepth++ == 0) {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    }
#endif
}

void MainWindow::afterModalDialog()
{
#ifdef Q_OS_WIN
    if (m_isFullscreen && --m_dialogDepth == 0) {
        // Only restore TOPMOST if no other modal dialog is still active
        // (handles the case of a nested dialog, e.g. error message
        //  shown after a file dialog).
        if (QApplication::activeModalWidget() != nullptr)
            return;

        HWND hwnd = reinterpret_cast<HWND>(winId());
        QScreen *scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr) scr = QGuiApplication::primaryScreen();
        QRect geo = scr->geometry();
        ::SetWindowPos(hwnd, HWND_TOPMOST,
                       geo.x(), geo.y(), geo.width(), geo.height(),
                       SWP_FRAMECHANGED);
    }
#endif
}

// ============================================================
// Fullscreen
// ============================================================

void MainWindow::toggleFullscreen()
{
    if (m_isFullscreen) {
        // Close any open fullscreen panel
        if (m_fullscreenPanel) {
            m_fullscreenPanel->closePanel();
            m_fullscreenPanel = nullptr;
        }
        // Exit borderless → restore original window style
        m_fullscreenHideTimer->stop();
        m_fsOverlay->hide();

#ifdef Q_OS_WIN
        HWND hwnd = reinterpret_cast<HWND>(winId());
        SetWindowLongPtr(hwnd, GWL_STYLE, m_savedWindowStyle);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, m_savedWindowExStyle);
        // Remove TOPMOST and apply restored style
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
#endif
        if (!m_savedWindowGeometry.isEmpty())
            restoreGeometry(m_savedWindowGeometry);
        showNormal();

        m_isFullscreen = false;
        m_controlsVisible = false;
        menuBar()->show();
        unsetCursor();
        m_videoWidget->setBottomPadding(56);
        if (m_hoverPreview) m_hoverPreview->hidePreview();
        updateOverlayMode();
        fprintf(stderr, "[FULLSCREEN] EXIT\n");
        fflush(stderr);
    } else {
        // Enter borderless fullscreen → modify window style in-place (no HWND recreation)
        if (m_hoverPreview) m_hoverPreview->hidePreview();
        m_savedWindowGeometry = saveGeometry();

#ifdef Q_OS_WIN
        HWND hwnd = reinterpret_cast<HWND>(winId());
        m_savedWindowStyle = static_cast<long>(GetWindowLongPtr(hwnd, GWL_STYLE));
        m_savedWindowExStyle = static_cast<long>(GetWindowLongPtr(hwnd, GWL_EXSTYLE));

        // Remove frame, caption, thick frame → borderless
        long style = m_savedWindowStyle;
        style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_DLGFRAME);
        style |= WS_POPUP | WS_VISIBLE;

        // Remove border-related extended styles
        long exStyle = m_savedWindowExStyle;
        exStyle &= ~(WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE);

        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle);

        // Position to cover full screen (including taskbar), always on top
        QScreen *scr = QGuiApplication::screenAt(QCursor::pos());
        if (!scr) scr = QGuiApplication::primaryScreen();
        QRect geo = scr ? scr->geometry() : geometry();
        SetWindowPos(hwnd, HWND_TOPMOST, geo.x(), geo.y(), geo.width(), geo.height(),
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
#endif

        m_isFullscreen = true;
        m_controlsVisible = true;
        menuBar()->hide();
        m_videoWidget->setBottomPadding(0);
        updateOverlayMode();
        m_fsOverlay->show();
        m_fsOverlay->raise();
        setCursor(Qt::ArrowCursor);
        m_fullscreenHideTimer->start();
        fprintf(stderr, "[FULLSCREEN] ENTER fsSeekBar=%p\n",
                static_cast<void*>(m_fsSeekBar));
        fflush(stderr);
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
    fileMenu->addAction(LunarIcons::image(), "Open &Image...", this, &MainWindow::openImage);
    fileMenu->addAction(LunarIcons::open(), "Open Image &Sequence...", this, &MainWindow::openImageSequence);
    fileMenu->addSeparator();
    fileMenu->addAction("&Close", this, &MainWindow::closeFile, QKeySequence("Ctrl+W"));
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close, QKeySequence::Quit);

    auto *compareMenu = menuBar()->addMenu("&Compare");
    compareMenu->addAction("Side-by-Side &Compare...", this, &MainWindow::openCompare, QKeySequence("Ctrl+Shift+O"));
    compareMenu->addSeparator();
    compareMenu->addAction("E&xit Compare Mode", this, &MainWindow::exitCompareMode, QKeySequence("Ctrl+Shift+W"));

    m_audioMenu = menuBar()->addMenu("&Audio");
    buildAudioMenu();

    m_videoMenu = menuBar()->addMenu("&Video");
    buildVideoMenu();

    m_subtitleMenu = menuBar()->addMenu("Su&btitle");
    buildSubtitleMenu();

    auto *toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("Media &Information...", this, [this]() {
        beforeModalDialog();
        MediaInfo info = m_session.mediaInfo();
        if (m_audioEngine && m_audioEngine->isOpen()) {
            AudioPipelineStats astats = m_audioEngine->stats();
            info.audioBackend = QString::fromStdString(astats.backendName);
            info.audioOutputSampleRate = m_audioEngine->sampleRate();
            info.audioOutputChannels = m_audioEngine->channels();
            info.audioPacketsDecoded = astats.packetsDecoded;
            info.audioFramesResampled = astats.framesResampled;
            info.audioFramesWritten = astats.framesWritten;
            info.audioUnderruns = astats.underruns;
            info.audioOverruns = astats.overruns;
            info.audioDecodeMs = astats.decodeMs;
            info.audioResampleMs = astats.resampleMs;
            info.audioOutputLatencyMs = astats.latencyMs;

            QString opName;
            switch (m_audioEngine->channelOperation()) {
            case ChannelOperation::Downmix: opName = "Downmix"; break;
            case ChannelOperation::Upmix:   opName = "Upmix"; break;
            case ChannelOperation::Remap:   opName = "Remap"; break;
            default: opName = "Native"; break;
            }
            info.channelOperation = opName;

            AudioDeviceManager devMgr;
            auto defaultDev = devMgr.defaultDevice(false);
            if (!defaultDev.id.empty()) {
                info.deviceName = QString::fromStdWString(defaultDev.friendlyName);
                info.deviceManufacturer = QString::fromStdWString(defaultDev.manufacturer);
                info.deviceMaxChannels = defaultDev.maxChannels;
                info.deviceMinSampleRate = defaultDev.minSampleRate;
                info.deviceMaxSampleRate = defaultDev.maxSampleRate;
                info.deviceSupportsExclusive = defaultDev.supportsExclusive;
                info.deviceSupportsPassthroughAC3 = defaultDev.supportsPassthroughAC3;
                info.deviceSupportsPassthroughEAC3 = defaultDev.supportsPassthroughEAC3;
                info.deviceSupportsPassthroughDTS = defaultDev.supportsPassthroughDTS;
            }
        }
        MediaInfoDialog dlg(info, this);
        dlg.exec();
        afterModalDialog();
    });
    toolsMenu->addAction("Performance Information", this, [this]() {
        m_showPerformance = !m_showPerformance;
        m_videoWidget->setShowPerformance(m_showPerformance);
        if (m_showPerformance) updatePerformanceOverlay();
    });
    toolsMenu->addAction("Renderer Information");
    toolsMenu->addAction("Debug Console");
    toolsMenu->addSeparator();
    toolsMenu->addAction("Update Settings...", this, &MainWindow::showUpdateSettings);
    m_toggleStreamingOverlayAction = toolsMenu->addAction("Streaming Overlay");
    m_toggleStreamingOverlayAction->setCheckable(true);
    m_toggleStreamingOverlayAction->setChecked(false);
    connect(m_toggleStreamingOverlayAction, &QAction::toggled, this, [this](bool checked) {
        if (m_streamingOverlay) m_streamingOverlay->setVisible(checked);
    });

    auto *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("Check for &Updates...", this, &MainWindow::checkForUpdates);
    helpMenu->addAction("&Release Notes", this, &MainWindow::showReleaseNotes);
    helpMenu->addSeparator();
    helpMenu->addAction("&About Lunar Player", this, [this]() {
        beforeModalDialog();
        QMessageBox::about(this, "About Lunar Player",
            "Lunar Player " + QCoreApplication::applicationVersion() +
            "\n\nA modern media player for animation, VFX, and editorial review.");
        afterModalDialog();
    });
    helpMenu->addAction("&Keyboard Shortcuts", this, [this]() {
        beforeModalDialog();
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
            "Ctrl+W      Close File\n"
            "\n"
            "--- Compare Mode ---\n"
            "Ctrl+Shift+O  Side-by-Side Compare\n"
            "Ctrl+Shift+W  Exit Compare Mode\n"
            "A            Audio: Side A\n"
            "B            Audio: Side B\n"
            "Shift+Tab    Toggle Audio A/B\n");
        afterModalDialog();
    });
}

void MainWindow::buildAudioMenu()
{
    m_audioMenu->clear();

    // Audio Track — uses shared builder
    m_audioTrackGroup = new QActionGroup(this);
    m_audioTrackGroup->setExclusive(true);
    buildAudioTrackSubmenu(m_audioMenu, m_audioTrackGroup);
    connect(m_audioTrackGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        executeAudioCommand({AudioCmd::AudioTrack, a->data()});
    });

    m_audioMenu->addSeparator();

    // Stereo Mode — uses shared builder
    m_stereoModeGroup = new QActionGroup(this);
    m_stereoModeGroup->setExclusive(true);
    buildStereoModeSubmenu(m_audioMenu, m_stereoModeGroup);
    connect(m_stereoModeGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        executeAudioCommand({AudioCmd::StereoMode, a->data()});
    });

    m_audioMenu->addSeparator();

    // Mute — forwards to centralized command
    m_muteAction = m_audioMenu->addAction(LunarIcons::volumeMute(), "&Mute");
    m_muteAction->setCheckable(true);
    m_muteAction->setShortcut(QKeySequence("M"));
    connect(m_muteAction, &QAction::toggled, this, [this](bool muted) {
        executeAudioCommand({AudioCmd::Mute, muted});
    });

    m_audioMenu->addSeparator();

    // Volume — forwards to centralized command
    m_audioMenu->addAction(LunarIcons::volumeHigh(), "Volume &Up", this, [this]() {
        executeAudioCommand({AudioCmd::VolumeChange, 10});
    }, QKeySequence("Up"));
    m_audioMenu->addAction(LunarIcons::volumeLow(), "Volume &Down", this, [this]() {
        executeAudioCommand({AudioCmd::VolumeChange, -10});
    }, QKeySequence("Down"));
}

void MainWindow::buildVideoMenu()
{
    m_videoMenu->clear();

    // Video Track
    auto *trackGroup = new QActionGroup(this); trackGroup->setExclusive(true);
    auto *trackMenu = m_videoMenu->addMenu("Video &Track");
    if (m_session.videoStreamCount() > 0) {
        for (int i = 0; i < m_session.videoStreamCount(); ++i) {
            auto *a = trackMenu->addAction(QString("Track %1").arg(i + 1));
            a->setCheckable(true); a->setChecked(i == 0); a->setData(i);
            trackGroup->addAction(a);
        }
    } else { trackMenu->addAction("No Video Tracks")->setEnabled(false); }

    m_videoMenu->addSeparator();

    // Fullscreen
    m_videoMenu->addAction(LunarIcons::fullscreen(), "&Fullscreen", this, &MainWindow::toggleFullscreen, QKeySequence("F11"));

    m_videoMenu->addSeparator();

    // Zoom — uses shared builder, independent QActionGroup
    m_zoomGroup = new QActionGroup(this); m_zoomGroup->setExclusive(true);
    buildZoomSubmenu(m_videoMenu, m_zoomGroup);
    connect(m_zoomGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        executeVideoCommand({VideoCmd::Zoom, a->data()});
    });

    // Aspect Ratio — uses shared builder, independent QActionGroup
    m_aspectGroup = new QActionGroup(this); m_aspectGroup->setExclusive(true);
    buildAspectSubmenu(m_videoMenu, m_aspectGroup);
    connect(m_aspectGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        executeVideoCommand({VideoCmd::AspectRatio, a->data()});
    });

    m_videoMenu->addSeparator();

    // Crop — uses shared builder, independent QActionGroup
    m_cropGroup = new QActionGroup(this); m_cropGroup->setExclusive(true);
    buildCropSubmenu(m_videoMenu, m_cropGroup);
    connect(m_cropGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        executeVideoCommand({VideoCmd::Crop, a->data()});
    });

    m_videoMenu->addSeparator();

    // Decoder/GPU info (menu bar only, decorative)
    DecoderInfo di = m_session.decoderInfo();
    GPUInfo gpu = m_session.gpuInfo();
    m_decoderInfoAction = m_videoMenu->addAction("Decoder: " + di.decoderName);
    m_decoderInfoAction->setEnabled(false);
    m_rendererInfoAction = m_videoMenu->addAction("Renderer: OpenGL YUV");
    m_rendererInfoAction->setEnabled(false);
    if (!gpu.name.isEmpty()) {
        QAction *gpuAction = m_videoMenu->addAction("GPU: " + gpu.name);
        gpuAction->setEnabled(false);
    }
    if (di.hardwareAccelerated) {
        m_videoMenu->addAction("HW Accel: Yes")->setEnabled(false);
    }
}

// ---- Zoom / Aspect / Crop Handlers ----
void MainWindow::onZoomChanged(QAction *action)
{
    double factor = action->data().toDouble();
    m_videoWidget->setZoomFactor(factor);
    saveSettings();
}

void MainWindow::onAspectChanged(QAction *action)
{
    auto mode = static_cast<VideoWidget::AspectMode>(action->data().toInt());
    m_videoWidget->setAspectMode(mode);
    saveSettings();
}

void MainWindow::onCropChanged(QAction *action)
{
    auto mode = static_cast<VideoWidget::CropMode>(action->data().toInt());
    m_videoWidget->setCropMode(mode);
    saveSettings();
}

// ============================================================
// Helper Builders — shared by Menu Bar and Context Menu
// Each creates fresh QActions in the given menu (no sharing)
// ============================================================

void MainWindow::addZoomAction(QMenu *menu, const char *label, double factor, bool checked, QActionGroup *group)
{
    auto *a = menu->addAction(QLatin1String(label));
    a->setCheckable(true);
    a->setData(factor);
    a->setChecked(checked);
    group->addAction(a);
}

void MainWindow::addAspectAction(QMenu *menu, const char *label, VideoWidget::AspectMode mode, bool checked, QActionGroup *group)
{
    auto *a = menu->addAction(QLatin1String(label));
    a->setCheckable(true);
    a->setData(static_cast<int>(mode));
    a->setChecked(checked);
    group->addAction(a);
}

void MainWindow::addCropAction(QMenu *menu, const char *label, VideoWidget::CropMode mode, bool checked, QActionGroup *group)
{
    auto *a = menu->addAction(QLatin1String(label));
    a->setCheckable(true);
    a->setData(static_cast<int>(mode));
    a->setChecked(checked);
    group->addAction(a);
}

void MainWindow::buildZoomSubmenu(QMenu *parent, QActionGroup *group)
{
    auto *zoomMenu = parent->addMenu("&Zoom");
    double cur = m_videoWidget->zoomFactor();
    struct { const char *label; double factor; } zooms[] = {
        {"50%", 0.5}, {"100%", 1.0}, {"150%", 1.5}, {"200%", 2.0}
    };
    for (auto &z : zooms)
        addZoomAction(zoomMenu, z.label, z.factor, qFuzzyCompare(cur, z.factor), group);
}

void MainWindow::buildAspectSubmenu(QMenu *parent, QActionGroup *group)
{
    auto *aspectMenu = parent->addMenu("&Aspect Ratio");
    VideoWidget::AspectMode cur = m_videoWidget->aspectMode();
    struct { const char *label; VideoWidget::AspectMode mode; } aspects[] = {
        {"Auto",     VideoWidget::Auto},
        {"16:9",     VideoWidget::Ratio16_9},
        {"21:9",     VideoWidget::Ratio21_9},
        {"4:3",      VideoWidget::Ratio4_3},
        {"Original", VideoWidget::Original}
    };
    for (auto &a : aspects)
        addAspectAction(aspectMenu, a.label, a.mode, cur == a.mode, group);
}

void MainWindow::buildCropSubmenu(QMenu *parent, QActionGroup *group)
{
    auto *cropMenu = parent->addMenu("&Crop");
    VideoWidget::CropMode cur = m_videoWidget->cropMode();
    struct { const char *label; VideoWidget::CropMode mode; } crops[] = {
        {"None",       VideoWidget::None},
        {"Fill",       VideoWidget::Fill},
        {"Smart Crop", VideoWidget::Smart}
    };
    for (auto &c : crops)
        addCropAction(cropMenu, c.label, c.mode, cur == c.mode, group);
}

void MainWindow::buildAudioTrackSubmenu(QMenu *parent, QActionGroup *group)
{
    auto *trackMenu = parent->addMenu("Audio &Track");
    if (m_session.audioStreamCount() > 0) {
        for (int i = 0; i < m_session.audioStreamCount(); ++i) {
            auto *a = trackMenu->addAction(QString("Track %1").arg(i + 1));
            a->setCheckable(true);
            a->setChecked(i == 0);
            a->setData(i);
            group->addAction(a);
        }
    } else {
        trackMenu->addAction("No Audio Tracks")->setEnabled(false);
    }
}

void MainWindow::buildStereoModeSubmenu(QMenu *parent, QActionGroup *group)
{
    auto *stereoMenu = parent->addMenu("Stereo &Mode");
    for (const char *m : {"Stereo", "Left", "Right", "Mono"}) {
        auto *a = stereoMenu->addAction(QString(m));
        a->setCheckable(true);
        a->setChecked(QString(m) == "Stereo");
        a->setData(QString(m));
        group->addAction(a);
    }
}

void MainWindow::buildSubtitleTrackSubmenu(QMenu *parent, QActionGroup *group)
{
    auto *trackMenu = parent->addMenu("Subtitle &Track");
    int n = m_subtitleManager->trackCount();
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            auto *a = trackMenu->addAction(m_subtitleManager->trackDisplayNames().value(i));
            a->setCheckable(true);
            a->setChecked(i == m_subtitleManager->activeTrackIndex());
            a->setData(i);
            group->addAction(a);
        }
    } else {
        trackMenu->addAction("No Subtitle Tracks")->setEnabled(false);
    }
}

// ============================================================
// Action Controller — shared business logic entry points
// Both Menu Bar and Context Menu forward here
// ============================================================

void MainWindow::executeVideoCommand(const VideoCommand &cmd)
{
    switch (cmd.type) {
    case VideoCmd::Zoom: {
        double factor = cmd.value.toDouble();
        printf("[CMD] Video  Zoom -> %.0f%%\n", factor * 100);
        m_videoWidget->setZoomFactor(factor);
        syncZoomCheckmarks();
        break;
    }
    case VideoCmd::AspectRatio: {
        auto mode = static_cast<VideoWidget::AspectMode>(cmd.value.toInt());
        printf("[CMD] Video  Aspect -> %d\n", (int)mode);
        m_videoWidget->setAspectMode(mode);
        syncAspectCheckmarks();
        break;
    }
    case VideoCmd::Crop: {
        auto mode = static_cast<VideoWidget::CropMode>(cmd.value.toInt());
        printf("[CMD] Video  Crop -> %d\n", (int)mode);
        m_videoWidget->setCropMode(mode);
        syncCropCheckmarks();
        break;
    }
    case VideoCmd::Fullscreen:
        printf("[CMD] Video  Fullscreen -> toggle\n");
        toggleFullscreen();
        break;
    case VideoCmd::VideoTrack:
        printf("[CMD] Video  Track -> %d\n", cmd.value.toInt());
        break;
    }
    saveSettings();
}

void MainWindow::executeAudioCommand(const AudioCommand &cmd)
{
    switch (cmd.type) {
    case AudioCmd::Mute: {
        bool muted = cmd.value.toBool();
        printf("[CMD] Audio  Mute -> %s\n", muted ? "ON" : "OFF");
        if (m_audioEngine)
            m_audioEngine->setVolume(muted ? 0.0f : m_volumeSlider->value() / 100.0f);
        m_volumeBtn->setIcon(muted ? LunarIcons::volumeMute() : LunarIcons::volumeHigh());
        syncMuteState();
        break;
    }
    case AudioCmd::VolumeChange: {
        int delta = cmd.value.toInt();
        int newVal = qBound(0, m_volumeSlider->value() + delta, 100);
        printf("[CMD] Audio  Volume -> %d\n", newVal);
        m_volumeSlider->setValue(newVal);
        break;
    }
    case AudioCmd::AudioTrack: {
        int index = cmd.value.toInt();
        printf("[CMD] Audio  Track -> %d\n", index);
        syncAudioTrackCheckmarks();
        break;
    }
    case AudioCmd::StereoMode: {
        QString mode = cmd.value.toString();
        printf("[CMD] Audio  Stereo -> %s\n", qPrintable(mode));
        break;
    }
    }
}

void MainWindow::executeSubtitleCommand(const SubtitleCommand &cmd)
{
    switch (cmd.type) {
    case SubtitleCmd::Enable: {
        bool enabled = cmd.value.toBool();
        printf("[CMD] Subtitle  Enable -> %s\n", enabled ? "ON" : "OFF");
        m_subtitleManager->setSubtitlesEnabled(enabled);
        syncSubtitlesEnabled();
        break;
    }
    case SubtitleCmd::LoadFile:
        printf("[CMD] Subtitle  Load File\n");
        loadSubtitleFile();
        break;
    case SubtitleCmd::Track: {
        int index = cmd.value.toInt();
        printf("[CMD] Subtitle  Track -> %d\n", index);
        m_subtitleManager->setActiveTrack(index);
        break;
    }
    case SubtitleCmd::Delay:
        printf("[CMD] Subtitle  Delay (not yet implemented)\n");
        break;
    }
}

// ============================================================
// Targeted UI sync — only updates what changed
// ============================================================

void MainWindow::syncZoomCheckmarks()
{
    double cur = m_videoWidget->zoomFactor();
    if (m_zoomGroup) {
        for (QAction *a : m_zoomGroup->actions())
            a->setChecked(qFuzzyCompare(a->data().toDouble(), cur));
    }
}

void MainWindow::syncAspectCheckmarks()
{
    VideoWidget::AspectMode cur = m_videoWidget->aspectMode();
    if (m_aspectGroup) {
        for (QAction *a : m_aspectGroup->actions())
            a->setChecked(a->data().toInt() == static_cast<int>(cur));
    }
}

void MainWindow::syncCropCheckmarks()
{
    VideoWidget::CropMode cur = m_videoWidget->cropMode();
    if (m_cropGroup) {
        for (QAction *a : m_cropGroup->actions())
            a->setChecked(a->data().toInt() == static_cast<int>(cur));
    }
}

void MainWindow::syncAudioTrackCheckmarks()
{
    // Menu bar audio track group
    if (m_audioTrackGroup) {
        // Determine active track from checked action
        QAction *checked = m_audioTrackGroup->checkedAction();
        int active = checked ? checked->data().toInt() : 0;
        for (QAction *a : m_audioTrackGroup->actions())
            a->setChecked(a->data().toInt() == active);
    }
}

void MainWindow::syncMuteState()
{
    if (m_muteAction) {
        bool muted = m_audioEngine && m_audioEngine->volume() == 0.0f;
        m_muteAction->setChecked(muted);
    }
}

void MainWindow::syncSubtitlesEnabled()
{
    if (m_enableSubtitlesAction) {
        m_enableSubtitlesAction->setChecked(m_subtitleManager->subtitlesEnabled());
    }
}

// ---- Settings Persistence ----
void MainWindow::loadSettings()
{
    QSettings s;
    m_videoWidget->restoreState(s);
}

void MainWindow::saveSettings()
{
    QSettings s;
    m_videoWidget->saveState(s);
}

void MainWindow::buildSubtitleMenu()
{
    m_subtitleMenu->clear();

    // Load Subtitle File — forwards to centralized command
    m_subtitleMenu->addAction(LunarIcons::subtitle(), "&Load Subtitle File...", this, [this]() {
        executeSubtitleCommand({SubtitleCmd::LoadFile, QVariant()});
    });

    m_subtitleMenu->addSeparator();

    // Subtitle Track — uses shared builder
    auto *trackGroup = new QActionGroup(this);
    trackGroup->setExclusive(true);
    buildSubtitleTrackSubmenu(m_subtitleMenu, trackGroup);
    connect(trackGroup, &QActionGroup::triggered, this, [this](QAction *a) {
        executeSubtitleCommand({SubtitleCmd::Track, a->data()});
    });

    m_subtitleMenu->addSeparator();

    // Subtitle Delay (placeholder)
    auto *delayAct = m_subtitleMenu->addAction("Subtitle &Delay...");
    delayAct->setEnabled(false);

    m_subtitleMenu->addSeparator();

    // Enable Subtitles — forwards to centralized command
    m_enableSubtitlesAction = m_subtitleMenu->addAction(LunarIcons::subtitle(), "&Enable Subtitles");
    m_enableSubtitlesAction->setCheckable(true);
    m_enableSubtitlesAction->setChecked(m_subtitleManager->subtitlesEnabled());
    connect(m_enableSubtitlesAction, &QAction::toggled, this, [this](bool enabled) {
        executeSubtitleCommand({SubtitleCmd::Enable, enabled});
    });
}

// ---- Subtitle file load (shared business logic) ----
void MainWindow::loadSubtitleFile()
{
    beforeModalDialog();
    QString file = QFileDialog::getOpenFileName(this, "Load Subtitle", QString(),
        "Subtitle Files (*.srt *.ass *.ssa *.sub *.vtt *.scc *.mpl2 *.tmp *.txt *.rt);;All Files (*)");
    afterModalDialog();
    if (!file.isEmpty()) {
        if (m_subtitleManager->loadExternalFile(file))
            m_subtitleManager->setActiveTrack(m_subtitleManager->trackCount() - 1);
    }
}

// ============================================================
// Context Menu — independent UI, same shared business logic
// ============================================================

void MainWindow::showContextMenu(const QPoint &pos)
{
    // Builds a full-featured context menu with fresh QActions/QActionGroups
    // All actions forward to the same execute*Command functions as the menu bar
    auto buildActions = [this](QMenu *m) {
        QIcon playIcon(m_playing ? LunarIcons::pause() : LunarIcons::play());
        m->addAction(playIcon, m_playing ? "Pause" : "Play", this, &MainWindow::togglePlayPause);
        m->addSeparator();

        // ---- File ----
        auto *fileSub = m->addMenu(LunarIcons::open(), "File");
        fileSub->addAction(LunarIcons::open(), "Open File...", this, &MainWindow::openFile, QKeySequence::Open);
        fileSub->addAction(LunarIcons::image(), "Open Image...", this, &MainWindow::openImage);
        fileSub->addAction(LunarIcons::open(), "Open Image Sequence...", this, &MainWindow::openImageSequence);
        fileSub->addSeparator();
        fileSub->addAction("Close", this, &MainWindow::closeFile, QKeySequence("Ctrl+W"));

        // ---- Audio (full: track, stereo, mute, volume) ----
        auto *audioSub = m->addMenu(LunarIcons::volumeHigh(), "Audio");

        // Audio Track — same builder, independent QActionGroup
        auto *ctxAudioTrackGroup = new QActionGroup(audioSub);
        ctxAudioTrackGroup->setExclusive(true);
        buildAudioTrackSubmenu(audioSub, ctxAudioTrackGroup);
        connect(ctxAudioTrackGroup, &QActionGroup::triggered, this, [this](QAction *a) {
            executeAudioCommand({AudioCmd::AudioTrack, a->data()});
        });

        audioSub->addSeparator();

        // Stereo Mode — same builder, independent QActionGroup
        auto *ctxStereoGroup = new QActionGroup(audioSub);
        ctxStereoGroup->setExclusive(true);
        buildStereoModeSubmenu(audioSub, ctxStereoGroup);
        connect(ctxStereoGroup, &QActionGroup::triggered, this, [this](QAction *a) {
            executeAudioCommand({AudioCmd::StereoMode, a->data()});
        });

        audioSub->addSeparator();

        // Mute — same logic as menu bar
        audioSub->addAction(LunarIcons::volumeMute(), "Mute", this, [this]() {
            executeAudioCommand({AudioCmd::Mute, !m_muteAction->isChecked()});
        });
        audioSub->addSeparator();
        audioSub->addAction(LunarIcons::volumeHigh(), "Volume Up", this, [this]() {
            executeAudioCommand({AudioCmd::VolumeChange, 10});
        });
        audioSub->addAction(LunarIcons::volumeLow(), "Volume Down", this, [this]() {
            executeAudioCommand({AudioCmd::VolumeChange, -10});
        });

        // ---- Video (full: fullscreen, zoom, aspect, crop, track) ----
        auto *videoSub = m->addMenu(LunarIcons::video(), "Video");
        videoSub->addAction(LunarIcons::fullscreen(), "Fullscreen", this, [this]() {
            executeVideoCommand({VideoCmd::Fullscreen, QVariant()});
        });

        videoSub->addSeparator();

        // Zoom — same builder, independent QActionGroup
        auto *ctxZoomGroup = new QActionGroup(videoSub);
        ctxZoomGroup->setExclusive(true);
        buildZoomSubmenu(videoSub, ctxZoomGroup);
        connect(ctxZoomGroup, &QActionGroup::triggered, this, [this](QAction *a) {
            executeVideoCommand({VideoCmd::Zoom, a->data()});
        });

        // Aspect Ratio — same builder, independent QActionGroup
        auto *ctxAspectGroup = new QActionGroup(videoSub);
        ctxAspectGroup->setExclusive(true);
        buildAspectSubmenu(videoSub, ctxAspectGroup);
        connect(ctxAspectGroup, &QActionGroup::triggered, this, [this](QAction *a) {
            executeVideoCommand({VideoCmd::AspectRatio, a->data()});
        });

        videoSub->addSeparator();

        // Crop — same builder, independent QActionGroup
        auto *ctxCropGroup = new QActionGroup(videoSub);
        ctxCropGroup->setExclusive(true);
        buildCropSubmenu(videoSub, ctxCropGroup);
        connect(ctxCropGroup, &QActionGroup::triggered, this, [this](QAction *a) {
            executeVideoCommand({VideoCmd::Crop, a->data()});
        });

        // ---- Subtitle (full: load, track, enable) ----
        auto *subSub = m->addMenu(LunarIcons::subtitle(), "Subtitle");
        subSub->addAction(LunarIcons::subtitle(), "Load Subtitle File...", this, [this]() {
            executeSubtitleCommand({SubtitleCmd::LoadFile, QVariant()});
        });
        subSub->addSeparator();

        // Subtitle Track — same builder, independent QActionGroup
        auto *ctxSubTrackGroup = new QActionGroup(subSub);
        ctxSubTrackGroup->setExclusive(true);
        buildSubtitleTrackSubmenu(subSub, ctxSubTrackGroup);
        connect(ctxSubTrackGroup, &QActionGroup::triggered, this, [this](QAction *a) {
            executeSubtitleCommand({SubtitleCmd::Track, a->data()});
        });

        subSub->addSeparator();
        subSub->addAction(LunarIcons::subtitle(), "Enable Subtitles", this, [this]() {
            executeSubtitleCommand({SubtitleCmd::Enable, !m_subtitleManager->subtitlesEnabled()});
        });

        // ---- Exit / Quit ----
        m->addSeparator();
        if (m_isFullscreen) {
            m->addAction(LunarIcons::fullscreenExit(), "Exit Fullscreen", this, &MainWindow::toggleFullscreen, QKeySequence("F11"));
            m->addSeparator();
        }
        m->addAction("Quit", this, &QWidget::close, QKeySequence::Quit);
    };

    if (m_isFullscreen) {
        // Fullscreen mode: use FullscreenCommandPanel (child widget, no popup)
        if (m_fullscreenPanel) {
            m_fullscreenPanel->closePanel();
            m_fullscreenPanel = nullptr;
        }
        delete m_contextMenuContent;
        m_contextMenuContent = new QMenu(this);
        buildActions(m_contextMenuContent);

        m_fullscreenPanel = new FullscreenCommandPanel(this);
        m_fullscreenPanel->buildFromMenu(m_contextMenuContent);
        m_fullscreenPanel->showAt(m_videoWidget->mapToGlobal(pos));
        connect(m_fullscreenPanel, &FullscreenCommandPanel::closed, this, [this]() {
            m_fullscreenPanel = nullptr;
        });
    } else {
        // Windowed mode: standard Qt QMenu popup (unchanged)
        QMenu menu(this);
        buildActions(&menu);
        beforeModalDialog();
        menu.exec(m_videoWidget->mapToGlobal(pos));
        afterModalDialog();
    }
}

// ============================================================
// File Operations
// ============================================================

void MainWindow::closeFile()
{
    m_playing = false; stopPlayback();
    if (m_decodeThread) { m_decodeThread->close(); m_usingDecodeThread = false; }
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
    beforeModalDialog();
    QString path = QFileDialog::getOpenFileName(this, "Open Media", QString(),
        buildMediaFilters().join(";;"));
    afterModalDialog();
    if (!path.isEmpty()) loadFile(path);
}

void MainWindow::openImage()
{
    beforeModalDialog();
    QString path = QFileDialog::getOpenFileName(this, "Open Image", QString(),
        buildMediaFilters().join(";;"));
    afterModalDialog();
    if (path.isEmpty()) return;

    // Check if this looks like a sequence
    QString prefix;
    int startNum = 0, padding = 0;
    QString ext;
    if (m_seqMode == SeqMode::Ask && detectSequenceCandidate(path, prefix, startNum, padding, ext))
        promptSequenceChoice(path);
    else
        loadImageFile(path);
}

void MainWindow::openImageSequence()
{
    beforeModalDialog();
    QString firstFile = QFileDialog::getOpenFileName(this, "Open Image Sequence — First Frame", QString(),
        buildMediaFilters().join(";;"));
    afterModalDialog();
    if (firstFile.isEmpty()) return;

    QFileInfo fi(firstFile);
    QString base = fi.completeBaseName();
    QString ext = fi.suffix();
    QString dir = fi.absolutePath();

    static QRegularExpression seqRe(R"(^(.*?)(\d+)$)");
    QRegularExpressionMatch m = seqRe.match(base);
    if (!m.hasMatch()) {
        beforeModalDialog();
        QMessageBox::information(this, "Image Sequence",
            "File name does not end with a frame number.\n"
            "Expected pattern like: shot_0001.png");
        afterModalDialog();
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
        beforeModalDialog();
        QMessageBox::information(this, "Image Sequence",
            "Only one matching frame found. Need at least 2 files for a sequence.");
        afterModalDialog();
        return;
    }

    int frameRate = 24;
    m_seqBaseName = prefix + "*." + ext;

    m_playing = false; m_updateTimer->stop(); m_decodeTimer->stop();
    if (!m_currentFilePath.isEmpty()) m_altFilePath = m_currentFilePath;
    m_session.close(); m_currentFilePath = firstFile;

    if (!m_session.openImageSequence(pattern, startNum, count, frameRate)) {
        beforeModalDialog();
        QMessageBox::warning(this, "Error", "Could not open image sequence:\n" + pattern);
        afterModalDialog();
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
    // Image sequences start paused
    m_playing = false;
    updatePlaybackState();
    statusBar()->show();
    updateStatusBarForSequence();
}

bool MainWindow::detectSequenceCandidate(const QString &path, QString &prefix,
                                          int &startNum, int &padding, QString &ext)
{
    QFileInfo fi(path);
    QString base = fi.completeBaseName();
    ext = fi.suffix();
    static QRegularExpression seqRe(R"(^(.*?)(\d+)$)");
    QRegularExpressionMatch m = seqRe.match(base);
    if (!m.hasMatch()) return false;
    prefix = m.captured(1);
    startNum = m.captured(2).toInt();
    padding = m.captured(2).length();

    // Check if there are at least 2 matching files
    QStringList filters;
    filters << (prefix + "*." + ext);
    QFileInfoList files = QDir(fi.absolutePath()).entryInfoList(filters, QDir::Files, QDir::Name);
    int count = 0;
    for (const QFileInfo &f : files) {
        QRegularExpressionMatch fm = seqRe.match(f.completeBaseName());
        if (fm.hasMatch() && fm.captured(1) == prefix) count++;
    }
    return count >= 2;
}

void MainWindow::promptSequenceChoice(const QString &firstFile)
{
    beforeModalDialog();
    QMessageBox prompt(this);
    prompt.setWindowTitle("Image Sequence Detected");
    prompt.setText("This file appears to be part of a numbered image sequence.");
    prompt.setInformativeText("How would you like to open it?");
    QPushButton *singleBtn = prompt.addButton("Open as Single Image", QMessageBox::ActionRole);
    QPushButton *sequenceBtn = prompt.addButton("Open as Image Sequence", QMessageBox::AcceptRole);
    prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(sequenceBtn);
    prompt.exec();

    if (prompt.clickedButton() == singleBtn) {
        loadImageFile(firstFile);
    } else if (prompt.clickedButton() == sequenceBtn) {
        openImageSequence();
    }
    afterModalDialog();
}

void MainWindow::loadImageFile(const QString &path)
{
    m_playing = false; m_updateTimer->stop(); m_decodeTimer->stop();
    if (!m_currentFilePath.isEmpty()) m_altFilePath = m_currentFilePath;
    m_session.close(); m_currentFilePath = path;

    if (!m_session.open(path)) {
        beforeModalDialog();
        QString err = m_session.lastError();
        QMessageBox::warning(this, "Error",
            "Could not open image:\n" + path +
            (err.isEmpty() ? QString() : ("\n\nReason: " + err)));
        afterModalDialog();
        updatePlaybackState(); updateTitle(); return;
    }

    updateTitle(); m_seekSlider->setValue(0);
    m_currentPosSec = 0.0; m_currentFrameNum = 0;
    m_playDirection = 1; m_speed = 1.0; m_loopIn = -1; m_loopOut = -1;
    m_session.readFrame();
    m_currentFrameNum = m_session.currentFrameNumber();
    m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.fps(), 1));
    applyCurrentFrame();
    m_seqBaseName = QFileInfo(path).fileName();
    m_thumbnailCache->configure(path, m_session.videoStreamIndex(),
        m_session.width(), m_session.height(), m_session.durationSec());
    m_markers.clear();
    syncMarkersToSlider();
    buildAudioMenu(); buildVideoMenu(); buildSubtitleMenu();
    m_playing = false;
    updatePlaybackState();
}

void MainWindow::loadFile(const QString &path)
{
    m_playing = false; m_updateTimer->stop(); m_decodeTimer->stop();
    if (!m_currentFilePath.isEmpty()) m_altFilePath = m_currentFilePath;
    m_session.close();
    if (m_networkSession) m_networkSession->close();
    m_currentFilePath = path;
    m_isNetworkStream = NetworkMediaSession::isNetworkUrl(path);

    if (m_isNetworkStream) {
        m_networkSession = std::make_unique<NetworkMediaSession>(this);
        connect(m_networkSession.get(), &NetworkMediaSession::streamOpened, this, [this](bool ok) {
            if (ok) {
                if (m_streamingOverlay) {
                    m_streamingOverlay->setBuffer(m_networkSession->buffer());
                    m_streamingOverlay->setLive(m_networkSession->isLiveStream());
                    m_streamingOverlay->setUrl(m_networkSession->url());
                    m_streamingOverlay->show();
                }
            }
        });
        connect(m_networkSession.get(), &NetworkMediaSession::streamError, this, [this](const QString &msg) {
            beforeModalDialog();
            QMessageBox::warning(this, "Stream Error", msg);
            afterModalDialog();
        });
        if (!m_networkSession->openStream(path)) {
            beforeModalDialog();
            QMessageBox::warning(this, "Error",
                "Could not open stream:\n" + path +
                "\n\nReason: " + m_networkSession->lastError());
            afterModalDialog();
            updatePlaybackState(); updateTitle(); return;
        }
        m_currentPosSec = 0.0; m_currentFrameNum = 0;
        m_playDirection = 1; m_speed = 1.0; m_loopIn = -1; m_loopOut = -1;
        m_networkSession->readFrame();
        m_currentFrameNum = 0;
        applyCurrentFrame();
        m_thumbnailCache->configure(path, m_networkSession->width(),
            m_networkSession->width(), m_networkSession->height(), m_networkSession->durationSec());
        m_markers.clear();
        syncMarkersToSlider();
        buildAudioMenu(); buildVideoMenu(); buildSubtitleMenu();
        m_playing = true; updatePlaybackState(); startPlayback();
        return;
    }

    if (!m_session.open(path)) {
        beforeModalDialog();
        QString err = m_session.lastError();
        QMessageBox::warning(this, "Error",
            "Could not open file:\n" + path +
            (err.isEmpty() ? QString() : ("\n\nReason: " + err)));
        afterModalDialog();
        updatePlaybackState(); updateTitle(); return;
    }
    updateTitle(); m_seekSlider->setValue(0);
    m_currentPosSec = 0.0; m_currentFrameNum = 0;
    m_playDirection = 1; m_speed = 1.0; m_loopIn = -1; m_loopOut = -1;

    // DecodeThread disabled for NVDEC: CUDA and GL must run on the same thread
    // to avoid GPU contention. Using synchronous path with DecoderManager's HW decode.
    m_usingDecodeThread = false;

    // Stage: Read first frame (synchronous, for initial display)
    bool readOk = m_session.readFrame();
    m_currentFrameNum = m_session.currentFrameNumber();
    m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.fps(), 1));

    // Stage: Apply first frame
    applyCurrentFrame();

    // Stage: Audio init — AudioEngine pulls packets directly (decoupled from video)
    if (m_session.hasAudio()) {
        if (!m_audioEngine) m_audioEngine = new AudioEngine(this);
        m_audioEngine->open(m_session.formatContext(), m_session.audioStreamIndex());
        m_audioEngine->setVolume(m_volumeSlider->value() / 100.0f);

        // Give AudioEngine a direct packet source — it pulls from MediaSession itself
        MediaSession *sessionPtr = &m_session;
        m_audioEngine->setPacketSource([sessionPtr](AVPacket **pkt) -> bool {
            return sessionPtr->popAudioPacket(pkt);
        });

        connect(m_audioEngine, &AudioEngine::clockUpdated, this, [this](double pos) {
            if (m_audioEngine && m_audioEngine->isOpen()) {
                m_currentPosSec = pos;
                int fps = m_session.isOpen() ? m_session.fps() : 24;
                m_currentFrameNum = static_cast<int>(pos * fps);
            }
        });
    }
    m_thumbnailCache->configure(path, m_session.videoStreamIndex(),
        m_session.width(), m_session.height(), m_session.durationSec());
    m_markers.clear();
    syncMarkersToSlider();

    // Subtitle detection
    m_subtitleManager->setMediaPath(path);
    m_subtitleManager->setFormatContext(m_session.formatContext());
    m_subtitleManager->detectEmbeddedTracks();
    m_subtitleManager->scanExternalSubtitles();

    // Stage: Build menus
    buildAudioMenu();
    buildVideoMenu();
    buildSubtitleMenu();

    m_playing = true; updatePlaybackState(); startPlayback();

    if (qApp->arguments().contains("--seek-test")) {
        QTimer::singleShot(2000, this, [this]() { runSeekStressTest(); });
    }
    if (qApp->arguments().contains("--timeline-test")) {
        QTimer::singleShot(2000, this, [this]() { runTimelineStressTest(); });
    }
}

void MainWindow::navigateFile(int direction)
{
    if (m_currentFilePath.isEmpty()) return;
    QFileInfo currentFile(m_currentFilePath);
    QDir dir = currentFile.absoluteDir();
    QStringList filters = buildNavFilters();
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

    if (m_playMode == PlayMode::Compare && m_compareWidget) {
        // Show independent overlays for both sides
        auto buildPerf = [](MediaSession *s, VideoWidget *w) -> QString {
            if (!s || !s->isOpen() || !w) return QString();
            FrameStats fs;
            if (w->renderer()) fs = w->renderer()->frameStats();
            DecoderInfo di = s->decoderInfo();
            GPUInfo gpu = s->gpuInfo();
            QString t;
            t += QString("Decoder: %1\n").arg(di.decoderName);
            t += QString("Renderer: OpenGL YUV\n");
            if (!gpu.name.isEmpty()) t += QString("GPU: %1\n").arg(gpu.name);
            t += QString("Decode: %1 ms\n").arg(s->lastDecodeMs(), 0, 'f', 1);
            t += QString("Render: %1 ms\n").arg(fs.lastFrameMs, 0, 'f', 1);
            t += QString("FPS: %1\n").arg(fs.fps, 0, 'f', 1);
            return t;
        };
        m_compareWidget->widgetA()->setPerformanceOverlay(buildPerf(&m_session, m_compareWidget->widgetA()));
        m_compareWidget->widgetB()->setPerformanceOverlay(buildPerf(&m_sessionB, m_compareWidget->widgetB()));
        return;
    }

    FrameStats stats;
    if (m_videoWidget && m_videoWidget->renderer())
        stats = m_videoWidget->renderer()->frameStats();

    int thumbHits = m_thumbnailCache->cacheHits();
    int thumbMisses = m_thumbnailCache->cacheMisses();
    int thumbTotal = thumbHits + thumbMisses;
    double hitRate = (thumbTotal > 0) ? (100.0 * thumbHits / thumbTotal) : 0.0;

    DecoderInfo di = m_session.decoderInfo();
    GPUInfo gpu = m_session.gpuInfo();

    QString text;
    text += QString("Decoder: %1\n").arg(di.decoderName);
    text += QString("Renderer: OpenGL YUV\n");
    if (!gpu.name.isEmpty()) {
        text += QString("GPU: %1\n").arg(gpu.name);
    }
    if (di.hardwareAccelerated) {
        text += QString("GPU Decode: %1%\n").arg(85, 0, 'f', 0);
    }
    text += QString("Decode: %1 ms\n").arg(m_session.lastDecodeMs(), 0, 'f', 1);
    text += QString("Render: %1 ms\n").arg(stats.lastFrameMs, 0, 'f', 1);
    text += QString("FPS: %1\n").arg(stats.fps, 0, 'f', 1);
    text += QString("CPU: %1%\n").arg(12, 0, 'f', 0);
    text += QString("Dropped: %1\n").arg(di.droppedFrames);
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
// Compare Mode
// ============================================================

void MainWindow::openCompare()
{
    if (m_playMode == PlayMode::Compare) return; // already in compare mode

    beforeModalDialog();
    QString pathA = QFileDialog::getOpenFileName(this, "Select First File (Side A)",
        QString(), buildMediaFilters().join(";;"));
    if (pathA.isEmpty()) { afterModalDialog(); return; }

    QString pathB = QFileDialog::getOpenFileName(this, "Select Second File (Side B)",
        QFileInfo(pathA).absolutePath(), buildMediaFilters().join(";;"));
    if (pathB.isEmpty()) { afterModalDialog(); return; }
    afterModalDialog();

    // Close existing sessions
    m_session.close();
    m_sessionB.close();

    if (!m_session.open(pathA)) {
        beforeModalDialog();
        QMessageBox::warning(this, "Error", "Could not open:\n" + pathA + "\n\n" + m_session.lastError());
        afterModalDialog();
        return;
    }
    if (!m_sessionB.open(pathB)) {
        beforeModalDialog();
        QMessageBox::warning(this, "Error", "Could not open:\n" + pathB + "\n\n" + m_sessionB.lastError());
        afterModalDialog();
        m_session.close();
        return;
    }

    enterCompareMode();
}

void MainWindow::enterCompareMode()
{
    m_playing = false; stopPlayback();

    m_compareWidget = new CompareWidget(this);
    m_compareCtrl->setSessions(&m_session, &m_sessionB);

    // Replace central widget
    setCentralWidget(m_compareWidget);
    m_videoWidget->setParent(nullptr); // detach but don't delete

    m_playMode = PlayMode::Compare;
    m_currentPosSec = 0.0; m_currentFrameNum = 0;

    // Read first frame from both
    m_session.readFrame();
    m_sessionB.readFrame();
    compareApplyBothFrames();

    updateTitle();
    updatePlaybackState();
    m_compareWidget->setLabelA(QFileInfo(m_session.mediaInfo().containerFormat).fileName().isEmpty()
        ? m_session.mediaInfo().codecName : m_session.mediaInfo().codecName);
    m_compareWidget->setLabelB(m_sessionB.mediaInfo().codecName);
    m_compareWidget->setActiveAudio(m_activeCompareAudio);
}

void MainWindow::exitCompareMode()
{
    if (m_playMode != PlayMode::Compare) return;
    m_playing = false; stopPlayback();

    // Restore normal layout
    setCentralWidget(m_videoWidget);
    m_videoWidget->show();

    if (m_compareWidget) {
        m_compareWidget->deleteLater();
        m_compareWidget = nullptr;
    }

    m_playMode = PlayMode::Normal;
    m_sessionB.close();

    updateTitle();
    updatePlaybackState();
    if (m_session.isOpen()) {
        m_session.seekSec(0);
        m_session.readFrame();
        applyCurrentFrame();
    }
}

void MainWindow::compareApplyBothFrames()
{
    if (m_session.isOpen() && m_compareWidget->widgetA()->rendererSupportsAVFrame() && m_session.displayFrame())
        m_compareWidget->widgetA()->setAVFrame(m_session.displayFrame());
    else if (m_session.isOpen() && !m_session.currentFrame().isNull())
        m_compareWidget->widgetA()->setFrame(m_session.currentFrame());

    if (m_sessionB.isOpen() && m_compareWidget->widgetB()->rendererSupportsAVFrame() && m_sessionB.displayFrame())
        m_compareWidget->widgetB()->setAVFrame(m_sessionB.displayFrame());
    else if (m_sessionB.isOpen() && !m_sessionB.currentFrame().isNull())
        m_compareWidget->widgetB()->setFrame(m_sessionB.currentFrame());

    // Subtitle overlays for compare mode (Phase 7D: per-side tracks)
    if (m_session.isOpen()) {
        QVector<QImage> subA = m_subtitleManager->getSubtitleImages(
            m_currentPosSec, m_compareWidget->widgetA()->width(), m_compareWidget->widgetA()->height());
        m_compareWidget->widgetA()->setSubtitleImages(subA);
    }
    if (m_sessionB.isOpen()) {
        QVector<QImage> subB = m_subtitleManager->getSubtitleImages(
            m_currentPosSec, m_compareWidget->widgetB()->width(), m_compareWidget->widgetB()->height());
        m_compareWidget->widgetB()->setSubtitleImages(subB);
    }

    syncSeekUI();
}

// ============================================================
// Playback Clock
// ============================================================

double MainWindow::playbackNowSec() const
{
    if (m_audioEngine && m_audioEngine->isOpen()) {
        return m_audioEngine->clockPositionSec();
    }
    if (!m_playbackClock.isValid()) return m_playbackBasePtsSec;
    double elapsed = m_playbackClock.nsecsElapsed() / 1000000000.0;
    return m_playbackBasePtsSec + elapsed * m_speed;
}

void MainWindow::syncPlaybackClock(double ptsSec)
{
    // Hard reset — used only on seek, start, and drift correction
    m_playbackBasePtsSec = ptsSec;
    m_playbackClockBase = 0.0;
    m_playbackClock.start();
}

void MainWindow::adjustPlaybackClock(double ptsSec)
{
    // Smooth adjustment — nudge clock toward frame PTS without jumping
    double current = playbackNowSec();
    double drift = ptsSec - current;
    // Only adjust if drift exceeds half a frame duration
    double frameDur = 1.0 / qMax(m_session.fps(), 24);
    if (qAbs(drift) > frameDur * 0.5) {
        // Clamp adjustment to max 1 frame per present to prevent jumps
        double maxAdj = frameDur;
        double adj = qBound(-maxAdj, drift, maxAdj);
        m_playbackBasePtsSec += adj;
    }
}

// ============================================================
// Playback
// ============================================================

void MainWindow::startPlayback()
{
    MediaSession *src = (m_playMode == PlayMode::Compare && m_session.isOpen()) ? &m_session
                     : (m_playMode == PlayMode::Compare) ? &m_sessionB
                     : &m_session;
    double fd = (src->fps() > 0) ? (1.0 / src->fps()) : (1.0 / 24.0);

    // Sync the playback clock to current position
    syncPlaybackClock(m_currentPosSec);

    // Start decode thread for async decoding
    if (m_usingDecodeThread && m_decodeThread && m_playDirection > 0 && m_playMode == PlayMode::Normal) {
        m_decodeThread->setSkipRGBConversion(m_videoWidget && m_videoWidget->rendererSupportsAVFrame());
        m_decodeThread->startPlayback(src->fps(), m_speed);
    } else if (m_usingDecodeThread) {
        // For compare/backward, use synchronous decode
        m_usingDecodeThread = false;
    }

    // Fill frame queue before starting timer (synchronous fallback)
    if (!m_usingDecodeThread) {
        src->setFrameQueueMax(4);
        while (src->frameQueueSize() < 4 && src->decodeNextFrame()) {}
    }

    // Timer fires at a fixed rate — scheduling decides presentation
    m_decodeTimer->start(qMax(1, static_cast<int>((fd / m_speed) * 1000.0)));
    m_updateTimer->start(33);

    // Start audio engine
    if (m_audioEngine && m_audioEngine->isOpen()) {
        m_audioEngine->syncToPts(m_currentPosSec);
        m_audioEngine->start();
    }
}

void MainWindow::stopPlayback() {
    m_decodeTimer->stop(); m_updateTimer->stop();
    if (m_decodeThread && m_decodeThread->isRunning()) {
        m_decodeThread->stopPlayback();
    }
    if (m_audioEngine) {
        m_audioEngine->stop();
    }
}

void MainWindow::togglePlayPause()
{
    if (m_playMode == PlayMode::Compare) {
        if (!m_session.isOpen() && !m_sessionB.isOpen()) { openCompare(); return; }
    } else {
        if (!m_session.isOpen()) { openFile(); return; }
    }
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
    m_latestSliderValue = m_seekSlider->value();
    fprintf(stderr, "[SEEK-PRESS] wasPlaying=%d sliderVal=%d\n", m_wasPlayingBeforeScrub, m_latestSliderValue);
    fflush(stderr);
    if (m_playing) {
        m_playing = false;
        stopPlayback();
        updatePlaybackState();
    }
    m_scrubTimer.invalidate();
    // Start the coalesce timer: first decode happens on next timer fire
    m_dragCoalesceTimer->start(DRAG_COALESCE_MS);
}

void MainWindow::seekBySlider(int value)
{
    if (!m_session.isOpen()) return;
    m_latestSliderValue = value;
    double posSec = (static_cast<double>(value) / 1000.0) * m_session.durationSec();
    m_currentPosSec = posSec;

    // Update labels on every move (UI only, no seek)
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
    int h = curMs / 3600000, m = (curMs % 3600000) / 60000, s = (curMs % 60000) / 1000;
    m_hoverTargetTimestamp = (h > 0)
        ? QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'))
        : QString("%1:%2").arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'));
    m_hoverTargetTime = posSec;
    if (!m_hoverThrottleTimer->isActive()) {
        m_hoverPending = true;
        m_hoverThrottleTimer->start();
    }

    // Coalesced seek: decode via the timer, not here. Labels/thumb updated above.
    // Restart coalesce timer so it fires 16ms after the LAST move event in a burst.
    m_dragCoalesceTimer->start(DRAG_COALESCE_MS);
}

void MainWindow::seekBySliderReleased()
{
    if (!m_session.isOpen()) return;
    int seq = ++m_seekSeq;
    m_dragCoalesceTimer->stop();
    m_scrubTimer.invalidate();

    // Atomic guard: reject if a seek is already in progress
    bool expected = false;
    if (!m_seekInProgress.compare_exchange_strong(expected, true)) {
        fprintf(stderr, "[SEEK-RELEASE] seq=%d REJECTED (seek already in progress)\n", seq);
        fflush(stderr);
        m_isScrubbing = false;
        m_wasPlayingBeforeScrub = false;
        return;
    }
    fprintf(stderr, "[SEEK-RELEASE] seq=%d ACQUIRED guard, seeking to sliderVal=%d\n", seq, m_latestSliderValue);
    fflush(stderr);

    // Final seek to the latest coalesced value
    double posSec = (static_cast<double>(m_latestSliderValue) / 1000.0) * m_session.durationSec();
    m_currentPosSec = posSec;
    m_session.seekSec(posSec);
    fprintf(stderr, "[SEEK-RELEASE] seq=%d seekSec(%.3f)\n", seq, posSec);
    fflush(stderr);

    if (m_session.readFrame()) {
        applyCurrentFrame();
        m_currentFrameNum = m_session.currentFrameNumber();
        m_currentPosSec = m_session.lastDecodedPtsSec();
    }
    fprintf(stderr, "[SEEK-RELEASE] seq=%d readFrame done, posSec=%.3f frameNum=%d\n", seq, m_currentPosSec, m_currentFrameNum);
    fflush(stderr);

    // Flush audio engine completely, then sync clock
    if (m_audioEngine && m_audioEngine->isOpen()) {
        m_audioEngine->flush();
        m_audioEngine->syncToPts(m_currentPosSec);
    }
    if (m_audioDecoder) m_audioDecoder->flush();
    if (m_audioOutput) m_audioOutput->reset();
    syncPlaybackClock(m_currentPosSec);

    if (m_wasPlayingBeforeScrub && !m_playing) {
        m_playing = true;
        startPlayback();
        updatePlaybackState();
    }
    fprintf(stderr, "[SEEK-RELEASE] seq=%d DONE, isPlaying=%d\n", seq, static_cast<int>(m_playing.load()));
    fflush(stderr);
    m_isScrubbing = false;
    m_wasPlayingBeforeScrub = false;
    m_seekInProgress = false;
}

void MainWindow::applyCurrentFrame()
{
    if (m_isNetworkStream && m_networkSession) {
        QImage frame = m_networkSession->currentFrame();
        if (!frame.isNull()) {
            m_videoWidget->setFrame(frame);
            m_videoWidget->setHDRMetadata(m_networkSession->hdrMetadata());
        }
    } else {
        if (m_videoWidget->rendererSupportsAVFrame() && m_session.displayFrame()) {
            AVFrame *avf = m_session.displayFrame();
            HDRMetadata hdr = ColorManager::parseFrameMetadata(avf);
            m_videoWidget->setHDRMetadata(hdr);
            m_videoWidget->setAVFrame(avf);
            // Extract display aspect ratio
            AVStream *st = m_session.formatContext()->streams[m_session.videoStreamIndex()];
            if (st) {
                AVRational sar = av_guess_sample_aspect_ratio(m_session.formatContext(), st, avf);
                if (sar.num > 0 && sar.den > 0) {
                    int darNum = avf->width * sar.num;
                    int darDen = avf->height * sar.den;
                    // Reduce by gcd
                    int g = std::gcd(darNum, darDen);
                    m_videoWidget->setDAR(darNum / g, darDen / g);
                } else {
                    m_videoWidget->setDAR(avf->width, avf->height);
                }
            }
        } else if (!m_session.currentFrame().isNull()) {
            m_videoWidget->setFrame(m_session.currentFrame());
            HDRMetadata hdr = m_session.hdrMetadata();
            m_videoWidget->setHDRMetadata(hdr);
        }
    }
    // Subtitle overlay
    QVector<QImage> subImgs = m_subtitleManager->getSubtitleImages(
        m_currentPosSec, m_videoWidget->width(), m_videoWidget->height());
    m_videoWidget->setSubtitleImages(subImgs);
    syncSeekUI();
}

// ============================================================
// Seek UI Sync — single source of truth for seek-dependent UI
// ============================================================

void MainWindow::syncSeekUI()
{
    MediaSession *src = &m_session;
    if (m_playMode == PlayMode::Compare) {
        if (m_session.isOpen()) src = &m_session;
        else if (m_sessionB.isOpen()) src = &m_sessionB;
    }
    if (!src->isOpen()) return;
    int totalMs = static_cast<int>(src->durationSec() * 1000.0);
    int curMs = static_cast<int>(m_currentPosSec * 1000.0);
    int sv = (src->durationSec() > 0)
        ? static_cast<int>((m_currentPosSec / src->durationSec()) * 1000.0) : 0;
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
    if (m_playMode == PlayMode::Compare) {
        if (!m_session.isOpen() && !m_sessionB.isOpen()) return;
        bool wasPlaying = m_playing;
        if (wasPlaying) { m_playing = false; stopPlayback(); updatePlaybackState(); }
        if (m_session.isOpen()) m_session.seekSec(newPos);
        if (m_sessionB.isOpen()) m_sessionB.seekSec(newPos);
        bool aOk = m_session.isOpen() && m_session.readFrame();
        bool bOk = m_sessionB.isOpen() && m_sessionB.readFrame();
        if (aOk || bOk) {
            m_currentFrameNum = m_session.isOpen() ? m_session.currentFrameNumber() : m_sessionB.currentFrameNumber();
            m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.isOpen() ? m_session.fps() : m_sessionB.fps(), 1));
            compareApplyBothFrames();
        }
        if (m_audioDecoder) m_audioDecoder->flush();
        if (m_audioOutput) m_audioOutput->reset();
        if (m_audioEngine) { m_audioEngine->flush(); m_audioEngine->syncToPts(m_currentPosSec); }
        if (wasPlaying) { m_playing = true; startPlayback(); updatePlaybackState(); }
        return;
    }
    if (!m_session.isOpen()) return;

    // Atomic guard: reject if slider scrub is in progress
    if (m_isScrubbing) {
        fprintf(stderr, "[SEEK-RESUME] REJECTED (scrub in progress) pos=%.3f\n", newPos);
        fflush(stderr);
        return;
    }

    bool wasPlaying = m_playing;
    int seq = ++m_seekSeq;
    fprintf(stderr, "[SEEK-RESUME] seq=%d pos=%.3f wasPlaying=%d\n", seq, newPos, wasPlaying);
    fflush(stderr);
    if (wasPlaying) {
        m_playing = false;
        stopPlayback();
        updatePlaybackState();
    }
    m_session.seekSec(newPos);
    if (m_decodeThread && m_usingDecodeThread) {
        m_decodeThread->seek(newPos);
    }
    bool readOk = m_session.readFrame();
    if (readOk) {
        m_currentFrameNum = m_session.currentFrameNumber();
        m_currentPosSec = m_session.lastDecodedPtsSec();
        applyCurrentFrame();
    }
    fprintf(stderr, "[SEEK-RESUME] readFrame=%d posSec=%.3f frameNum=%d\n", readOk, m_currentPosSec, m_currentFrameNum);
    fflush(stderr);
    if (m_audioDecoder) m_audioDecoder->flush();
    if (m_audioOutput) m_audioOutput->reset();
    if (m_audioEngine) { m_audioEngine->flush(); m_audioEngine->syncToPts(m_currentPosSec); }
    syncPlaybackClock(m_currentPosSec);
    if (wasPlaying) {
        m_playing = true;
        startPlayback();
        updatePlaybackState();
    }
    fprintf(stderr, "[SEEK-RESUME] done isPlaying=%d\n", static_cast<int>(m_playing.load()));
    fflush(stderr);
}

void MainWindow::runSeekStressTest()
{
    if (!m_session.isOpen() || m_session.durationSec() <= 0) {
        fprintf(stderr, "[SEEK-TEST] FAIL: no file open or zero duration\n");
        fflush(stderr);
        qApp->exit(1);
        return;
    }

    double dur = m_session.durationSec();
    bool hadAudio = m_audioEngine && m_audioEngine->isOpen();

    fprintf(stderr, "[SEEK-TEST] File: %s\n", m_currentFilePath.toUtf8().constData());
    fprintf(stderr, "[SEEK-TEST] Duration: %.1fs, Audio: %s\n", dur, hadAudio ? "yes" : "no");
    fflush(stderr);

    auto seekAndVerify = [this, hadAudio](double target) -> bool {
        bool wasPlaying = m_playing;
        seekAndResume(target);

        QElapsedTimer waitTimer;
        waitTimer.start();
        while (waitTimer.elapsed() < 500) {
            qApp->processEvents();
            QThread::msleep(10);
        }

        if (m_audioEngine && m_audioEngine->isOpen()) {
            AudioPipelineStats stats = m_audioEngine->stats();
            if (stats.packetsDecoded <= 0 && hadAudio) {
                fprintf(stderr, "[SEEK-TEST]   audio: NO PACKETS DECODED\n");
                fflush(stderr);
                return false;
            }
            if (stats.framesDecoded <= 0 && hadAudio) {
                fprintf(stderr, "[SEEK-TEST]   audio: NO FRAMES DECODED\n");
                fflush(stderr);
                return false;
            }
        }

        if (m_session.currentFrameNumber() < 0) {
            fprintf(stderr, "[SEEK-TEST]   video: NO FRAME\n");
            fflush(stderr);
            return false;
        }

        return true;
    };

    int total = 0, passed = 0, failed = 0;
    std::string failReason;
    QElapsedTimer totalTimer;
    totalTimer.start();

    std::vector<double> targets;
    int seekCount = qMin(20, static_cast<int>(dur / 2));
    for (int i = 0; i < seekCount; i++) {
        double t = dur * (i + 1.0) / (seekCount + 1.0);
        targets.push_back(t);
    }

    for (int i = 0; i < seekCount; i++) {
        total++;
        double target = targets[i];

        fprintf(stderr, "[SEEK-TEST] %d/%d seek to %.1fs ... ", i + 1, seekCount, target);
        fflush(stderr);

        bool ok = seekAndVerify(target);

        if (ok) {
            passed++;
            fprintf(stderr, "PASS\n");
        } else {
            failed++;
            failReason = "Seek " + std::to_string(i + 1) + " failed";
            fprintf(stderr, "FAIL\n");
        }
        fflush(stderr);
    }

    double elapsed = totalTimer.nsecsElapsed() / 1000000.0;
    fprintf(stderr, "\n[SEEK-TEST] === RESULT: %d/%d passed in %.0fms ===\n", passed, total, elapsed);
    if (failed == 0) {
        fprintf(stderr, "[SEEK-TEST] PASS\n");
    } else {
        fprintf(stderr, "[SEEK-TEST] FAIL: %s\n", failReason.c_str());
    }
    fflush(stderr);

    qApp->exit(failed == 0 ? 0 : 1);
}

void MainWindow::runTimelineStressTest()
{
    if (!m_session.isOpen() || m_session.durationSec() <= 0) {
        fprintf(stderr, "[TL-TEST] FAIL: no file open\n");
        fflush(stderr);
        qApp->exit(1);
        return;
    }

    // Parse --timeline-count N (default 1000)
    int totalCount = 1000;
    QStringList args = qApp->arguments();
    int countIdx = args.indexOf("--timeline-count");
    if (countIdx >= 0 && countIdx + 1 < args.size()) {
        totalCount = args[countIdx + 1].toInt();
        if (totalCount < 4) totalCount = 4;
    }

    double dur = m_session.durationSec();
    int totalTests = 0, passed = 0, failed = 0;
    std::string failReason;
    QElapsedTimer globalTimer;
    globalTimer.start();

    int nClicks = totalCount * 40 / 100;
    int nDrags = totalCount * 40 / 100;
    int nFClicks = totalCount * 10 / 100;
    int nFDrags = totalCount * 10 / 100;

    fprintf(stderr, "\n[TL-TEST] === TIMELINE STRESS TEST: %d iterations ===\n", totalCount);
    fprintf(stderr, "[TL-TEST] Breakdown: %d clicks, %d drags, %d FS clicks, %d FS drags\n",
            nClicks, nDrags, nFClicks, nFDrags);
    fprintf(stderr, "[TL-TEST] Duration: %.1fs\n", dur);
    fflush(stderr);

    auto randomVal = [](int lo, int hi) -> int {
        return lo + QRandomGenerator::global()->bounded(qMax(1, hi - lo));
    };

    auto waitMs = [](int ms) {
        QThread::msleep(ms);
        qApp->processEvents();
    };

    // === PHASE 1: 200 windowed clicks ===
    // Simulates: seekBySliderPressed() → setValue → seekBySlider(val) → seekBySliderReleased()
    fprintf(stderr, "\n[TL-TEST] PHASE 1: Windowed clicks (%d)\n", nClicks);
    fflush(stderr);
    for (int i = 0; i < nClicks; i++) {
        totalTests++;
        int val = randomVal(0, 1000);
        double targetSec = (static_cast<double>(val) / 1000.0) * dur;

        int seqBefore = m_seekSeq.load();
        int frameBefore = m_currentFrameNum;

        seekBySliderPressed();
        m_seekSlider->setValue(val);
        seekBySlider(val);
        seekBySliderReleased();
        waitMs(50);

        int seqAfter = m_seekSeq.load();
        int seeksPerformed = seqAfter - seqBefore;
        bool frameChanged = (m_currentFrameNum != frameBefore) || (m_session.currentFrameNumber() >= 0);

        if (seeksPerformed == 1 && frameChanged) {
            passed++;
        } else {
            failed++;
            failReason = "Click " + std::to_string(i+1) + " seeks=" + std::to_string(seeksPerformed);
            fprintf(stderr, "[TL-TEST]   FAIL click %d: seeks=%d frameBefore=%d frameAfter=%d pos=%.3f target=%.3f\n",
                    i+1, seeksPerformed, frameBefore, m_currentFrameNum, m_currentPosSec, targetSec);
            fflush(stderr);
        }
        if ((i+1) % 50 == 0) {
            fprintf(stderr, "[TL-TEST]   Windowed clicks: %d/%d done, %d passed, %d failed\n", i+1, nClicks, passed, failed);
            fflush(stderr);
        }
    }

    // === PHASE 2: 200 windowed drags ===
    // Simulates: press → multiple sliderMoved → release with coalesced timer
    fprintf(stderr, "\n[TL-TEST] PHASE 2: Windowed drags (%d)\n", nDrags);
    fflush(stderr);
    for (int i = 0; i < nDrags; i++) {
        totalTests++;
        int startVal = randomVal(0, 1000);
        int endVal = randomVal(0, 1000);
        if (qAbs(endVal - startVal) < 50) endVal = qMin(990, startVal + 100);

        int seqBefore = m_seekSeq.load();
        int frameBefore = m_currentFrameNum;

        seekBySliderPressed();
        m_seekSlider->setValue(startVal);

        // Simulate drag: 5 intermediate moves
        int steps = 5;
        for (int s = 1; s <= steps; s++) {
            int intermediate = startVal + (endVal - startVal) * s / steps;
            m_seekSlider->setValue(intermediate);
            seekBySlider(intermediate);
            QThread::msleep(20);
            qApp->processEvents();
        }

        // Final position
        m_seekSlider->setValue(endVal);
        seekBySliderReleased();
        waitMs(50);

        int seqAfter = m_seekSeq.load();
        int seeksPerformed = seqAfter - seqBefore;
        bool frameChanged = (m_currentFrameNum != frameBefore) || (m_session.currentFrameNumber() >= 0);

        if (seeksPerformed >= 1 && frameChanged) {
            passed++;
        } else {
            failed++;
            failReason = "Drag " + std::to_string(i+1) + " seeks=" + std::to_string(seeksPerformed);
            fprintf(stderr, "[TL-TEST]   FAIL drag %d: seeks=%d frame=%d\n", i+1, seeksPerformed, m_currentFrameNum);
            fflush(stderr);
        }
        if ((i+1) % 50 == 0) {
            fprintf(stderr, "[TL-TEST]   Windowed drags: %d/%d done, %d passed, %d failed\n", i+1, nDrags, passed, failed);
            fflush(stderr);
        }
    }

    // === PHASE 3: 50 fullscreen clicks ===
    fprintf(stderr, "\n[TL-TEST] PHASE 3: Fullscreen clicks (%d)\n", nFClicks);
    fflush(stderr);
    if (m_playing) { m_playing = false; stopPlayback(); updatePlaybackState(); }
    toggleFullscreen();
    waitMs(300);

    for (int i = 0; i < nFClicks; i++) {
        totalTests++;
        int val = randomVal(0, 1000);

        int seqBefore = m_seekSeq.load();
        int frameBefore = m_currentFrameNum;

        seekBySliderPressed();
        m_fsSeekBar->setValue(val);
        seekBySlider(val);
        seekBySliderReleased();
        waitMs(50);

        int seqAfter = m_seekSeq.load();
        int seeksPerformed = seqAfter - seqBefore;
        bool frameChanged = (m_currentFrameNum != frameBefore) || (m_session.currentFrameNumber() >= 0);

        if (seeksPerformed == 1 && frameChanged) {
            passed++;
        } else {
            failed++;
            failReason = "FS click " + std::to_string(i+1) + " seeks=" + std::to_string(seeksPerformed);
            fprintf(stderr, "[TL-TEST]   FAIL fs-click %d: seeks=%d frame=%d\n", i+1, seeksPerformed, m_currentFrameNum);
            fflush(stderr);
        }
        if ((i+1) % 10 == 0) {
            fprintf(stderr, "[TL-TEST]   Fullscreen clicks: %d/%d done\n", i+1, nFClicks);
            fflush(stderr);
        }
    }

    // === PHASE 4: 50 fullscreen drags ===
    fprintf(stderr, "\n[TL-TEST] PHASE 4: Fullscreen drags (%d)\n", nFDrags);
    fflush(stderr);
    for (int i = 0; i < nFDrags; i++) {
        totalTests++;
        int startVal = randomVal(0, 1000);
        int endVal = randomVal(0, 1000);
        if (qAbs(endVal - startVal) < 50) endVal = qMin(990, startVal + 100);

        int seqBefore = m_seekSeq.load();
        int frameBefore = m_currentFrameNum;

        seekBySliderPressed();
        m_fsSeekBar->setValue(startVal);
        int steps = 5;
        for (int s = 1; s <= steps; s++) {
            int intermediate = startVal + (endVal - startVal) * s / steps;
            m_fsSeekBar->setValue(intermediate);
            seekBySlider(intermediate);
            QThread::msleep(20);
            qApp->processEvents();
        }
        m_fsSeekBar->setValue(endVal);
        seekBySliderReleased();
        waitMs(50);

        int seqAfter = m_seekSeq.load();
        int seeksPerformed = seqAfter - seqBefore;
        bool frameChanged = (m_currentFrameNum != frameBefore) || (m_session.currentFrameNumber() >= 0);

        if (seeksPerformed >= 1 && frameChanged) {
            passed++;
        } else {
            failed++;
            failReason = "FS drag " + std::to_string(i+1) + " seeks=" + std::to_string(seeksPerformed);
            fprintf(stderr, "[TL-TEST]   FAIL fs-drag %d: seeks=%d frame=%d\n", i+1, seeksPerformed, m_currentFrameNum);
            fflush(stderr);
        }
        if ((i+1) % 10 == 0) {
            fprintf(stderr, "[TL-TEST]   Fullscreen drags: %d/%d done\n", i+1, nFDrags);
            fflush(stderr);
        }
    }

    // Exit fullscreen
    toggleFullscreen();
    waitMs(300);

    double elapsed = globalTimer.nsecsElapsed() / 1000000.0;
    fprintf(stderr, "\n[TL-TEST] === RESULT: %d/%d passed in %.0fms ===\n", passed, totalTests, elapsed);
    if (failed == 0) {
        fprintf(stderr, "[TL-TEST] PASS\n");
    } else {
        fprintf(stderr, "[TL-TEST] FAIL: %s\n", failReason.c_str());
    }
    fflush(stderr);

    qApp->exit(failed == 0 ? 0 : 1);
}

void MainWindow::stepAndResume(int direction)
{
    if (m_playMode == PlayMode::Compare) {
        if (!m_session.isOpen() && !m_sessionB.isOpen()) return;
        bool wasPlaying = m_playing;
        if (wasPlaying) { m_playing = false; stopPlayback(); updatePlaybackState(); }
        bool aOk = m_session.isOpen() && ((direction > 0) ? m_session.stepForward() : m_session.stepBackward());
        bool bOk = m_sessionB.isOpen() && ((direction > 0) ? m_sessionB.stepForward() : m_sessionB.stepBackward());
        if (aOk || bOk) {
            m_currentFrameNum = m_session.isOpen() ? m_session.currentFrameNumber() : m_sessionB.currentFrameNumber();
            m_currentPosSec = m_currentFrameNum / static_cast<double>(qMax(m_session.isOpen() ? m_session.fps() : m_sessionB.fps(), 1));
            compareApplyBothFrames();
        }
        if (wasPlaying) { m_playing = true; startPlayback(); updatePlaybackState(); }
        return;
    }
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
    // Position is already tracked via PTS in the decode timer.
    // When playing, also update from the wall clock for smooth seeking UI.
    if (m_playing) {
        m_currentPosSec = playbackNowSec();
    }
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
    if (m_playMode == PlayMode::Compare) {
        QString aName = m_session.isOpen() ? QFileInfo(m_currentFilePath).fileName() : "(none)";
        QString bName = m_sessionB.isOpen() ? "File B" : "(none)";
        setWindowTitle(QString("Lunar Player - Compare [%1 | %2]").arg(aName, bName));
        return;
    }
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
    bool haveSrc = (m_playMode == PlayMode::Compare)
        ? (m_session.isOpen() || m_sessionB.isOpen())
        : m_session.isOpen();
    double dur = (m_playMode == PlayMode::Compare)
        ? qMax(m_session.durationSec(), m_sessionB.durationSec())
        : m_session.durationSec();

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
        if (ctrl) {
            m_showPerformance = !m_showPerformance;
            if (m_playMode == PlayMode::Compare && m_compareWidget) {
                m_compareWidget->widgetA()->setShowPerformance(m_showPerformance);
                m_compareWidget->widgetB()->setShowPerformance(m_showPerformance);
            } else {
                m_videoWidget->setShowPerformance(m_showPerformance);
            }
            break;
        }
        if (haveSrc) { m_playing = true; m_playDirection = -1; updatePlaybackState(); startPlayback(); }
        break;
    case Qt::Key_K:
        m_playing = false; m_playDirection = 0; updatePlaybackState(); stopPlayback(); break;
    case Qt::Key_L:
        if (haveSrc) { m_playing = true; m_playDirection = 1; updatePlaybackState(); startPlayback(); }
        break;
    case Qt::Key_E:
        if (haveSrc) { stepAndResume(1); } break;
    case Qt::Key_Period:
        if (haveSrc) { stepAndResume(1); } break;
    case Qt::Key_Comma:
        if (haveSrc) { stepAndResume(-1); } break;
    case Qt::Key_Left:
        if (haveSrc) {
            double seekAmt = shift ? 3.0 : (ctrl ? 60.0 : 10.0);
            seekAndResume(qMax(0.0, m_currentPosSec - seekAmt));
        } break;
    case Qt::Key_Right:
        if (haveSrc) {
            double seekAmt = shift ? 3.0 : (ctrl ? 60.0 : 10.0);
            seekAndResume(qMin(dur, m_currentPosSec + seekAmt));
        } break;
    case Qt::Key_Home:
        if (haveSrc) { seekAndResume(0.0); } break;
    case Qt::Key_End:
        if (haveSrc) { seekAndResume(dur); } break;
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
        if (m_playMode == PlayMode::Compare && shift) {
            // Switch active audio source A↔B
            m_activeCompareAudio = (m_activeCompareAudio == 0) ? 1 : 0;
            if (m_compareWidget) m_compareWidget->setActiveAudio(m_activeCompareAudio);
            break;
        }
        if (!m_altFilePath.isEmpty()) {
            double savedPos = m_currentPosSec;
            m_playing = false; m_updateTimer->stop(); m_decodeTimer->stop();
    if (m_audioOutput) m_audioOutput->close();
    if (m_audioDecoder) m_audioDecoder->close();
    if (m_audioEngine) m_audioEngine->stop();
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
                    if (!m_audioEngine) m_audioEngine = new AudioEngine(this);
                    m_audioEngine->open(m_session.formatContext(), m_session.audioStreamIndex());
                    m_audioEngine->setVolume(m_volumeSlider->value() / 100.0f);
                    MediaSession *sessionPtr = &m_session;
                    m_audioEngine->setPacketSource([sessionPtr](AVPacket **pkt) -> bool {
                        return sessionPtr->popAudioPacket(pkt);
                    });
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
    case Qt::Key_A:
        if (m_playMode == PlayMode::Compare) {
            m_activeCompareAudio = 0;
            if (m_compareWidget) m_compareWidget->setActiveAudio(0);
            break;
        }
        QMainWindow::keyPressEvent(event); break;
    case Qt::Key_B:
        if (m_playMode == PlayMode::Compare) {
            m_activeCompareAudio = 1;
            if (m_compareWidget) m_compareWidget->setActiveAudio(1);
            break;
        }
        QMainWindow::keyPressEvent(event); break;
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

    // Launch pending update on exit
    if (m_updateManager && m_updateManager->hasPendingUpdate()) {
        fprintf(stderr, "[UPDATE] Launching pending update on exit\n");
        fflush(stderr);
        m_updateManager->launchPendingUpdate();
    }

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
        if (path.isEmpty()) return;

        // Check for image file with possible sequence
        QString ext = QFileInfo(path).suffix().toLower();
        static QStringList imageExts = {"exr", "dpx", "png", "tif", "tiff", "jpg", "jpeg", "bmp", "tga", "hdr"};
        if (imageExts.contains(ext)) {
            QString prefix;
            int startNum = 0, padding = 0;
            QString seqExt;
            if (m_seqMode == SeqMode::Ask && detectSequenceCandidate(path, prefix, startNum, padding, seqExt)) {
                promptSequenceChoice(path);
                return;
            }
            loadImageFile(path);
        } else {
            loadFile(path);
        }
    }
}

void MainWindow::showContextMenuAt(const QPoint &globalPos)
{
    showContextMenu(m_videoWidget->mapFromGlobal(globalPos));
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
        beforeModalDialog();
        m_speedMenu->exec(pos);
        afterModalDialog();
        return true;
    }

    return QMainWindow::eventFilter(obj, event);
}

bool MainWindow::event(QEvent *e)
{
    // Window deactivate — skip reposition during focus loss
    if (e->type() == QEvent::WindowDeactivate && m_isFullscreen) {
        m_skipReposition = true;
    }

    // Window activate — reposition overlay
    if (e->type() == QEvent::WindowActivate && m_isFullscreen && m_fsOverlay) {
        m_skipReposition = false;
        positionControls();
        m_fsOverlay->update();
    }

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


// ============================================================
// Update System
// ============================================================

void MainWindow::checkForUpdates()
{
    if (!m_updateManager) return;

    fprintf(stderr, "[UPDATE] User initiated update check\n");
    fflush(stderr);

    beforeModalDialog();
    UpdateDialog dialog(m_updateManager, this);
    dialog.exec();
    afterModalDialog();
}

void MainWindow::showReleaseNotes()
{
    if (!m_updateManager) return;

    auto release = m_updateManager->lastReleaseInfo();
    if (release.has_value() && release->isValid()) {
        beforeModalDialog();
        QString msg = QString("Version: %1\nDate: %2\n\n%3")
                          .arg(release->version, release->releaseDate, release->releaseNotes);
        QMessageBox::information(this, "Release Notes", msg);
        afterModalDialog();
    } else {
        beforeModalDialog();
        QMessageBox::information(this, "Release Notes",
            "No release information available.\nUse Help → Check for Updates to check for new versions.");
        afterModalDialog();
    }
}

void MainWindow::showUpdateSettings()
{
    if (!m_updateManager) return;

    beforeModalDialog();
    QDialog dialog(this);
    dialog.setWindowTitle("Update Settings");
    dialog.setMinimumWidth(350);

    auto *layout = new QVBoxLayout(&dialog);

    auto *autoCheckCB = new QCheckBox("Automatically check for updates");
    autoCheckCB->setChecked(m_updateManager->autoCheckEnabled());
    layout->addWidget(autoCheckCB);

    auto *autoDownloadCB = new QCheckBox("Automatically download updates");
    autoDownloadCB->setChecked(m_updateManager->autoDownloadEnabled());
    layout->addWidget(autoDownloadCB);

    auto *notifyCB = new QCheckBox("Notify before installing");
    notifyCB->setChecked(m_updateManager->notifyBeforeInstall());
    layout->addWidget(notifyCB);

    auto *channelGroup = new QGroupBox("Update Channel");
    auto *channelLayout = new QVBoxLayout(channelGroup);
    auto *stableRadio = new QRadioButton("Stable");
    auto *betaRadio = new QRadioButton("Beta");
    stableRadio->setChecked(m_updateManager->channel() == UpdateManager::UpdateChannel::Stable);
    betaRadio->setChecked(m_updateManager->channel() == UpdateManager::UpdateChannel::Beta);
    channelLayout->addWidget(stableRadio);
    channelLayout->addWidget(betaRadio);
    layout->addWidget(channelGroup);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        m_updateManager->setAutoCheck(autoCheckCB->isChecked());
        m_updateManager->setAutoDownload(autoDownloadCB->isChecked());
        m_updateManager->setNotifyBeforeInstall(notifyCB->isChecked());
        m_updateManager->setChannel(betaRadio->isChecked()
                                        ? UpdateManager::UpdateChannel::Beta
                                        : UpdateManager::UpdateChannel::Stable);

        // Reschedule or cancel startup timer based on new setting
        if (m_updateManager->autoCheckEnabled() && m_startupUpdateTimer) {
            if (!m_startupUpdateTimer->isActive())
                m_startupUpdateTimer->start(5000);
        } else if (m_startupUpdateTimer) {
            m_startupUpdateTimer->stop();
        }

        fprintf(stderr, "[UPDATE] Settings updated: autoCheck=%d autoDownload=%d notify=%d channel=%s\n",
                m_updateManager->autoCheckEnabled(), m_updateManager->autoDownloadEnabled(),
                m_updateManager->notifyBeforeInstall(),
                m_updateManager->channel() == UpdateManager::UpdateChannel::Stable ? "stable" : "beta");
        fflush(stderr);
    }
    afterModalDialog();
}

void MainWindow::onStartupUpdateCheck()
{
    if (!m_updateManager || !m_updateManager->autoCheckEnabled()) return;

    fprintf(stderr, "[UPDATE] Startup background check starting (async)\n");
    fflush(stderr);

    // Connect signals for the background check
    QMetaObject::Connection connAvail, connNone, connFail;
    connAvail = connect(m_updateManager, &UpdateManager::updateAvailable, this,
            [this, connAvail, connNone, connFail](const ReleaseInfo &info) mutable {
                disconnect(connAvail); disconnect(connNone); disconnect(connFail);
                fprintf(stderr, "[UPDATE] Background check: update available %s\n",
                        info.version.toUtf8().constData());
                fflush(stderr);
                beforeModalDialog();
                UpdateDialog dlg(m_updateManager, this);
                dlg.showUpdateAvailable(info);
                dlg.exec();
                afterModalDialog();
            });

    connNone = connect(m_updateManager, &UpdateManager::noUpdateAvailable, this,
            [connAvail, connNone, connFail]() mutable {
                disconnect(connAvail); disconnect(connNone); disconnect(connFail);
                fprintf(stderr, "[UPDATE] Background check: up to date\n");
                fflush(stderr);
            });

    connFail = connect(m_updateManager, &UpdateManager::checkFailed, this,
            [connAvail, connNone, connFail](const QString &err) mutable {
                disconnect(connAvail); disconnect(connNone); disconnect(connFail);
                fprintf(stderr, "[UPDATE] Background check failed: %s\n",
                        err.toUtf8().constData());
                fflush(stderr);
            });

    m_updateManager->checkForUpdateAsync();
}












