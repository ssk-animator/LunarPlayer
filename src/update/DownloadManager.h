#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

class DownloadManager : public QObject {
    Q_OBJECT
public:
    struct Progress {
        qint64 bytesReceived = 0;
        qint64 bytesTotal = 0;
        double speedMbps = 0.0;
        int remainingSec = 0;
        int percent = 0;
    };

    explicit DownloadManager(QObject *parent = nullptr);

    void startDownload(const QUrl &url, const QString &destPath);
    void pauseDownload();
    void resumeDownload();
    void cancelDownload();

    bool isDownloading() const { return m_downloading; }
    bool isPaused() const { return m_paused; }
    QString filePath() const { return m_destPath; }

signals:
    void progressChanged(DownloadManager::Progress progress);
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);
    void downloadCancelled();

private slots:
    void onReadyRead();
    void onDownloadProgress(qint64 received, qint64 total);
    void onFinished();

private:
    void startRequest();
    void updateSpeed();

    QNetworkAccessManager m_nam;
    QNetworkReply *m_reply = nullptr;
    QFile m_file;
    QString m_destPath;
    QUrl m_url;
    qint64 m_bytesAtStart = 0;
    qint64 m_bytesReceived = 0;
    qint64 m_bytesTotal = 0;
    bool m_downloading = false;
    bool m_paused = false;

    QElapsedTimer m_speedTimer;
    QTimer m_speedUpdateTimer;
    qint64 m_lastSpeedBytes = 0;
};

#endif
