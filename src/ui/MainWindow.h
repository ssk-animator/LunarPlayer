#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "SeekSlider.h"
#include "MediaInfo.h"
#include "decoder/DecoderInfo.h"
#include "decoder/HWAccel.h"
#include "decoder/DecoderManager.h"
#include "renderer/ColorManager.h"
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QMenu>
#include <QActionGroup>
#include <mutex>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QContextMenuEvent>
#include <QElapsedTimer>
#include <queue>
#include <atomic>
#include "VideoWidget.h"

// ---- Command types for Action Controller pattern ----
enum class VideoCmd { Zoom, AspectRatio, Crop, Fullscreen, VideoTrack };
enum class AudioCmd { Mute, VolumeChange, AudioTrack, StereoMode };
enum class SubtitleCmd { Enable, LoadFile, Track, Delay };

struct VideoCommand {
    VideoCmd type;
    QVariant value;
};
struct AudioCommand {
    AudioCmd type;
    QVariant value;
};
struct SubtitleCommand {
    SubtitleCmd type;
    QVariant value;
};

class VideoWidget;
class AudioDecoder;
class AudioOutput;
class AudioEngine;
class ThumbnailCache;
class HoverPreviewWidget;
class GradientOverlay;
class FullscreenCommandPanel;
class SequenceFrameCache;
class CompareController;
class SubtitleManager;
class CompareWidget;
class NetworkMediaSession;
class NetworkBuffer;
class StreamingOverlay;
class DecodeThread;
class UpdateManager;
class UpdateDialog;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/mathematics.h>
}

// ---- Frame queue for PTS-based presentation scheduling ----
struct QueuedFrame {
    AVFrame *frame = nullptr;
    double ptsSec = 0.0;
    int64_t frameNum = -1;
};

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
    void setSkipRGBConversion(bool skip) { m_skipRGBConversion = skip; }
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
    size_t audioQueueSize();
    AVFormatContext* formatContext() const { return m_fmtCtx; }

    int audioStreamCount() const { return m_audioStreamCount; }
    int subtitleStreamCount() const { return m_subtitleStreamCount; }
    int videoStreamCount() const { return m_videoStreamCount; }
    DecoderInfo decoderInfo() const { return m_decoderInfo; }
    MediaInfo mediaInfo() const;
    HDRMetadata hdrMetadata() const;
    double lastDecodeMs() const { return m_lastDecodeMs; }
    double lastSwsScaleMs() const { return m_lastSwsScaleMs; }
    const GPUInfo& gpuInfo() const { return m_gpuInfo; }
    QString lastError() const { return m_lastError; }

    // Decode state for EOF flush
    enum class DecodeState { Reading, Flushing, Finished };
    DecodeState decodeState() const { return m_decodeState; }

    // ---- Frame queue API ----
    bool decodeNextFrame();              // decode one frame into queue, return false on EOF/error
    bool hasDisplayableFrame(double ptsSec) const;  // is there a frame with pts <= ptsSec?
    bool popDisplayFrame(QueuedFrame &out, double ptsSec);  // pop frame with pts <= ptsSec, drop late ones
    bool peekDisplayFrame(QueuedFrame &out) const;  // peek front of queue without removing
    int frameQueueSize() const { return static_cast<int>(m_frameQueue.size()); }
    double nextFramePtsSec() const;      // PTS of head frame, or -1 if empty
    void flushFrameQueue();              // clear queue (on seek/close)
    void setFrameQueueMax(int n) { m_frameQueueMax = n; }

    // Current decoded PTS (seconds) — for position tracking
    double lastDecodedPtsSec() const { return m_lastDecodedPtsSec; }

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
    std::mutex m_audioPacketMutex;
    int m_audioStreamCount = 0;
    int m_subtitleStreamCount = 0;
    int m_videoStreamCount = 0;

    AVBufferRef *m_hwDeviceCtx = nullptr;
    AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    DecoderInfo m_decoderInfo;
    GPUInfo m_gpuInfo;

    QElapsedTimer m_decodePerfTimer;
    double m_lastDecodeMs = 0.0;
    double m_lastSwsScaleMs = 0.0;

    QString m_codecName;
    QString m_codecLongName;
    QString m_profileName;
    QString m_pixFmtName;
    QString m_colorSpace;
    QString m_hdrType;
    int64_t m_bitrate = 0;
    QString m_audioCodecName;
    QString m_audioCodecLongName;
    QString m_audioChannelLayout;
    QString m_audioSampleFormat;
    int m_audioBitDepth = 0;
    int64_t m_audioBitrate = 0;
    QString m_audioProfile;
    int m_audioFrameSize = 0;
    int m_audioBlockAlign = 0;
    QString m_rendererName;
    QString m_containerFormat;
    QString m_containerLongName;

    SwsContext *m_convertCtx = nullptr;
    AVFrame *m_convertedFrame = nullptr;
    bool m_isImageSequence = false;
    QString m_lastError;

    // Decode state + counters
    DecodeState m_decodeState = DecodeState::Reading;
    int64_t m_totalFramesDecoded = 0;
    int m_packetsSkipped = 0;
    int m_framesDropped = 0;
    int m_seeksCount = 0;
    double m_seekLatencyMs = 0.0;
    double m_totalDecodeMs = 0.0;
    double m_peakDecodeMs = 0.0;
    QElapsedTimer m_seekTimer;
    bool m_skipRGBConversion = false;

    // ---- Frame queue for PTS-based scheduling ----
    std::deque<QueuedFrame> m_frameQueue;
    int m_frameQueueMax = 4;
    double m_lastDecodedPtsSec = 0.0;

    // Internal: decode one raw packet into m_decodedFrame
    bool decodeOnePacket();
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
    // Playback mode
    enum class PlayMode { Normal, Compare };
    PlayMode playMode() const { return m_playMode; }

    void loadFile(const QString &path);
    void loadImageFile(const QString &path);
    void closeFile();

    // Sequence auto-detection mode for the session
    enum class SeqMode { Ask, Single, Sequence };
    void setSeqMode(SeqMode m) { m_seqMode = m; }
    SeqMode seqMode() const { return m_seqMode; }
    MediaSession* session() { return &m_session; }
    MediaSession* sessionB() { return &m_sessionB; }
    VideoWidget* videoWidget() { return m_videoWidget; }
    double durationSec() const { return m_session.durationSec(); }

    // Format filter helpers — built from FFmpeg's demuxer list (public for testing)
    static QStringList buildMediaFilters();
    static QStringList buildNavFilters();
    static void initMediaFilters();

private slots:
    void openFile();
    void openImage();
    void openImageSequence();
    void openCompare();
    void exitCompareMode();
    void togglePlayPause();
    void toggleFullscreen();
    QString currentFilePath() const { return m_currentFilePath; }
    void seekBySlider(int value);
    void seekBySliderPressed();
    void seekBySliderReleased();
    void updateTimerTick();
    void onZoomChanged(QAction *action);
    void onAspectChanged(QAction *action);
    void onCropChanged(QAction *action);

    // ---- Action Controller: shared business logic ----
    void executeVideoCommand(const VideoCommand &cmd);
    void executeAudioCommand(const AudioCommand &cmd);
    void executeSubtitleCommand(const SubtitleCommand &cmd);
    void loadSubtitleFile();

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
    void runSeekStressTest();
    void runTimelineStressTest();
    void stepAndResume(int direction);
    void showContextMenuAt(const QPoint &globalPos);

    // ---- Update system ----
    void checkForUpdates();
    void showReleaseNotes();
    void showUpdateSettings();
    void onStartupUpdateCheck();

    // ---- Helper builders (shared by menu bar + context menu) ----
    void addZoomAction(QMenu *menu, const char *label, double factor, bool checked, QActionGroup *group);
    void addAspectAction(QMenu *menu, const char *label, VideoWidget::AspectMode mode, bool checked, QActionGroup *group);
    void addCropAction(QMenu *menu, const char *label, VideoWidget::CropMode mode, bool checked, QActionGroup *group);
    void buildZoomSubmenu(QMenu *parent, QActionGroup *group);
    void buildAspectSubmenu(QMenu *parent, QActionGroup *group);
    void buildCropSubmenu(QMenu *parent, QActionGroup *group);
    void buildAudioTrackSubmenu(QMenu *parent, QActionGroup *group);
    void buildStereoModeSubmenu(QMenu *parent, QActionGroup *group);
    void buildSubtitleTrackSubmenu(QMenu *parent, QActionGroup *group);

    // ---- Targeted UI sync (only updates what changed) ----
    void syncZoomCheckmarks();
    void syncAspectCheckmarks();
    void syncCropCheckmarks();
    void syncAudioTrackCheckmarks();
    void syncMuteState();
    void syncSubtitlesEnabled();

    // Temporarily drop/reacquire HWND_TOPMOST so that modal dialogs
    // appear *above* the fullscreen borderless window instead of behind it.
    void beforeModalDialog();
    void afterModalDialog();

    FullscreenCommandPanel *m_fullscreenPanel  = nullptr;
    QMenu                 *m_contextMenuContent = nullptr;

    void addMarker();
    void removeNearestMarker();
    void seekNextMarker();
    void seekPrevMarker();
    void syncMarkersToSlider();

    // Compare mode
    void enterCompareMode();
    void compareApplyBothFrames();

    // Prompt user when a numbered image file might be a sequence
    void promptSequenceChoice(const QString &firstFile);
    bool detectSequenceCandidate(const QString &path, QString &prefix,
                                  int &startNum, int &padding, QString &ext);

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
    GradientOverlay *m_fsOverlay = nullptr;
    QPushButton *m_fsPlayBtn = nullptr;
    SeekSlider *m_fsSeekBar = nullptr;
    QPushButton *m_fsFullscreenBtn = nullptr;

    // Overlay animation
    QGraphicsOpacityEffect *m_overlayOpacity = nullptr;
    QPropertyAnimation *m_overlayAnim = nullptr;

    MediaSession m_session;
    MediaSession m_sessionB;     // second session for compare mode
    DecodeThread *m_decodeThread = nullptr;
    bool m_usingDecodeThread = false;
    AudioDecoder *m_audioDecoder = nullptr;
    AudioOutput *m_audioOutput = nullptr;
    AudioEngine *m_audioEngine = nullptr;
    ThumbnailCache *m_thumbnailCache = nullptr;
    SequenceFrameCache *m_sequenceCache = nullptr;
    SeqMode m_seqMode = SeqMode::Ask;
    CompareController *m_compareCtrl = nullptr;
    CompareWidget *m_compareWidget = nullptr;
    PlayMode m_playMode = PlayMode::Normal;
    int m_activeCompareAudio = 0; // 0=A, 1=B
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

    // ---- Timeline reliability: atomic seek guard + coalesced drag ----
    std::atomic<bool> m_seekInProgress{false};
    std::atomic<int>  m_seekSeq{0};
    int  m_latestSliderValue = 0;
    QTimer *m_dragCoalesceTimer = nullptr;
    static constexpr int DRAG_COALESCE_MS = 16;  // ~60Hz seek during drag

    int m_playDirection = 1;
    double m_speed = 1.0;
    int m_loopIn = -1;
    int m_loopOut = -1;

    // ---- PTS-driven playback clock ----
    QElapsedTimer m_playbackClock;
    double m_playbackBasePtsSec = 0.0;   // PTS (seconds) at playback start/seek
    double m_playbackClockBase = 0.0;    // wall clock (seconds) at playback start/seek
    double playbackNowSec() const;       // current playback position from wall clock
    void syncPlaybackClock(double ptsSec); // hard reset clock to given PTS (seek/start only)
    void adjustPlaybackClock(double ptsSec); // smooth nudge toward frame PTS (max 1 frame/step)

    // Fullscreen
    bool m_isFullscreen = false;
    int  m_dialogDepth  = 0;   // >0 when a modal dialog is showing
    bool m_showPerformance = false;
    bool m_controlsVisible = false;
    bool m_skipReposition = false;
    QTimer *m_fullscreenHideTimer = nullptr;
    QByteArray m_savedWindowGeometry;
    long m_savedWindowStyle = 0;
    long m_savedWindowExStyle = 0;

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

    // Subtitle management
    std::unique_ptr<SubtitleManager> m_subtitleManager;

    // Streaming support
    std::unique_ptr<NetworkMediaSession> m_networkSession;
    StreamingOverlay *m_streamingOverlay = nullptr;
    QAction *m_toggleStreamingOverlayAction = nullptr;
    bool m_isNetworkStream = false;

    // Menus
    QMenu *m_audioMenu = nullptr;
    QActionGroup *m_audioTrackGroup = nullptr;
    QActionGroup *m_stereoModeGroup = nullptr;
    QAction *m_muteAction = nullptr;

    QMenu *m_videoMenu = nullptr;
    QActionGroup *m_zoomGroup = nullptr;
    QActionGroup *m_aspectGroup = nullptr;
    QActionGroup *m_cropGroup = nullptr;
    QAction *m_decoderInfoAction = nullptr;
    QAction *m_rendererInfoAction = nullptr;

    void loadSettings();
    void saveSettings();

    QMenu *m_subtitleMenu = nullptr;
    QAction *m_enableSubtitlesAction = nullptr;

    // Pipeline profiling
    QElapsedTimer m_pipelineTimer;

    // ---- Update system ----
    UpdateManager *m_updateManager = nullptr;
    QTimer *m_startupUpdateTimer = nullptr;
    int m_profFrameCount = 0;
    double m_profTotalMs = 0.0;
    double m_profPeakTotalMs = 0.0;
    double m_profReadMs = 0.0;
    double m_profDecodeMs = 0.0;
    double m_profConvertMs = 0.0;
    double m_profUploadMs = 0.0;
    double m_profPaintMs = 0.0;
    double m_profPeakReadMs = 0.0;
    double m_profPeakDecodeMs = 0.0;
    double m_profPeakConvertMs = 0.0;
    double m_profPeakUploadMs = 0.0;
    double m_profPeakPaintMs = 0.0;
    int m_profCount = 0;
};

#endif
