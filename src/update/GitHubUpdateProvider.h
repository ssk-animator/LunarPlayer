#ifndef GITHUBUPDATEPROVIDER_H
#define GITHUBUPDATEPROVIDER_H

#include "IUpdateProvider.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

class GitHubUpdateProvider : public IUpdateProvider {
    Q_OBJECT
public:
    explicit GitHubUpdateProvider(const QString &owner, const QString &repo,
                                 const QString &token = {},
                                 QObject *parent = nullptr);

    std::optional<ReleaseInfo> checkForUpdate(
        const QString &currentVersion, QString *errorOut = nullptr) override;

    QString providerName() const override { return "GitHub Releases"; }
    QString repositoryUrl() const override {
        return QString("https://github.com/%1/%2").arg(m_owner, m_repo);
    }

    void setToken(const QString &token) { m_token = token; }
    void setChannel(const QString &channel) { m_channel = channel; }
    void setOverrideUrl(const QString &url) { m_overrideUrl = url; }

private:
    QNetworkRequest apiRequest(const QString &url) const;
    static ReleaseInfo parseReleaseJson(const QByteArray &data, QString *errorOut);
    static ReleaseInfo parseReleaseAsset(const QJsonObject &release,
                                         const QString &zipName,
                                         QString *errorOut);

public:
    static ReleaseInfo parseReleaseJsonPublic(const QByteArray &data, QString *errorOut) {
        return parseReleaseJson(data, errorOut);
    }

private:

    QString m_owner;
    QString m_repo;
    QString m_token;
    QString m_channel = "stable";
    QString m_overrideUrl;
    QNetworkAccessManager m_nam;
};

#endif
