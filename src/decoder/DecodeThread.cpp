#include "DecodeThread.h"
#include <QCoreApplication>
#include <cstdio>

DecodeThread::DecodeThread()
{
}

DecodeThread::~DecodeThread()
{
    stopPlayback();
    close();
}

bool DecodeThread::open(const QString &path)
{
    close();

    QByteArray pathBytes = path.toUtf8();
    if (avformat_open_input(&m_fmtCtx, pathBytes.constData(), nullptr, nullptr) < 0) {
        fprintf(stderr, "[DECODE-THD] Failed to open: %s\n", pathBytes.constData());
        return false;
    }
    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        fprintf(stderr, "[DECODE-THD] Failed to find stream info\n");
        return false;
    }

    const AVCodec *videoCodec = nullptr;
    m_videoStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
    if (m_videoStreamIdx < 0) {
        fprintf(stderr, "[DECODE-THD] No video stream found\n");
        return false;
    }

    AVStream *st = m_fmtCtx->streams[m_videoStreamIdx];
    m_fps = st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0
            ? av_q2d(st->avg_frame_rate) : 24;
    if (m_fps <= 0) m_fps = 24;

    // HW decoder (hevc_cuvid/CUDA) disabled: cuvid uses the GPU for decode while
    // OpenGL uses the same GPU for texture upload → driver deadlock in glTexImage2D.
    // Requires CUDA-GL interop (cuGraphicsGLRegisterBuffer) to fix. Using software for now.
    AVBufferRef *hwDeviceCtx = nullptr;
    bool usingHW = false;

    if (!usingHW) {
        // Software fallback
        if (hwDeviceCtx) av_buffer_unref(&hwDeviceCtx);
        m_codecCtx = avcodec_alloc_context3(videoCodec);
        if (!m_codecCtx) {
            fprintf(stderr, "[DECODE-THD] Failed to allocate codec context\n");
            return false;
        }
        avcodec_parameters_to_context(m_codecCtx, st->codecpar);
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "threads", "4", 0);
        if (avcodec_open2(m_codecCtx, m_codecCtx->codec, &opts) < 0) {
            fprintf(stderr, "[DECODE-THD] Failed to open codec\n");
            av_dict_free(&opts);
            return false;
        }
        av_dict_free(&opts);
        fprintf(stderr, "[DECODE-THD] Using SW decoder: %s\n", videoCodec->name);
    }

    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;
    m_pixFmt = m_codecCtx->pix_fmt;
    m_audioStreamIdx = av_find_best_stream(m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    m_pkt = av_packet_alloc();
    m_decodedFrame = av_frame_alloc();
    m_hwFrame = av_frame_alloc();
    if (usingHW && m_codecCtx->hw_frames_ctx) {
        m_hwFrame->hw_frames_ctx = av_buffer_ref(m_codecCtx->hw_frames_ctx);
    }
    m_hwDeviceCtx = hwDeviceCtx;

    fprintf(stderr, "[DECODE-THD] Opened: %dx%d @ %d fps, pixfmt=%d hw=%d\n",
            m_width, m_height, m_fps, static_cast<int>(m_pixFmt), usingHW);
    return true;
}

void DecodeThread::close()
{
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    if (m_pkt) { av_packet_free(&m_pkt); }
    if (m_decodedFrame) { av_frame_free(&m_decodedFrame); }
    if (m_hwFrame) { av_frame_free(&m_hwFrame); }
    if (m_hwDeviceCtx) { av_buffer_unref(&m_hwDeviceCtx); }

    QMutexLocker lock(&m_mutex);
    while (!m_queue.empty()) {
        av_frame_free(&m_queue.front().frame);
        m_queue.pop_front();
    }
    while (!m_audioQueue.empty()) {
        av_packet_free(&m_audioQueue.front());
        m_audioQueue.pop();
    }

    m_videoStreamIdx = -1;
    m_audioStreamIdx = -1;
    m_width = m_height = 0;
    m_fps = 24;
    m_finished = false;
    m_lastDecodedPtsSec = 0.0;
}

void DecodeThread::startPlayback(int fps, double speed)
{
    Q_UNUSED(speed);
    if (m_running) return;
    m_fps = fps > 0 ? fps : 24;
    m_finished = false;
    m_stopRequested = false;
    m_running = true;
    m_thread = new std::thread(&DecodeThread::decodeLoop, this);
}

void DecodeThread::stopPlayback()
{
    m_stopRequested = true;
    m_queueNotFull.wakeAll();
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    delete m_thread;
    m_thread = nullptr;
    m_running = false;
    m_stopRequested = false;
}

void DecodeThread::seek(double ptsSec)
{
    m_seekPts = ptsSec;
    m_seekRequested = true;
    m_finished = false;
    m_queueNotFull.wakeAll();
}

int DecodeThread::queueSize() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_queue.size());
}

bool DecodeThread::hasFrame() const
{
    QMutexLocker lock(&m_mutex);
    return !m_queue.empty();
}

bool DecodeThread::popFrame(DecodeFrame &out)
{
    QMutexLocker lock(&m_mutex);
    if (m_queue.empty()) return false;
    out = m_queue.front();
    m_queue.pop_front();
    m_queueNotFull.wakeOne();
    return true;
}

bool DecodeThread::peekFrame(DecodeFrame &out) const
{
    QMutexLocker lock(&m_mutex);
    if (m_queue.empty()) return false;
    out = m_queue.front();
    return true;
}

void DecodeThread::dropFrontFrame()
{
    QMutexLocker lock(&m_mutex);
    if (m_queue.empty()) return;
    av_frame_free(&m_queue.front().frame);
    m_queue.pop_front();
    m_queueNotFull.wakeOne();
}

bool DecodeThread::decodeOnePacket()
{
    if (!m_fmtCtx || !m_codecCtx) return false;

    while (av_read_frame(m_fmtCtx, m_pkt) >= 0) {
        if (m_pkt->stream_index == m_videoStreamIdx) {
            m_demuxTimer.start();
            int sendRet = avcodec_send_packet(m_codecCtx, m_pkt);
            m_lastDemuxMs = m_demuxTimer.nsecsElapsed() / 1000000.0;
            av_packet_unref(m_pkt);

            if (sendRet == AVERROR(EAGAIN)) {
                int drainRet = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
                if (drainRet == 0) {
                    if (m_decodedFrame->format == AV_PIX_FMT_CUDA && m_hwFrame) {
                        int64_t savedPts = m_decodedFrame->pts;
                        int64_t savedBest = m_decodedFrame->best_effort_timestamp;
                        AVRational savedTb = m_decodedFrame->time_base;
                        av_frame_unref(m_hwFrame);
                        int hwRet = av_hwframe_transfer_data(m_hwFrame, m_decodedFrame, 0);
                        if (hwRet == 0) {
                            m_hwFrame->pts = savedPts;
                            m_hwFrame->best_effort_timestamp = savedBest;
                            m_hwFrame->time_base = savedTb;
                            AVFrame *tmp = m_decodedFrame;
                            m_decodedFrame = m_hwFrame;
                            m_hwFrame = tmp;
                        } else {
                            char errBuf[128];
                            av_strerror(hwRet, errBuf, sizeof(errBuf));
                            fprintf(stderr, "[DECODE-THD] HW transfer FAILED (EAGAIN drain): %s\n", errBuf);
                        }
                    }
                    return true;
                }
                continue;
            }
            if (sendRet < 0) {
                char errBuf[128];
                av_strerror(sendRet, errBuf, sizeof(errBuf));
                fprintf(stderr, "[DECODE-THD] send_packet error: %s\n", errBuf);
                continue;
            }

            m_decodeTimer.start();
            int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
            m_lastDecodeMs = m_decodeTimer.nsecsElapsed() / 1000000.0;
            if (ret == 0) {
                if (m_decodedFrame->format == AV_PIX_FMT_CUDA && m_hwFrame) {
                    int64_t savedPts = m_decodedFrame->pts;
                    int64_t savedBest = m_decodedFrame->best_effort_timestamp;
                    AVRational savedTb = m_decodedFrame->time_base;
                    av_frame_unref(m_hwFrame);
                    int hwRet = av_hwframe_transfer_data(m_hwFrame, m_decodedFrame, 0);
                    if (hwRet == 0) {
                        m_hwFrame->pts = savedPts;
                        m_hwFrame->best_effort_timestamp = savedBest;
                        m_hwFrame->time_base = savedTb;
                        AVFrame *tmp = m_decodedFrame;
                        m_decodedFrame = m_hwFrame;
                        m_hwFrame = tmp;
                    } else {
                        char errBuf[128];
                        av_strerror(hwRet, errBuf, sizeof(errBuf));
                        fprintf(stderr, "[DECODE-THD] HW transfer FAILED: %s (fmt_in=%d fmt_out=%d)\n",
                                errBuf, m_decodedFrame->format,
                                m_hwFrame->format);
                    }
                }
                return true;
            }
            if (ret == AVERROR(EAGAIN)) continue;
            if (ret == AVERROR_EOF) {
                avcodec_send_packet(m_codecCtx, nullptr);
                continue;
            }
            char errBuf[128];
            av_strerror(ret, errBuf, sizeof(errBuf));
            fprintf(stderr, "[DECODE-THD] receive_frame error: %s\n", errBuf);
            continue;
        } else if (m_audioStreamIdx >= 0 && m_pkt->stream_index == m_audioStreamIdx) {
            AVPacket *copy = av_packet_alloc();
            av_packet_ref(copy, m_pkt);
            QMutexLocker lock(&m_mutex);
            m_audioQueue.push(copy);
            av_packet_unref(m_pkt);
        } else {
            av_packet_unref(m_pkt);
        }
    }

    avcodec_send_packet(m_codecCtx, nullptr);
    int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
    if (ret == 0) {
        if (m_decodedFrame->format == AV_PIX_FMT_CUDA && m_hwFrame) {
            int64_t savedPts = m_decodedFrame->pts;
            int64_t savedBest = m_decodedFrame->best_effort_timestamp;
            AVRational savedTb = m_decodedFrame->time_base;
            av_frame_unref(m_hwFrame);
            int hwRet = av_hwframe_transfer_data(m_hwFrame, m_decodedFrame, 0);
            if (hwRet == 0) {
                m_hwFrame->pts = savedPts;
                m_hwFrame->best_effort_timestamp = savedBest;
                m_hwFrame->time_base = savedTb;
                AVFrame *tmp = m_decodedFrame;
                m_decodedFrame = m_hwFrame;
                m_hwFrame = tmp;
            } else {
                char errBuf[128];
                av_strerror(hwRet, errBuf, sizeof(errBuf));
                fprintf(stderr, "[DECODE-THD] HW transfer FAILED (drain): %s\n", errBuf);
            }
        }
        return true;
    }
    return false;
}

void DecodeThread::decodeLoop()
{
    if (!m_fmtCtx || !m_codecCtx) {
        m_running = false;
        return;
    }

    fprintf(stderr, "[DECODE-THD] decode loop started (pixfmt=%d hw=%d)\n",
            static_cast<int>(m_pixFmt), m_hwDeviceCtx ? 1 : 0);

    int64_t decodedFrames = 0;

    while (!m_stopRequested) {
        if (m_seekRequested) {
            m_seekRequested = false;
            double targetPts = m_seekPts.load();

            {
                QMutexLocker lock(&m_mutex);
                while (!m_queue.empty()) {
                    av_frame_free(&m_queue.front().frame);
                    m_queue.pop_front();
                }
            }

            int64_t ts = static_cast<int64_t>(targetPts * AV_TIME_BASE);
            if (ts < 0) ts = 0;
            av_seek_frame(m_fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(m_codecCtx);
            m_finished = false;

            fprintf(stderr, "[DECODE-THD] seeked to %.3f\n", targetPts);
        }

        {
            QMutexLocker lock(&m_mutex);
            while (static_cast<int>(m_queue.size()) >= m_queueMax && !m_stopRequested && !m_seekRequested) {
                m_queueNotFull.wait(&m_mutex);
            }
        }
        if (m_stopRequested) break;
        if (m_seekRequested) continue;

        if (!decodeOnePacket()) {
            m_finished = true;
            fprintf(stderr, "[DECODE-THD] EOF reached, decoded %lld frames\n", (long long)decodedFrames);

            QMutexLocker lock(&m_mutex);
            while (!m_stopRequested && !m_seekRequested) {
                m_queueNotFull.wait(&m_mutex);
            }
            continue;
        }

        double ptsSec = 0.0;
        if (m_decodedFrame->pts != AV_NOPTS_VALUE) {
            AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
            ptsSec = static_cast<double>(m_decodedFrame->pts) * av_q2d(tb);
        } else if (m_decodedFrame->best_effort_timestamp != AV_NOPTS_VALUE) {
            AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
            ptsSec = static_cast<double>(m_decodedFrame->best_effort_timestamp) * av_q2d(tb);
        }

        int64_t frameNum = qRound(ptsSec * m_fps);
        m_lastDecodedPtsSec = ptsSec;

        DecodeFrame df;
        df.frame = av_frame_alloc();
        av_frame_ref(df.frame, m_decodedFrame);
        df.ptsSec = ptsSec;
        df.frameNum = frameNum;

        {
            QMutexLocker lock(&m_mutex);
            m_queue.push_back(df);
            if (decodedFrames < 10 || decodedFrames % 100 == 0) {
                fprintf(stderr, "[DECODE-THD] F%lld q=%zu pts=%.3f fmt=%d demux=%.1fms dec=%.1fms\n",
                        (long long)decodedFrames, m_queue.size(), ptsSec,
                        m_decodedFrame->format, m_lastDemuxMs, m_lastDecodeMs);
            }
            m_frameAvailable.wakeOne();
        }
        decodedFrames++;
    }

    fprintf(stderr, "[DECODE-THD] decode loop stopped after %lld frames\n", (long long)decodedFrames);
    m_running = false;
}
