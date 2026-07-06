#include "StreamingOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QFont>
#include <QDateTime>

StreamingOverlay::StreamingOverlay(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &StreamingOverlay::updateDisplay);
    m_updateTimer->start(1000);
}

void StreamingOverlay::setupUI()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_urlLabel = new QLabel(this);
    m_urlLabel->setStyleSheet("color: #aaa; font-size: 10px;");
    m_urlLabel->setWordWrap(true);
    layout->addWidget(m_urlLabel);

    auto *statusRow = new QHBoxLayout();
    m_statusLabel = new QLabel("Idle", this);
    m_statusLabel->setStyleSheet("color: #0f0; font-weight: bold; font-size: 12px;");
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch();
    layout->addLayout(statusRow);

    m_bufferBar = new QProgressBar(this);
    m_bufferBar->setRange(0, 100);
    m_bufferBar->setValue(0);
    m_bufferBar->setTextVisible(true);
    m_bufferBar->setFixedHeight(14);
    m_bufferBar->setStyleSheet(
        "QProgressBar { background: #333; border: 1px solid #555; border-radius: 3px; text-align: center; color: #ccc; font-size: 9px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0, stop:0 #0a0, stop:1 #0f0); border-radius: 2px; }");
    layout->addWidget(m_bufferBar);

    auto *infoRow = new QHBoxLayout();
    m_bitrateLabel = new QLabel("0 Mbps", this);
    m_bitrateLabel->setStyleSheet("color: #aaa; font-size: 10px;");
    m_latencyLabel = new QLabel("0 ms", this);
    m_latencyLabel->setStyleSheet("color: #aaa; font-size: 10px;");
    m_droppedLabel = new QLabel("0 dropped", this);
    m_droppedLabel->setStyleSheet("color: #aaa; font-size: 10px;");
    infoRow->addWidget(m_bitrateLabel);
    infoRow->addWidget(m_latencyLabel);
    infoRow->addWidget(m_droppedLabel);
    infoRow->addStretch();
    layout->addLayout(infoRow);

    layout->addStretch();
    setFixedWidth(280);
    setMinimumHeight(120);
}

void StreamingOverlay::setBuffer(NetworkBuffer *buffer)
{
    m_buffer = buffer;
    if (buffer) {
        connect(buffer, &NetworkBuffer::stateChanged, this, &StreamingOverlay::onStateChanged);
        connect(buffer, &NetworkBuffer::stallWarning, this, &StreamingOverlay::onStallWarning);
        connect(buffer, &NetworkBuffer::progressChanged, this, [this](double) { updateDisplay(); });
    }
}

void StreamingOverlay::setLive(bool live) { m_isLive = live; }
void StreamingOverlay::setUrl(const QString &url) { m_url = url; m_urlLabel->setText(url); }

void StreamingOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 180));
}

void StreamingOverlay::updateDisplay()
{
    if (!m_buffer) return;
    m_bitrateLabel->setText(QString("%1 Mbps").arg(m_buffer->bitrateBps() / 1000000.0, 0, 'f', 1));
    m_latencyLabel->setText(QString("%1 ms").arg(m_buffer->latencyMs(), 0, 'f', 0));
    m_droppedLabel->setText(QString("%1 dropped").arg(m_buffer->droppedPackets()));
    m_bufferBar->setValue(static_cast<int>(m_buffer->progress() * 100.0));
    m_statusLabel->setText(stateText(m_buffer->state()));
    m_urlLabel->setText(m_url);
}

void StreamingOverlay::onStateChanged(NetworkBuffer::State state)
{
    m_statusLabel->setText(stateText(state));
    switch (state) {
    case NetworkBuffer::Stalled:
        m_statusLabel->setStyleSheet("color: #f80; font-weight: bold; font-size: 12px;");
        break;
    case NetworkBuffer::Error:
        m_statusLabel->setStyleSheet("color: #f00; font-weight: bold; font-size: 12px;");
        break;
    case NetworkBuffer::Ready:
        m_statusLabel->setStyleSheet("color: #0f0; font-weight: bold; font-size: 12px;");
        break;
    default:
        m_statusLabel->setStyleSheet("color: #aaa; font-weight: bold; font-size: 12px;");
    }
}

void StreamingOverlay::onStallWarning()
{
    m_statusLabel->setText("STALLED — Buffering...");
    m_statusLabel->setStyleSheet("color: #f80; font-weight: bold; font-size: 12px;");
}

QString StreamingOverlay::stateText(NetworkBuffer::State s)
{
    switch (s) {
    case NetworkBuffer::Idle:      return "Idle";
    case NetworkBuffer::Buffering: return "Buffering...";
    case NetworkBuffer::Ready:     return "Ready";
    case NetworkBuffer::Stalled:   return "Stalled";
    case NetworkBuffer::Error:     return "Error";
    }
    return "Unknown";
}
