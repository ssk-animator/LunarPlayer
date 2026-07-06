#include "UpdateInstaller.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <cstring>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

UpdateInstaller::UpdateInstaller(QObject *parent) : QObject(parent) {}

QString UpdateInstaller::stagingDir(const QString &updatesDir, const QString &version)
{
    return updatesDir + "/staging_" + version;
}

QString UpdateInstaller::backupDir(const QString &updatesDir)
{
    return updatesDir + "/backup_" + QString::number(QCoreApplication::applicationPid());
}

bool UpdateInstaller::extractZipToStaging(const QString &zipPath, const QString &stagingPath)
{
    QDir().mkpath(stagingPath);

    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));

    QByteArray zipPathUtf8 = zipPath.toUtf8();
    if (!mz_zip_reader_init_file(&archive, zipPathUtf8.constData(), 0))
        return false;

    int fileCount = (int)mz_zip_reader_get_num_files(&archive);
    for (int i = 0; i < fileCount; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, i, &stat))
            continue;

        QString outPath = stagingPath + "/" + QString::fromUtf8(stat.m_filename);
        if (stat.m_filename[strlen(stat.m_filename) - 1] == '/') {
            QDir().mkpath(outPath);
            continue;
        }

        QDir().mkpath(QFileInfo(outPath).absolutePath());

        QByteArray outPathUtf8 = outPath.toUtf8();
        if (!mz_zip_reader_extract_to_file(&archive, i, outPathUtf8.constData(), 0))
            continue;

        // Preserve file timestamps
        if (stat.m_time > 0) {
            QDateTime dt = QDateTime::fromSecsSinceEpoch((qint64)stat.m_time);
            QFile(outPath).setFileTime(dt, QFileDevice::FileModificationTime);
        }
    }

    mz_zip_reader_end(&archive);
    return true;
}

bool UpdateInstaller::validateStaging(const QString &stagingPath)
{
    QString exePath = stagingPath + "/LunarPlayer.exe";
    return QFileInfo::exists(exePath);
}

bool UpdateInstaller::backupCurrentInstall(const QString &backupPath)
{
    QDir().mkpath(backupPath);
    QDir installDir(m_installDir);

    QStringList entries = installDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QString src = m_installDir + "/" + entry;
        QString dst = backupPath + "/" + entry;
        if (QFileInfo(src).isDir()) {
            if (!QDir().mkpath(dst)) return false;
            if (!QDir(src).rename(src, dst)) return false;
        } else {
            if (!QFile::copy(src, dst)) return false;
        }
    }
    return true;
}

bool UpdateInstaller::swapFiles(const QString &stagingPath, const QString &backupPath)
{
    QDir stagingDirObj(stagingPath);
    QStringList entries =
        stagingDirObj.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        QString src = stagingPath + "/" + entry;
        QString dst = m_installDir + "/" + entry;

        if (QFileInfo(src).isDir()) {
            QDir().mkpath(dst);
            QStringList subEntries = QDir(src).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &sub : subEntries) {
                if (!QFile::copy(src + "/" + sub, dst + "/" + sub))
                    return false;
            }
        } else {
            QFile::remove(dst);
            if (!QFile::copy(src, dst))
                return false;
        }
    }

    return true;
}

bool UpdateInstaller::rollbackFromBackup(const QString &backupPath)
{
    QDir backupDirObj(backupPath);
    if (!backupDirObj.exists()) return false;

    QStringList entries =
        backupDirObj.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QString src = backupPath + "/" + entry;
        QString dst = m_installDir + "/" + entry;

        if (QFileInfo(src).isDir()) {
            if (!QDir().mkpath(dst)) return false;
            QStringList subEntries = QDir(src).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &sub : subEntries) {
                QFile::remove(dst + "/" + sub);
                if (!QFile::copy(src + "/" + sub, dst + "/" + sub))
                    return false;
            }
        } else {
            QFile::remove(dst);
            if (!QFile::copy(src, dst))
                return false;
        }
    }

    return true;
}

void UpdateInstaller::cleanupAfterSuccess(const QString &stagingPath, const QString &bakDir)
{
    QDir(stagingPath).removeRecursively();
    QDir(bakDir).removeRecursively();
}

InstallResult UpdateInstaller::installFromZip(const QString &zipPath, const QString &updateVersion)
{
    InstallResult result;
    result.newExePath = m_installDir + "/LunarPlayer.exe";

    QString updatesDir = QFileInfo(zipPath).absolutePath();
    QString staging = stagingDir(updatesDir, updateVersion);
    QString backup = backupDir(updatesDir);

    // Step 1: Extract to staging
    if (!extractZipToStaging(zipPath, staging)) {
        result.errorString = "Failed to extract update package";
        emit installationFailed(result.errorString);
        return result;
    }

    // Step 2: Validate staging content
    if (!validateStaging(staging)) {
        QDir(staging).removeRecursively();
        result.errorString = "Staged update is missing LunarPlayer.exe";
        emit installationFailed(result.errorString);
        return result;
    }

    // Step 3: Backup current install
    if (!backupCurrentInstall(backup)) {
        QDir(staging).removeRecursively();
        result.errorString = "Failed to backup current installation";
        emit installationFailed(result.errorString);
        return result;
    }

    // Step 4: Swap files
    if (!swapFiles(staging, backup)) {
        // Rollback
        rollbackFromBackup(backup);
        QDir(staging).removeRecursively();
        QDir(backup).removeRecursively();
        result.errorString = "Failed to install update files; rolled back to previous version";
        result.rollbackPerformed = true;
        emit installationFailed(result.errorString);
        return result;
    }

    // Step 5: Success - cleanup
    cleanupAfterSuccess(staging, backup);
    result.success = true;
    return result;
}

void UpdateInstaller::cleanupOldUpdates(const QString &updatesDir, const QString &keepZip)
{
    QDir dir(updatesDir);
    if (!dir.exists()) return;

    QStringList entries = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (entry == "updates.log") continue;
        QString fullPath = updatesDir + "/" + entry;

        if (!keepZip.isEmpty() && fullPath == keepZip) continue;

        if (QFileInfo(fullPath).isDir()) {
            QDir(fullPath).removeRecursively();
        } else if (entry.endsWith(".zip") || entry.startsWith("staging_") ||
                   entry.startsWith("backup_")) {
            QFile::remove(fullPath);
        }
    }
}
