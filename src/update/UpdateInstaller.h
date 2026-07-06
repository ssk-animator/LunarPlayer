#ifndef UPDATEINSTALLER_H
#define UPDATEINSTALLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QElapsedTimer>

struct InstallResult {
    bool success = false;
    bool rollbackPerformed = false;
    QString errorString;
    QString newExePath;
};

class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    explicit UpdateInstaller(QObject *parent = nullptr);

    void setInstallDir(const QString &dir) { m_installDir = dir; }

    InstallResult installFromZip(const QString &zipPath, const QString &updateVersion);

    static void cleanupOldUpdates(const QString &updatesDir, const QString &keepZip = {});
    static QString stagingDir(const QString &updatesDir, const QString &version);
    static QString backupDir(const QString &updatesDir);

signals:
    void installationFailed(const QString &error);

private:
    bool extractZipToStaging(const QString &zipPath, const QString &stagingPath);
    bool validateStaging(const QString &stagingPath);
    bool backupCurrentInstall(const QString &backupPath);
    bool swapFiles(const QString &stagingPath, const QString &backupPath);
    bool rollbackFromBackup(const QString &backupPath);
    void cleanupAfterSuccess(const QString &stagingPath, const QString &backupPath);

    QString m_installDir;
};

#endif
