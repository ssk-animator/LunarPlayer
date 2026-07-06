#include "DownloadManager.h"
#include <QDir>

DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_speedUpdateTimer, &QTimer::timeout, this, &DownloadManager::updateSpeed);
}

void DownloadManager::startDownload(const QUrl &url, const QString &destPath)
{
    if (m_downloading) return;

    m_url = url;
    m_destPath = destPath;
    m_bytesAtStart = 0;
    m_bytesReceived = 0;
    m_bytesTotal = 0;
    m_paused = false;

    QDir().mkpath(QFileInfo(destPath).absolutePath());

    startRequest();
}

void DownloadManager::startRequest()
{
    QNetworkRequest req(m_url);
    req.setRawHeader("User-Agent", "LunarPlayer-UpdateDownloader/1.0");

    if (m_bytesAtStart > 0) {
        QByteArray range = "bytes=" + QByteArray::number(m_bytesAtStart) + "-";
        req.setRawHeader("Range", range);
    }

    m_reply = m_nam.get(req);
    m_downloading = true;
    m_speedTimer.start();
    m_lastSpeedBytes = 0;
    m_speedUpdateTimer.start(500);

    if (m_bytesAtStart == 0) {
        m_file.setFileName(m_destPath);
        m_file.open(QIODevice::WriteOnly);
    } else {
        m_file.setFileName(m_destPath);
        m_file.open(QIODevice::Append);
    }

    connect(m_reply, &QNetworkReply::readyRead, this, &DownloadManager::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            &DownloadManager::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &DownloadManager::onFinished);
}

void DownloadManager::pauseDownload()
{
    if (!m_downloading || m_paused) return;
    m_paused = true;
    m_speedUpdateTimer.stop();
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_file.close();
}

void DownloadManager::resumeDownload()
{
    if (!m_paused) return;
    m_bytesAtStart = m_file.size();
    startRequest();
}

void DownloadManager::cancelDownload()
{
    if (!m_downloading && !m_paused) return;
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_file.close();
    m_file.remove();
    m_downloading = false;
    m_paused = false;
    m_speedUpdateTimer.stop();
    emit downloadCancelled();
}

void DownloadManager::onReadyRead()
{
    if (!m_reply) return;
    QByteArray data = m_reply->readAll();
    m_file.write(data);
}

void DownloadManager::onDownloadProgress(qint64 received, qint64 total)
{
    m_bytesReceived = m_bytesAtStart + received;
    m_bytesTotal = (total > 0) ? m_bytesAtStart + total : 0;
}

void DownloadManager::onFinished()
{
    m_speedUpdateTimer.stop();

    if (!m_reply) return;

    bool success = (m_reply->error() == QNetworkReply::NoError);
    QString errorStr = m_reply->errorString();

    m_reply->deleteLater();
    m_reply = nullptr;

    if (success) {
        // Check for HTTP range support (partial content = 206)
        int statusCode = m_file.size();
        m_file.close();
        m_downloading = false;
        emit downloadFinished(m_destPath);
    } else {
        m_file.close();
        m_downloading = false;
        emit downloadFailed(errorStr);
    }
}

void DownloadManager::updateSpeed()
{
    if (!m_downloading || m_paused) return;

    qint64 elapsed = m_speedTimer.elapsed();
    if (elapsed <= 0) return;

    qint64 bytesSinceLast = m_bytesReceived - m_lastSpeedBytes;
    double speedMBps =
        static_cast<double>(bytesSinceLast) / (static_cast<double>(elapsed) * 1024.0 * 1024.0);

    m_lastSpeedBytes = m_bytesReceived;
    m_speedTimer.start();

    Progress p;
    p.bytesReceived = m_bytesReceived;
    p.bytesTotal = m_bytesTotal;
    p.speedMbps = speedMBps;
    if (speedMBps > 0.001 && m_bytesTotal > m_bytesReceived) {
        qint64 remaining = (m_bytesTotal - m_bytesReceived) /
                           static_cast<qint64>(speedMBps * 1024.0 * 1024.0);
        p.remainingSec = static_cast<int>(remaining);
    }
    p.percent =
        (m_bytesTotal > 0) ? static_cast<int>((m_bytesReceived * 100) / m_bytesTotal) : 0;

    emit progressChanged(p);
}
