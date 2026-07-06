#ifndef LOCALUPDATESERVER_H
#define LOCALUPDATESERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>

class LocalUpdateServer : public QObject {
    Q_OBJECT
public:
    explicit LocalUpdateServer(QObject *parent = nullptr)
        : QObject(parent), m_server(new QTcpServer(this))
    {
        connect(m_server, &QTcpServer::newConnection, this, &LocalUpdateServer::onNewConnection);
    }

    ~LocalUpdateServer() { stop(); }

    bool start() {
        if (!m_server->listen(QHostAddress::LocalHost, 0)) {
            fprintf(stderr, "[LOCAL-SERVER] Failed to start: %s\n",
                    m_server->errorString().toUtf8().constData());
            fflush(stderr);
            return false;
        }
        m_port = m_server->serverPort();
        fprintf(stderr, "[LOCAL-SERVER] Listening on http://127.0.0.1:%d\n", m_port);
        fflush(stderr);
        return true;
    }

    void stop() {
        if (m_server->isListening()) {
            m_server->close();
        }
    }

    quint16 port() const { return m_port; }
    QString baseUrl() const { return QString("http://127.0.0.1:%1").arg(m_port); }
    QString apiUrl() const { return baseUrl() + "/repos/test/test/releases/latest"; }

    void setZipData(const QByteArray &data) { m_zipData = data; m_zipHash = computeHash(data); }
    void setZipPath(const QString &path) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            m_zipData = f.readAll();
            m_zipHash = computeHash(m_zipData);
        }
    }

    void setVersion(const QString &v) { m_version = v; }
    void setReleaseNotes(const QString &n) { m_releaseNotes = n; }
    void setSimulateNotFound(bool b) { m_simulateNotFound = b; }
    void setSimulateCorrupt(bool b) { m_simulateCorrupt = b; }
    void setSimulateNoAssets(bool b) { m_simulateNoAssets = b; }
    void setSimulateRateLimit(bool b) { m_simulateRateLimit = b; }

    int connectionCount() const { return m_connectionCount; }
    int downloadCount() const { return m_downloadCount; }

    static QString computeHash(const QByteArray &data) {
        return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().toUpper();
    }

signals:
    void requestReceived(const QString &path);

private slots:
    void onNewConnection() {
        while (m_server->hasPendingConnections()) {
            QTcpSocket *sock = m_server->nextPendingConnection();
            m_connectionCount++;
            connect(sock, &QTcpSocket::readyRead, this, [this, sock]() { handleRequest(sock); });
            connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
        }
    }

private:
    void handleRequest(QTcpSocket *sock) {
        QByteArray request = sock->readAll();
        QByteArray method = request.left(request.indexOf(' '));
        QByteArray path = request.mid(method.size() + 1);
        path = path.left(path.indexOf(' '));

        QString pathStr = QString::fromUtf8(path);
        emit requestReceived(pathStr);
        fprintf(stderr, "[LOCAL-SERVER] %s %s\n", method.constData(), path.constData());
        fflush(stderr);

        if (m_simulateNotFound) {
            sendResponse(sock, 404, "Not Found", R"({"message":"Not Found"})");
            return;
        }
        if (m_simulateRateLimit) {
            sendResponse(sock, 403, "Forbidden", R"({"message":"API rate limit exceeded"})");
            return;
        }

        if (pathStr.contains("/releases/latest")) {
            QJsonObject asset;
            if (!m_simulateNoAssets) {
                asset["name"] = "LunarPlayerPortable.zip";
                asset["browser_download_url"] = baseUrl() + "/downloads/LunarPlayerPortable.zip";
                asset["size"] = (qint64)m_zipData.size();
                asset["content_type"] = "application/zip";
            }

            QJsonObject release;
            release["tag_name"] = m_version;
            release["name"] = "Lunar Player " + m_version;
            release["published_at"] = "2026-07-06T00:00:00Z";
            release["body"] = m_releaseNotes;

            if (!m_simulateNoAssets) {
                release["assets"] = QJsonArray({asset});
            } else {
                release["assets"] = QJsonArray();
            }

            QJsonDocument doc(release);
            sendResponse(sock, 200, "OK", doc.toJson(QJsonDocument::Compact));
            return;
        }

        if (pathStr.contains("/downloads/LunarPlayerPortable.zip")) {
            if (m_simulateCorrupt) {
                QByteArray corrupt = m_zipData.mid(0, qMin(m_zipData.size() / 2, (qsizetype)1024));
                sendResponse(sock, 200, "OK", corrupt);
                return;
            }
            m_downloadCount++;
            sendResponse(sock, 200, "OK", m_zipData);
            return;
        }

        sendResponse(sock, 404, "Not Found", R"({"message":"Unknown path"})");
    }

    void sendResponse(QTcpSocket *sock, int code, const QByteArray &status, const QByteArray &body) {
        QByteArray header;
        header += "HTTP/1.1 " + QByteArray::number(code) + " " + status + "\r\n";
        header += "Content-Type: application/json\r\n";
        header += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        header += "Connection: close\r\n";
        header += "\r\n";
        sock->write(header);
        if (pathIsFileRequest(sock)) {
            // For zip downloads, send raw bytes
        }
        sock->write(body);
        sock->flush();
        sock->disconnectFromHost();
    }

    bool pathIsFileRequest(QTcpSocket *sock) { Q_UNUSED(sock); return false; }

    QTcpServer *m_server = nullptr;
    quint16 m_port = 0;
    QByteArray m_zipData;
    QString m_zipHash;
    QString m_version = "0.2.0";
    QString m_releaseNotes = "Test release notes for E2E testing.\n\n- Bug fixes\n- Performance improvements";
    bool m_simulateNotFound = false;
    bool m_simulateCorrupt = false;
    bool m_simulateNoAssets = false;
    bool m_simulateRateLimit = false;
    int m_connectionCount = 0;
    int m_downloadCount = 0;
};

#endif
