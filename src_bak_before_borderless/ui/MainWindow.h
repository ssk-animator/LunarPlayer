#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "SeekSlider.h"
#include "MediaInfo.h"
#include "decoder/DecoderInfo.h"
#include "decoder/HWAccel.h"
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMenu>
#include <QActionGroup>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <queue>
#include <atomic>

class VideoWidget;
class AudioDecoder;
class AudioOutput;
class ThumbnailCache;
class HoverPreviewWidget;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

struct SequenceInfo {
    bool isValid = false;
    QString baseName;
    QString pattern;
    int startNumber = 1;
    int frameCount = 0;
    int padding = 0;
};

class MediaSession {
public:
    MediaSession() = default;
    ~MediaSession();

    bool open(const QString &path);
    bool openImageSequence(const QString &pattern, int startNum, int frameCount, int frameRate);
    void close();
    bool readFrame();
    QImage currentFrame() const { return m_frame; }
    AVFrame* decodedFrame() const { return m_decodedFrame; }
    AVFrame* displayFrame() const;

    bool isImageSequence() const { return m_isImageSequence; }
    double durationSec() const { return m_durationSec; }
    void seekSec(double sec);
    int fps() const { return m_fps; }
    bool isOpen() const { return m_fmtCtx != nullptr; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    int64_t currentPts() const;
    int currentFrameNumber() const;
    int frameCount() const;
    bool seekToFrame(int frameNum);
    bool stepForward();
    bool stepBackward();

    int videoStreamIndex() const { return m_videoStreamIdx; }
    int audioStreamIndex() const { return m_audioStreamIdx; }
    int audioSampleRate() const { return m_audioSampleRate; }
    int audioChannels() const { return m_audioChannels; }
    bool hasAudio() const { return m_audioStreamIdx >= 0; }
    bool popAudioPacket(AVPacket **pkt);
    void flushAudioQueue();
    AVFormatContext* formatContext() const { return m_fmtCtx; }

    int audioStreamCount() const { return m_audioStreamCount; }
    int subtitleStreamCount() const { return m_subtitleStreamCount; }
    int videoStreamCount() const { return m_videoStreamCount; }
    DecoderInfo decoderInfo() const { return m_decoderInfo; }
    MediaInfo mediaInfo() const;
    double lastDecodeMs() const { return m_lastDecodeMs; }

private:
    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    const AVCodec *m_codec = nullptr;
    SwsContext *m_swsCtx = nullptr;
    int m_videoStreamIdx = -1;
    int m_audioStreamIdx = -1;
    int m_audioSampleRate = 0;
    int m_audioChannels = 0;
    int m_width = 0, m_height = 0;
    double m_durationSec = 0.0;
    int m_fps = 24;
    int m_totalFrames = 0;
    QImage m_frame;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_decodedFrame = nullptr;
    std::queue<AVPacket*> m_audioPacketQueue;
    int m_audioStreamCount = 0;
    int m_subtitleStreamCount = 0;
    int m_videoStreamCount = 0;

    AVBufferRef *m_hwDeviceCtx = nullptr;
    AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    DecoderInfo m_decoderInfo;

    QElapsedTimer m_decodePerfTimer;
    double m_lastDecodeMs = 0.0;

    QString m_codecName;
    QString m_codecLongName;
    QString m_profileName;
    QString m_pixFmtName;
    QString m_colorSpace;
    QString m_hdrType;
    int64_t m_bitrate = 0;
    QString m_audioCodecName;
    QString m_rendererName;

    SwsContext *m_convertCtx = nullptr;
    AVFrame *m_convertedFrame = nullptr;
    bool m_isImageSequence = false;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    bool event(QEvent *e) override;

public:
    void loadFile(const QString &path);
    void closeFile();
    MediaSession* session() { return &m_session; }
    VideoWidget* videoWidget() { return m_videoWidget; }
    double durationSec() const { return m_session.durationSec(); }

private slots:
    void openFile();
    void openImageSequence();
    void togglePlayPause();
    void toggleFullscreen();
    QString currentFilePath() const { return m_currentFilePath; }
    void seekBySlider(int value);
    void seekBySliderPressed();
    void seekBySliderReleased();
    void updateTimerTick();

private:
    void setupUI();
    void setupMenus();
    void updatePlaybackState();
    void updateTitle();
    void updateInfoLabel();
    void startPlayback();
    void stopPlayback();
    void buildAudioMenu();
    void buildVideoMenu();
    void buildSubtitleMenu();
    void showContextMenu(const QPoint &pos);
    void positionControls();
    void fadeOverlayIn();
    void fadeOverlayOut();
    void updateOverlayMode();
    void navigateFile(int direction);
    void updatePerformanceOverlay();
    void updateStatusBarForSequence();
    void syncSeekUI();
    void seekAndResume(double newPos);
    void stepAndResume(int direction);
    void showContextMenuAt(const QPoint &globalPos);

    void addMarker();
    void removeNearestMarker();
    void seekNextMarker();
    void seekPrevMarker();
    void syncMarkersToSlider();

    QString buttonStyle() const;
    QString sliderStyle() const;
    QWidget* createSeparator() const;

public:
    void applyCurrentFrame();

    VideoWidget *m_videoWidget = nullptr;

    // Windowed mode overlay
    QWidget *m_controlsOverlay = nullptr;
    SeekSlider *m_seekSlider = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_prevFrameBtn = nullptr;
    QPushButton *m_nextFrameBtn = nullptr;
    QPushButton *m_skipBackBtn = nullptr;
    QPushButton *m_skipForwardBtn = nullptr;
    QPushButton *m_volumeBtn = nullptr;
    QPushButton *m_fullscreenBtn = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_frameLabel = nullptr;
    QLabel *m_speedLabel = nullptr;
    QMenu *m_speedMenu = nullptr;
    QSlider *m_volumeSlider = nullptr;

    // Fullscreen overlay
    QWidget *m_fsOverlay = nullptr;
    QPushButton *m_fsPlayBtn = nullptr;
    SeekSlider *m_fsSeekBar = nullptr;
    QPushButton *m_fsFullscreenBtn = nullptr;

    // Overlay animation
    QGraphicsOpacityEffect *m_overlayOpacity = nullptr;
    QPropertyAnimation *m_overlayAnim = nullptr;

    MediaSession m_session;
    AudioDecoder *m_audioDecoder = nullptr;
    AudioOutput *m_audioOutput = nullptr;
    ThumbnailCache *m_thumbnailCache = nullptr;
    HoverPreviewWidget *m_hoverPreview = nullptr;
    QTimer *m_updateTimer = nullptr;
    QTimer *m_decodeTimer = nullptr;
    QString m_currentFilePath;
    QString m_altFilePath;
    QString m_seqBaseName;
    double m_savedAltPos = 0.0;

    std::atomic<bool> m_playing{false};
    bool m_isScrubbing = false;
    bool m_wasPlayingBeforeScrub = false;
    QElapsedTimer m_scrubTimer;
    double m_currentPosSec = 0.0;
    int m_currentFrameNum = 0;

    int m_playDirection = 1;
    double m_speed = 1.0;
    int m_loopIn = -1;
    int m_loopOut = -1;

    // Fullscreen
    bool m_isFullscreen = false;
    bool m_showPerformance = false;
    bool m_controlsVisible = false;
    QTimer *m_fullscreenHideTimer = nullptr;

    // Frame markers
    QVector<double> m_markers;

    QLabel *m_statusLabel = nullptr;

    // Hover preview throttle
    QTimer *m_hoverThrottleTimer = nullptr;
    double m_hoverTargetTime = 0.0;
    QString m_hoverTargetTimestamp;
    bool m_hoverPending = false;
    bool m_hoverActive = false;
    int m_lastHoverX = -999999;
    // Hover handler timing for performance overlay
    QElapsedTimer m_hoverTimer;
    double m_lastHoverHandlerMs = 0.0;
    double m_peakHoverHandlerMs = 0.0;

    // Menus
    QMenu *m_audioMenu = nullptr;
    QActionGroup *m_audioTrackGroup = nullptr;
    QActionGroup *m_stereoModeGroup = nullptr;
    QAction *m_muteAction = nullptr;

    QMenu *m_videoMenu = nullptr;
    QActionGroup *m_zoomGroup = nullptr;
    QActionGroup *m_aspectGroup = nullptr;
    QAction *m_decoderInfoAction = nullptr;
    QAction *m_rendererInfoAction = nullptr;

    QMenu *m_subtitleMenu = nullptr;
    QAction *m_enableSubtitlesAction = nullptr;
};

#endif
