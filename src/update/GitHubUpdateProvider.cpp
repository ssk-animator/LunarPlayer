#include "GitHubUpdateProvider.h"
#include "SemVer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QEventLoop>

GitHubUpdateProvider::GitHubUpdateProvider(const QString &owner, const QString &repo,
                                           const QString &token, QObject *parent)
    : IUpdateProvider(parent), m_owner(owner), m_repo(repo), m_token(token)
{
}

QNetworkRequest GitHubUpdateProvider::apiRequest(const QString &url) const
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "LunarPlayer-UpdateChecker/1.0");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());
    return req;
}

std::optional<ReleaseInfo> GitHubUpdateProvider::checkForUpdate(
    const QString &currentVersion, QString *errorOut)
{
    QString apiBase = m_overrideUrl.isEmpty()
        ? QString("https://api.github.com/repos/%1/%2/releases/latest").arg(m_owner, m_repo)
        : m_overrideUrl;

    QNetworkRequest req;
    req.setUrl(QUrl(apiBase));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "LunarPlayer-UpdateChecker/1.0");
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + m_token).toUtf8());

    QNetworkReply *reply = m_nam.get(req);

    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

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
            errMsg = QString("Network error: %1 (HTTP %2)").arg(reply->errorString()).arg(statusCode);
        }
        if (errorOut) *errorOut = errMsg;
        reply->deleteLater();
        return std::nullopt;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    auto release = parseReleaseJson(data, errorOut);
    if (!release.isValid())
        return std::nullopt;

    SemVer remote = SemVer::fromString(release.version);
    SemVer local = SemVer::fromString(currentVersion);
    if (!remote.isValid() || !local.isValid()) {
        if (errorOut) *errorOut = "Invalid version format";
        return std::nullopt;
    }

    if (remote <= local) {
        if (errorOut) *errorOut = "Already up to date";
        return std::nullopt;
    }

    return release;
}

ReleaseInfo GitHubUpdateProvider::parseReleaseJson(const QByteArray &data, QString *errorOut)
{
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        if (errorOut) *errorOut = "JSON parse error: " + err.errorString();
        return {};
    }
    return parseReleaseAsset(doc.object(), "LunarPlayerPortable.zip", errorOut);
}

ReleaseInfo GitHubUpdateProvider::parseReleaseAsset(
    const QJsonObject &release, const QString &zipName, QString *errorOut)
{
    ReleaseInfo info;
    info.version = release["tag_name"].toString();
    info.title = release["name"].toString();
    info.releaseDate = release["published_at"].toString();
    info.releaseNotes = release["body"].toString();

    if (info.version.isEmpty()) {
        if (errorOut) *errorOut = "No tag_name in release";
        return {};
    }

    auto assets = release["assets"].toArray();
    for (const auto &asset : assets) {
        auto obj = asset.toObject();
        QString name = obj["name"].toString();
        if (name == zipName) {
            info.downloadUrl = obj["browser_download_url"].toString();
            info.downloadSize = obj["size"].toInteger();
            info.fileName = name;
            break;
        }
    }

    if (info.downloadUrl.isEmpty()) {
        if (errorOut) *errorOut = "Portable ZIP not found in release assets";
        return {};
    }

    return info;
}
