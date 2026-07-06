#ifndef EXTERNALSUBTITLELOADER_H
#define EXTERNALSUBTITLELOADER_H

#include "SubtitleFrame.h"
#include <QString>
#include <QStringList>
#include <QVector>

struct ExternalSubtitleTrack {
    QString filePath;
    QString language;
    QString codecName;
    QVector<SubtitleFrame> frames;
    bool loaded = false;
    QString errorMsg;
};

class ExternalSubtitleLoader {
public:
    ExternalSubtitleLoader();
    ~ExternalSubtitleLoader();

    static QStringList scanDirectory(const QString &mediaPath);
    static ExternalSubtitleTrack loadFile(const QString &filePath);

    // Text format parsers
    static QVector<SubtitleFrame> parseSRT(const QString &filePath);
    static QVector<SubtitleFrame> parseASS(const QString &filePath);
    static QVector<SubtitleFrame> parseVTT(const QString &filePath);

    // Bitmap format loading (via FFmpeg)
    static ExternalSubtitleTrack loadBitmapFile(const QString &filePath);

    static QString detectCodec(const QString &filePath);
    static QString extractLanguage(const QString &fileName);
    static QStringList supportedExtensions();

private:
    static double parseSRTTime(const QString &timeStr);
    static double parseVTTTime(const QString &timeStr);
    static double parseASSTime(const QString &timeStr);
};

#endif // EXTERNALSUBTITLELOADER_H
