#ifndef IUPDATEPROVIDER_H
#define IUPDATEPROVIDER_H

#include <QObject>
#include <QString>
#include <optional>

struct ReleaseInfo {
    QString version;
    QString title;
    QString releaseDate;
    QString releaseNotes;
    QString downloadUrl;
    QString sha256;
    qint64 downloadSize = 0;
    QString fileName;

    bool isValid() const { return !version.isEmpty() && !downloadUrl.isEmpty(); }
};

class IUpdateProvider : public QObject {
    Q_OBJECT
public:
    explicit IUpdateProvider(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IUpdateProvider() = default;

    virtual std::optional<ReleaseInfo> checkForUpdate(
        const QString &currentVersion, QString *errorOut = nullptr) = 0;

    virtual QString providerName() const = 0;
    virtual QString repositoryUrl() const = 0;
};

#endif
