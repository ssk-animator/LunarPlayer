#ifndef PACKAGEVERIFIER_H
#define PACKAGEVERIFIER_H

#include "IUpdateProvider.h"
#include <QObject>
#include <QString>

struct VerificationResult {
    bool passed = false;
    bool sha256Match = false;
    bool fileSizeMatch = false;
    bool versionValid = false;
    bool zipValid = false;
    QString errorString;
};

class PackageVerifier : public QObject {
    Q_OBJECT
public:
    explicit PackageVerifier(QObject *parent = nullptr);

    VerificationResult verify(const QString &filePath,
                              const ReleaseInfo &expected,
                              const QString &currentVersion);

    static QString computeSHA256(const QString &filePath);
    static bool isZipValid(const QString &filePath);
};

#endif
