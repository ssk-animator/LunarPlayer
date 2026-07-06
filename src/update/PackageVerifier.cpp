#include "PackageVerifier.h"
#include "SemVer.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <cstring>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

PackageVerifier::PackageVerifier(QObject *parent) : QObject(parent) {}

QString PackageVerifier::computeSHA256(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};

    return hash.result().toHex().toUpper();
}

bool PackageVerifier::isZipValid(const QString &filePath)
{
    mz_zip_archive archive;
    memset(&archive, 0, sizeof(archive));

    QByteArray pathBytes = filePath.toUtf8();
    if (!mz_zip_reader_init_file(&archive, pathBytes.constData(), 0))
        return false;

    mz_uint fileCount = mz_zip_reader_get_num_files(&archive);
    mz_bool valid = (fileCount > 0);
    mz_zip_reader_end(&archive);
    return valid;
}

VerificationResult PackageVerifier::verify(
    const QString &filePath, const ReleaseInfo &expected, const QString &currentVersion)
{
    VerificationResult result;

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        result.errorString = "Downloaded file not found: " + filePath;
        return result;
    }

    // File size check
    if (expected.downloadSize > 0 && fi.size() != expected.downloadSize) {
        result.fileSizeMatch = false;
        result.errorString = QString("File size mismatch: expected %1, got %2")
                                 .arg(expected.downloadSize)
                                 .arg(fi.size());
        return result;
    }
    result.fileSizeMatch = true;

    // ZIP structure check
    result.zipValid = isZipValid(filePath);
    if (!result.zipValid) {
        result.errorString = "Downloaded file is not a valid ZIP archive";
        return result;
    }

    // SHA-256 check
    if (!expected.sha256.isEmpty()) {
        QString actual = computeSHA256(filePath);
        result.sha256Match = (actual.toUpper() == expected.sha256.toUpper());
        if (!result.sha256Match) {
            result.errorString = QString("SHA-256 mismatch: expected %1, got %2")
                                     .arg(expected.sha256, actual);
            return result;
        }
    } else {
        result.sha256Match = true;
    }

    // Version format check
    SemVer remote = SemVer::fromString(expected.version);
    result.versionValid = remote.isValid();
    if (!result.versionValid) {
        result.errorString = "Invalid version format in release: " + expected.version;
        return result;
    }

    result.passed = true;
    return result;
}
