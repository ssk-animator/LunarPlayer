#include "UpdateManager.h"
#include "SemVer.h"
#include <QCoreApplication>
#include <QDir>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <cstdio>

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent),
      m_provider(new GitHubUpdateProvider("ssk-animator", "LunarPlayer", {}, this)),
      m_downloadManager(new DownloadManager(this)),
      m_verifier(new PackageVerifier(this)),
      m_installer(new UpdateInstaller(this))
{
    loadSettings();
    m_provider->setChannel(m_channel == UpdateChannel::Stable ? "stable" : "beta");

    connect(m_downloadManager, &DownloadManager::downloadFinished,
            this, &UpdateManager::onProviderDownloadFinished);
    connect(m_downloadManager, &DownloadManager::downloadFailed,
            this, &UpdateManager::onProviderDownloadFailed);
}

QString UpdateManager::updatesDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/Updates";
}

QString UpdateManager::tempDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/LunarPlayer";
}

void UpdateManager::checkForUpdate(bool silent)
{
    QString currentVersion = QCoreApplication::applicationVersion();
    QString errorStr;

    fprintf(stderr, "[UPDATE] Checking for updates (current=%s)\n",
            currentVersion.toUtf8().constData());
    fflush(stderr);

    auto release = m_provider->checkForUpdate(currentVersion, &errorStr);
    if (!release.has_value()) {
        if (errorStr == "Already up to date" || errorStr.contains("up to date")) {
            fprintf(stderr, "[UPDATE] No updates available\n");
            fflush(stderr);
            emit noUpdateAvailable();
        } else {
            fprintf(stderr, "[UPDATE] Check failed: %s\n", errorStr.toUtf8().constData());
            fflush(stderr);
            emit checkFailed(errorStr);
        }
        return;
    }

    m_lastRelease = release;
    QString remoteVer = release->version;
    fprintf(stderr, "[UPDATE] Update available: %s (latest=%s)\n",
            currentVersion.toUtf8().constData(), remoteVer.toUtf8().constData());
    fflush(stderr);

    if (isVersionSkipped(remoteVer)) {
        fprintf(stderr, "[UPDATE] Version %s is skipped\n", remoteVer.toUtf8().constData());
        fflush(stderr);
        emit noUpdateAvailable();
        return;
    }

    emit updateAvailable(*release);

    if (m_autoDownload && release->downloadSize > 0) {
        fprintf(stderr, "[UPDATE] Auto-downloading update\n");
        fflush(stderr);
        startUpdate();
    }
}

void UpdateManager::checkForUpdateAsync()
{
    QString currentVersion = QCoreApplication::applicationVersion();
    fprintf(stderr, "[UPDATE] Async check starting (current=%s)\n",
            currentVersion.toUtf8().constData());
    fflush(stderr);

    QString apiBase = m_overrideUrl.isEmpty()
        ? QString("https://api.github.com/repos/ssk-animator/LunarPlayer/releases/latest")
        : m_overrideUrl;
    QNetworkRequest req{QUrl(apiBase)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "LunarPlayer-UpdateChecker/1.0");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());

    QNetworkAccessManager *nam = new QNetworkAccessManager(this);
    QNetworkReply *reply = nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, currentVersion, nam]() {
        nam->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errMsg;
            if (statusCode == 404) {
                errMsg = "No releases found for this repository";
            } else if (statusCode == 403) {
                errMsg = "GitHub API rate limit exceeded or access denied";
            } else if (statusCode >= 500) {
                errMsg = "GitHub server error (HTTP " + QString::number(statusCode) + ")";
            } else {
                errMsg = QString("Network error: %1 (HTTP %2)")
                             .arg(reply->errorString()).arg(statusCode);
            }
            fprintf(stderr, "[UPDATE] Async check failed: %s\n",
                    errMsg.toUtf8().constData());
            fflush(stderr);
            emit checkFailed(errMsg);
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QString errorStr;
        auto release = GitHubUpdateProvider::parseReleaseJsonPublic(data, &errorStr);
        if (!release.isValid()) {
            fprintf(stderr, "[UPDATE] Async check parse error: %s\n",
                    errorStr.toUtf8().constData());
            fflush(stderr);
            emit checkFailed(errorStr);
            return;
        }

        SemVer remote = SemVer::fromString(release.version);
        SemVer local = SemVer::fromString(currentVersion);
        if (!remote.isValid() || !local.isValid()) {
            fprintf(stderr, "[UPDATE] Invalid version format\n");
            fflush(stderr);
            emit checkFailed("Invalid version format");
            return;
        }

        if (remote <= local) {
            fprintf(stderr, "[UPDATE] Async check: up to date\n");
            fflush(stderr);
            emit noUpdateAvailable();
            return;
        }

        m_lastRelease = release;
        fprintf(stderr, "[UPDATE] Async check: update available %s\n",
                release.version.toUtf8().constData());
        fflush(stderr);

        if (isVersionSkipped(release.version)) {
            fprintf(stderr, "[UPDATE] Version %s is skipped\n",
                    release.version.toUtf8().constData());
            fflush(stderr);
            emit noUpdateAvailable();
            return;
        }

        emit updateAvailable(release);

        if (m_autoDownload && release.downloadSize > 0) {
            fprintf(stderr, "[UPDATE] Auto-downloading update\n");
            fflush(stderr);
            startUpdate();
        }
    });
}

void UpdateManager::startUpdate()
{
    if (!m_lastRelease) return;

    QString destPath = updatesDir() + "/" + m_lastRelease->fileName;
    QDir().mkpath(updatesDir());

    fprintf(stderr, "[UPDATE] Starting download to %s\n", destPath.toUtf8().constData());
    fflush(stderr);

    m_downloadManager->startDownload(QUrl(m_lastRelease->downloadUrl), destPath);
}

void UpdateManager::cancelUpdate()
{
    m_downloadManager->cancelDownload();
}

void UpdateManager::onProviderDownloadFinished(const QString &path)
{
    fprintf(stderr, "[UPDATE] Download complete: %s\n", path.toUtf8().constData());
    fflush(stderr);

    // Verify the package
    auto result = m_verifier->verify(path, *m_lastRelease, QCoreApplication::applicationVersion());
    if (!result.passed) {
        fprintf(stderr, "[UPDATE] Verification failed: %s\n", result.errorString.toUtf8().constData());
        fflush(stderr);
        emit verificationFailed(result.errorString);
        QFile::remove(path);
        return;
    }

    fprintf(stderr, "[UPDATE] Verification passed\n");
    fflush(stderr);
    emit verificationPassed();

    m_pendingUpdatePath = path;
    m_pendingUpdateVersion = m_lastRelease->version;

    if (m_notifyBeforeInstall) {
        // Let the UI handle the installation prompt
        return;
    }

    launchHelper();
}

void UpdateManager::onProviderDownloadFailed(const QString &error)
{
    fprintf(stderr, "[UPDATE] Download failed: %s\n", error.toUtf8().constData());
    fflush(stderr);
    emit downloadFailed(error);
}

void UpdateManager::launchPendingUpdate()
{
    launchHelper();
}

void UpdateManager::launchHelper()
{
    if (m_pendingUpdatePath.isEmpty()) return;

    fprintf(stderr, "[UPDATE] Launching update helper\n");
    fflush(stderr);

    m_installer->cleanupOldUpdates(updatesDir(), m_pendingUpdatePath);

    QString helperPath =
        QCoreApplication::applicationDirPath() + "/LunarUpdateHelper.exe";
    QStringList args;
    args << "--archive" << m_pendingUpdatePath;
    args << "--install-dir" << QCoreApplication::applicationDirPath();
    args << "--pid" << QString::number(QCoreApplication::applicationPid());
    args << "--version" << m_pendingUpdateVersion;

    fprintf(stderr, "[UPDATE] Helper: %s\n", helperPath.toUtf8().constData());
    fflush(stderr);

    bool launched = QProcess::startDetached(helperPath, args);
    if (!launched) {
        fprintf(stderr, "[UPDATE] Failed to launch helper\n");
        fflush(stderr);
        emit installationFailed("Failed to launch update helper");
        return;
    }

    emit installationFinished();
    QCoreApplication::quit();
}

void UpdateManager::skipVersion(const QString &version)
{
    QStringList skipped = m_settings.value("updates/skippedVersions").toStringList();
    if (!skipped.contains(version)) {
        skipped.append(version);
        m_settings.setValue("updates/skippedVersions", skipped);
    }
}

bool UpdateManager::isVersionSkipped(const QString &version) const
{
    return m_settings.value("updates/skippedVersions").toStringList().contains(version);
}

void UpdateManager::setAutoCheck(bool enabled)
{
    m_autoCheck = enabled;
    saveSettings();
}

void UpdateManager::setAutoDownload(bool enabled)
{
    m_autoDownload = enabled;
    saveSettings();
}

void UpdateManager::setNotifyBeforeInstall(bool enabled)
{
    m_notifyBeforeInstall = enabled;
    saveSettings();
}

void UpdateManager::setChannel(UpdateChannel ch)
{
    m_channel = ch;
    m_provider->setChannel(ch == UpdateChannel::Stable ? "stable" : "beta");
    saveSettings();
}

void UpdateManager::setToken(const QString &token)
{
    m_token = token;
    m_provider->setToken(token);
}

void UpdateManager::setOverrideUrl(const QString &url)
{
    m_overrideUrl = url;
    m_provider->setOverrideUrl(url);
}

void UpdateManager::saveSettings()
{
    m_settings.setValue("updates/autoCheck", m_autoCheck);
    m_settings.setValue("updates/autoDownload", m_autoDownload);
    m_settings.setValue("updates/notifyBeforeInstall", m_notifyBeforeInstall);
    m_settings.setValue("updates/channel",
                        m_channel == UpdateChannel::Stable ? "stable" : "beta");
}

void UpdateManager::loadSettings()
{
    m_autoCheck = m_settings.value("updates/autoCheck", true).toBool();
    m_autoDownload = m_settings.value("updates/autoDownload", false).toBool();
    m_notifyBeforeInstall = m_settings.value("updates/notifyBeforeInstall", true).toBool();
    QString ch = m_settings.value("updates/channel", "stable").toString();
    m_channel = (ch == "beta") ? UpdateChannel::Beta : UpdateChannel::Stable;
}
