#include "MainWindow.h"
#include "VideoWidget.h"
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
#include <cmath>

MediaSession::~MediaSession()
{
    close();
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
    if (!m_codecCtx) {
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    avcodec_parameters_to_context(m_codecCtx, m_fmtCtx->streams[m_videoStreamIdx]->codecpar);

    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_width = m_codecCtx->width;
    m_height = m_codecCtx->height;

    double dur = static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE;
    if (dur <= 0) {
        AVRational tb = m_fmtCtx->streams[m_videoStreamIdx]->time_base;
        dur = static_cast<double>(m_fmtCtx->streams[m_videoStreamIdx]->duration) * tb.num / tb.den;
    }
    m_durationSec = dur;

    AVRational avg_framerate = m_fmtCtx->streams[m_videoStreamIdx]->avg_frame_rate;
    if (avg_framerate.den > 0)
        m_fps = qRound(static_cast<double>(avg_framerate.num) / avg_framerate.den);
    else
        m_fps = 24;

    m_swsCtx = sws_getContext(m_width, m_height, m_codecCtx->pix_fmt,
                               m_width, m_height, AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);

    m_pkt = av_packet_alloc();
    m_decodedFrame = av_frame_alloc();

    return true;
}

void MediaSession::close()
{
    av_frame_free(&m_decodedFrame);
    av_packet_free(&m_pkt);
    sws_freeContext(m_swsCtx);
    m_swsCtx = nullptr;
    avcodec_free_context(&m_codecCtx);
    avformat_close_input(&m_fmtCtx);
    m_videoStreamIdx = -1;
    m_width = m_height = 0;
    m_durationSec = 0.0;
    m_fps = 24;
    m_frame = QImage();
}

bool MediaSession::readFrame()
{
    if (!m_fmtCtx)
        return false;

    while (av_read_frame(m_fmtCtx, m_pkt) >= 0) {
        if (m_pkt->stream_index == m_videoStreamIdx) {
            if (avcodec_send_packet(m_codecCtx, m_pkt) == 0) {
                int ret = avcodec_receive_frame(m_codecCtx, m_decodedFrame);
                if (ret == 0) {
                    QImage img(m_width, m_height, QImage::Format_RGB888);
                    uint8_t *dstData[1] = { img.bits() };
                    int dstLinesize[1] = { static_cast<int>(img.bytesPerLine()) };
                    sws_scale(m_swsCtx,
                              m_decodedFrame->data, m_decodedFrame->linesize,
                              0, m_height,
                              dstData, dstLinesize);
                    m_frame = img.copy();
                    av_packet_unref(m_pkt);
                    return true;
                }
            }
        }
        av_packet_unref(m_pkt);
    }

    return false;
}

void MediaSession::seekSec(double sec)
{
    if (!m_fmtCtx || m_videoStreamIdx < 0)
        return;

    int64_t ts = static_cast<int64_t>(sec * AV_TIME_BASE);
    if (ts < 0) ts = 0;

    av_seek_frame(m_fmtCtx, -1, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_codecCtx);
}

// ----- MainWindow -----

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setAcceptDrops(true);
    setMinimumSize(640, 480);
    resize(1280, 720);
    setupUI();
    updatePlaybackState();
    updateTitle();

    m_updateTimer = new QTimer(this);
    m_updateTimer->setTimerType(Qt::PreciseTimer);
    connect(m_updateTimer, &QTimer::timeout, this, &MainWindow::updateTimerTick);

    m_decodeTimer = new QTimer(this);
    m_decodeTimer->setTimerType(Qt::PreciseTimer);
    connect(m_decodeTimer, &QTimer::timeout, this, [this]() {
        if (!m_playing || !m_session.isOpen() || m_seeking)
            return;
        if (m_session.readFrame()) {
            applyCurrentFrame();
        } else {
            m_playing = false;
            updatePlaybackState();
            m_updateTimer->stop();
            m_decodeTimer->stop();
        }
    });
}

MainWindow::~MainWindow()
{
    m_playing = false;
    m_session.close();
}

void MainWindow::setupUI()
{
    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_videoWidget = new VideoWidget(this);
    m_videoWidget->setMinimumSize(320, 240);
    mainLayout->addWidget(m_videoWidget, 1);

    auto *controlsWidget = new QWidget(this);
    controlsWidget->setStyleSheet("QWidget { background: #222; }");
    auto *controlLayout = new QVBoxLayout(controlsWidget);
    controlLayout->setContentsMargins(8, 4, 8, 8);
    controlLayout->setSpacing(4);

    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setRange(0, 1000);
    m_seekSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #888; width: 12px; margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #4a9eff; height: 4px; border-radius: 2px; }"
    );
    connect(m_seekSlider, &QSlider::sliderPressed, this, &MainWindow::seekBySliderPressed);
    connect(m_seekSlider, &QSlider::sliderMoved, this, &MainWindow::seekBySlider);
    connect(m_seekSlider, &QSlider::sliderReleased, this, &MainWindow::seekBySliderReleased);
    controlLayout->addWidget(m_seekSlider);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(8);

    m_playBtn = new QPushButton(this);
    m_playBtn->setFixedSize(32, 32);
    m_playBtn->setStyleSheet(
        "QPushButton { background: #444; border: none; border-radius: 4px; color: white; font-size: 16px; }"
        "QPushButton:hover { background: #555; }"
    );
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::togglePlayPause);
    btnLayout->addWidget(m_playBtn);

    m_timeLabel = new QLabel("00:00.000 / 00:00.000", this);
    m_timeLabel->setStyleSheet("color: #aaa; font-size: 11px; font-family: monospace;");
    btnLayout->addWidget(m_timeLabel);

    btnLayout->addStretch();

    auto *volLabel = new QLabel("Vol", this);
    volLabel->setStyleSheet("color: #aaa; font-size: 11px;");
    btnLayout->addWidget(volLabel);

    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setFixedWidth(80);
    m_volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 3px; }"
        "QSlider::handle:horizontal { background: #888; width: 8px; margin: -3px 0; border-radius: 4px; }"
        "QSlider::sub-page:horizontal { background: #4a9eff; height: 3px; }"
    );
    btnLayout->addWidget(m_volumeSlider);

    controlLayout->addLayout(btnLayout);
    mainLayout->addWidget(controlsWidget);

    auto *fileMenu = menuBar()->addMenu("&File");
    auto *openAction = fileMenu->addAction("&Open...", this, &MainWindow::openFile);
    openAction->setShortcut(QKeySequence::Open);
    fileMenu->addSeparator();
    fileMenu->addAction("&Close", this, &MainWindow::closeFile, QKeySequence("Ctrl+W"));
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close, QKeySequence::Quit);
}

void MainWindow::closeFile()
{
    m_playing = false;
    m_updateTimer->stop();
    m_decodeTimer->stop();
    m_session.close();
    m_currentFilePath.clear();
    m_videoWidget->clearFrame();
    updatePlaybackState();
    updateTitle();
}

void MainWindow::openFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Media", QString(),
        "Video Files (*.mp4 *.mov *.avi *.mkv *.webm *.m4v *.ts *.mts);;All Files (*)");
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::loadFile(const QString &path)
{
    m_playing = false;
    m_updateTimer->stop();
    m_decodeTimer->stop();
    m_session.close();
    m_currentFilePath = path;

    if (!m_session.open(path)) {
        QMessageBox::warning(this, "Error", "Could not open file:\n" + path);
        updatePlaybackState();
        updateTitle();
        return;
    }

    updateTitle();
    m_seekSlider->setValue(0);
    m_currentPosSec = 0.0;

    m_session.readFrame();
    applyCurrentFrame();

    m_playing = true;
    updatePlaybackState();
    m_decodeTimer->start(0);
    m_updateTimer->start(33);
}

void MainWindow::togglePlayPause()
{
    if (!m_session.isOpen()) {
        openFile();
        return;
    }
    m_playing = !m_playing;
    if (m_playing) {
        m_decodeTimer->start(0);
        m_updateTimer->start(33);
    } else {
        m_decodeTimer->stop();
        m_updateTimer->stop();
    }
    updatePlaybackState();
}

void MainWindow::seekBySlider(int value)
{
    if (!m_session.isOpen()) return;
    double pos = (static_cast<double>(value) / 1000.0) * m_session.durationSec();
    m_pendingSeekPos = pos;
    m_pendingSeek = true;
}

void MainWindow::seekBySliderPressed()
{
    m_seeking = true;
}

void MainWindow::seekBySliderReleased()
{
    if (!m_session.isOpen()) return;
    m_session.seekSec(m_pendingSeekPos);
    m_currentPosSec = m_pendingSeekPos;
    m_pendingSeek = false;

    if (m_session.readFrame()) {
        applyCurrentFrame();
    }

    m_seeking = false;
}

void MainWindow::applyCurrentFrame()
{
    if (m_videoWidget->rendererSupportsAVFrame() && m_session.decodedFrame())
        m_videoWidget->setAVFrame(m_session.decodedFrame());
    else
        applyCurrentFrame();
}

void MainWindow::updateTimerTick()
{
    if (!m_session.isOpen() || m_seeking || !m_playing)
        return;

    double fps = m_session.fps();
    double frameDur = (fps > 0) ? (1.0 / fps) : (1.0 / 24.0);
    m_currentPosSec += frameDur;

    if (m_currentPosSec > m_session.durationSec()) {
        m_playing = false;
        updatePlaybackState();
        m_updateTimer->stop();
        m_decodeTimer->stop();
        m_currentPosSec = 0.0;
        m_session.seekSec(0);
        m_session.readFrame();
        applyCurrentFrame();
    }

    int totalMs = static_cast<int>(m_session.durationSec() * 1000.0);
    int curMs = static_cast<int>(m_currentPosSec * 1000.0);

    if (totalMs > 0) {
        int sliderVal = static_cast<int>((m_currentPosSec / m_session.durationSec()) * 1000.0);
        m_seekSlider->blockSignals(true);
        m_seekSlider->setValue(qBound(0, sliderVal, 1000));
        m_seekSlider->blockSignals(false);
    }

    auto formatTime = [](int ms) -> QString {
        int h = ms / 3600000;
        int m = (ms % 3600000) / 60000;
        int s = (ms % 60000) / 1000;
        int ml = ms % 1000;
        if (h > 0)
            return QString("%1:%2:%3.%4").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')).arg(ml, 3, 10, QChar('0'));
        return QString("%1:%2.%3").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')).arg(ml, 3, 10, QChar('0'));
    };
    m_timeLabel->setText(formatTime(curMs) + " / " + formatTime(totalMs));
}

void MainWindow::updatePlaybackState()
{
    if (!m_session.isOpen()) {
        m_playBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_playBtn->setToolTip("Open File");
        return;
    }
    if (m_playing) {
        m_playBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        m_playBtn->setToolTip("Pause");
    } else {
        m_playBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_playBtn->setToolTip("Play");
    }
}

void MainWindow::updateTitle()
{
    if (m_session.isOpen()) {
        QString fileName = QFileInfo(m_currentFilePath).fileName();
        if (fileName.isEmpty())
            fileName = "Untitled";
        setWindowTitle(QString("Lunar Player - %1").arg(fileName));
    } else {
        setWindowTitle("Lunar Player - No File");
    }
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!m_session.isOpen()) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    switch (event->key()) {
    case Qt::Key_Space:
        togglePlayPause();
        break;
    case Qt::Key_Left: {
        double step = 1.0 / qMax(m_session.fps(), 1);
        double newPos = qBound(0.0, m_currentPosSec - step, m_session.durationSec());
        m_session.seekSec(newPos);
        m_currentPosSec = newPos;
        if (m_session.readFrame())
            applyCurrentFrame();
        break;
    }
    case Qt::Key_Right: {
        double step = 1.0 / qMax(m_session.fps(), 1);
        double newPos = qBound(0.0, m_currentPosSec + step, m_session.durationSec());
        m_session.seekSec(newPos);
        m_currentPosSec = newPos;
        if (m_session.readFrame())
            applyCurrentFrame();
        break;
    }
    case Qt::Key_Escape:
        if (isFullScreen())
            showNormal();
        break;
    default:
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_playing = false;
    m_updateTimer->stop();
    m_decodeTimer->stop();
    m_session.close();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        QString path = urls.first().toLocalFile();
        if (!path.isEmpty())
            loadFile(path);
    }
}
