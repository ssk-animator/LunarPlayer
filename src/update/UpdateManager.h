#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include "IUpdateProvider.h"
#include "GitHubUpdateProvider.h"
#include "DownloadManager.h"
#include "PackageVerifier.h"
#include "UpdateInstaller.h"
#include <QObject>
#include <QSettings>
#include <atomic>

class UpdateManager : public QObject {
    Q_OBJECT
public:
    enum class UpdateChannel { Stable, Beta };

    explicit UpdateManager(QObject *parent = nullptr);

    void checkForUpdate(bool silent = false);
    void checkForUpdateAsync();
    void startUpdate();
    void cancelUpdate();
    void skipVersion(const QString &version);
    bool isVersionSkipped(const QString &version) const;

    bool autoCheckEnabled() const { return m_autoCheck; }
    void setAutoCheck(bool enabled);
    bool autoDownloadEnabled() const { return m_autoDownload; }
    void setAutoDownload(bool enabled);
    bool notifyBeforeInstall() const { return m_notifyBeforeInstall; }
    void setNotifyBeforeInstall(bool enabled);
    UpdateChannel channel() const { return m_channel; }
    void setChannel(UpdateChannel ch);
    void setToken(const QString &token);
    void setOverrideUrl(const QString &url);

    bool hasPendingUpdate() const { return !m_pendingUpdatePath.isEmpty(); }
    void launchPendingUpdate();

    std::optional<ReleaseInfo> lastReleaseInfo() const { return m_lastRelease; }

signals:
    void updateAvailable(const ReleaseInfo &info);
    void noUpdateAvailable();
    void checkFailed(const QString &error);
    void downloadProgressChanged(int percent, double speedMbps, int remainingSec);
    void downloadFinished(const QString &path);
    void downloadFailed(const QString &error);
    void verificationPassed();
    void verificationFailed(const QString &error);
    void installationStarted();
    void installationFinished();
    void installationFailed(const QString &error);

private slots:
    void onProviderDownloadFinished(const QString &path);
    void onProviderDownloadFailed(const QString &error);

private:
    QString updatesDir() const;
    QString tempDir() const;
    void saveSettings();
    void loadSettings();
    void launchHelper();

    GitHubUpdateProvider *m_provider = nullptr;
    DownloadManager *m_downloadManager = nullptr;
    PackageVerifier *m_verifier = nullptr;
    UpdateInstaller *m_installer = nullptr;

    std::optional<ReleaseInfo> m_lastRelease;
    QString m_pendingUpdatePath;
    QString m_pendingUpdateVersion;

    bool m_autoCheck = true;
    bool m_autoDownload = false;
    bool m_notifyBeforeInstall = true;
    UpdateChannel m_channel = UpdateChannel::Stable;
    QString m_token;
    QString m_overrideUrl;

    QSettings m_settings;
};

#endif
