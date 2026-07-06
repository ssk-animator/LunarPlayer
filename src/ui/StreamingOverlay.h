#pragma once
#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include "../network/NetworkBuffer.h"

class StreamingOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit StreamingOverlay(QWidget *parent = nullptr);

    void setBuffer(NetworkBuffer *buffer);
    void setLive(bool live);
    void setUrl(const QString &url);

protected:
    void paintEvent(QPaintEvent *e) override;

private slots:
    void updateDisplay();
    void onStateChanged(NetworkBuffer::State state);
    void onStallWarning();

private:
    NetworkBuffer *m_buffer = nullptr;
    bool m_isLive = false;
    QString m_url;

    QLabel *m_statusLabel;
    QLabel *m_bitrateLabel;
    QLabel *m_latencyLabel;
    QLabel *m_droppedLabel;
    QLabel *m_urlLabel;
    QProgressBar *m_bufferBar;
    QTimer *m_updateTimer;

    void setupUI();
    QString stateText(NetworkBuffer::State s);
};
