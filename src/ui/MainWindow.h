#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <atomic>

class VideoWidget;
class QComboBox;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class MediaSession {
public:
    MediaSession() = default;
    ~MediaSession();

    bool open(const QString &path);
    void close();
    bool readFrame();
    QImage currentFrame() const { return m_frame; }
    double durationSec() const { return m_durationSec; }
    void seekSec(double sec);
    int fps() const { return m_fps; }
    bool isOpen() const { return m_fmtCtx != nullptr; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    const AVCodec *m_codec = nullptr;
    SwsContext *m_swsCtx = nullptr;
    int m_videoStreamIdx = -1;
    int m_width = 0, m_height = 0;
    double m_durationSec = 0.0;
    int m_fps = 24;
    QImage m_frame;
    AVPacket *m_pkt = nullptr;
    AVFrame *m_decodedFrame = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

public:
    void loadFile(const QString &path);
    void closeFile();
    MediaSession* session() { return &m_session; }
    VideoWidget* videoWidget() { return m_videoWidget; }
    double durationSec() const { return m_session.durationSec(); }

private slots:
    void openFile();
    void togglePlayPause();
    QString currentFilePath() const { return m_currentFilePath; }
    void seekBySlider(int value);
    void seekBySliderPressed();
    void seekBySliderReleased();
    void updateTimerTick();

private:
    void setupUI();
    void updatePlaybackState();
    void updateTitle();

    VideoWidget *m_videoWidget;
    QPushButton *m_playBtn;
    QSlider *m_seekSlider;
    QSlider *m_volumeSlider;
    QLabel *m_timeLabel;

    MediaSession m_session;
    QTimer *m_updateTimer;
    QTimer *m_decodeTimer;
    QString m_currentFilePath;

    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_seeking{false};
    std::atomic<bool> m_pendingSeek{false};
    double m_pendingSeekPos = 0.0;
    double m_currentPosSec = 0.0;
};

#endif
