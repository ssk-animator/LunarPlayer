#pragma once
#include <QObject>
#include <QString>
#include <QImage>
#include <QVector>
#include <QElapsedTimer>
#include <queue>
#include <atomic>
#include "NetworkBuffer.h"
#include "renderer/ColorManager.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVCodecParameters;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

class NetworkMediaSession : public QObject
{
    Q_OBJECT
public:
    explicit NetworkMediaSession(QObject *parent = nullptr);
    ~NetworkMediaSession();

    // --- Open / Close ---
    bool openStream(const QString &url);
    void close();

    // --- Frame reading ---
    bool readFrame();
    QImage currentFrame() const { return m_frame; }

    // --- Audio ---
    bool hasAudio() const { return m_audioStreamIdx >= 0; }
    int audioSampleRate() const { return m_audioSampleRate; }
    int audioChannels() const { return m_audioChannels; }
    bool popAudioSamples(QVector<float> &samples);
    double audioClock() const;

    // --- State ---
    bool isOpen() const { return m_fmtCtx != nullptr; }
    QString url() const { return m_url; }
    double durationSec() const { return m_durationSec; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double fps() const { return m_fps; }
    double currentPtsSec() const;
    double lastDecodeMs() const { return m_lastDecodeMs; }
    QString lastError() const { return m_lastError; }

    // --- Seeking ---
    bool seekSec(double sec);
    bool isLiveStream() const { return m_isLive; }

    // --- Buffer ---
    NetworkBuffer* buffer() { return &m_buffer; }

    // --- HDR Metadata ---
    HDRMetadata hdrMetadata() const;

    // --- Protocol detection ---
    static bool isNetworkUrl(const QString &url);
    static QString protocolName(const QString &url);

signals:
    void streamOpened(bool success);
    void streamError(const QString &message);
    void bufferingUpdated(double progress);
    void reconnecting();

private:
    bool openCodec(int streamIndex);
    bool openAudioDecoder(int streamIndex);
    void processDecodedFrame(AVFrame *frame);
    void processAudioFrame(AVFrame *frame);
    void flushAudioDecoder();

    QString m_url;
    AVFormatContext *m_fmtCtx = nullptr;

    // Video
    AVCodecContext *m_codecCtx = nullptr;
    int m_videoStreamIdx = -1;
    QImage m_frame;
    SwsContext *m_swsCtx = nullptr;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_decodedFrame = nullptr;

    // Audio
    AVCodecContext *m_audioCodecCtx = nullptr;
    int m_audioStreamIdx = -1;
    int m_audioSampleRate = 0;
    int m_audioChannels = 0;
    SwrContext *m_swrCtx = nullptr;
    double m_audioClock = 0.0;
    QVector<float> m_audioSampleBuffer;

    double m_durationSec = 0.0;
    int m_width = 0, m_height = 0;
    double m_fps = 24.0;
    double m_lastDecodeMs = 0.0;
    QString m_lastError;
    bool m_isLive = false;
    std::atomic<bool> m_interrupt{false};

    NetworkBuffer m_buffer;

    QElapsedTimer m_decodeTimer;
};
