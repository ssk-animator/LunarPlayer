#include "ExternalSubtitleLoader.h"
#include "SubtitleDecoder.h"
#include "AssParser.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringConverter>
#include <QRegularExpression>
#include <QDebug>

extern "C" {
#include <libavformat/avformat.h>
}

ExternalSubtitleLoader::ExternalSubtitleLoader() = default;
ExternalSubtitleLoader::~ExternalSubtitleLoader() = default;

QStringList ExternalSubtitleLoader::supportedExtensions()
{
    return { "srt", "ass", "ssa", "vtt", "sub", "sup", "idx",
             "mpl2", "tmp", "txt", "rt" };
}

QStringList ExternalSubtitleLoader::scanDirectory(const QString &mediaPath)
{
    QFileInfo fi(mediaPath);
    QDir dir(fi.absolutePath());
    QString baseName = fi.completeBaseName();

    QStringList candidates;
    QStringList exts = supportedExtensions();

    QStringList baseNames;
    baseNames.append(baseName);
    int dotIdx = baseName.lastIndexOf('.');
    if (dotIdx > 0) {
        QString parentBase = baseName.left(dotIdx);
        if (!baseNames.contains(parentBase))
            baseNames.append(parentBase);
    }

    for (const QString &base : baseNames) {
        for (const QString &ext : exts) {
            QString pattern = base + "." + ext;
            QFileInfo candidate(dir.absoluteFilePath(pattern));
            if (candidate.exists())
                candidates.append(candidate.absoluteFilePath());
        }
    }
    return candidates;
}

QString ExternalSubtitleLoader::detectCodec(const QString &filePath)
{
    QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == "srt") return "srt";
    if (ext == "ass" || ext == "ssa") return "ass";
    if (ext == "vtt") return "vtt";
    if (ext == "sup") return "sup";
    if (ext == "idx" || ext == "sub") return "vobsub";
    if (ext == "mpl2" || ext == "tmp") return "sub";
    if (ext == "txt" || ext == "rt") return "txt";
    return "unknown";
}

QString ExternalSubtitleLoader::extractLanguage(const QString &fileName)
{
    QFileInfo fi(fileName);
    QString base = fi.completeBaseName();
    int dotIdx = base.lastIndexOf('.');
    if (dotIdx < 0) return QString();
    QString lang = base.mid(dotIdx + 1);
    static QStringList knownCodes = {
        "en","eng","english","fr","fre","fra","french","de","ger","deu","german",
        "es","spa","spanish","it","ita","italian","pt","por","portuguese",
        "ru","rus","russian","ja","jpn","japanese","ko","kor","korean",
        "zh","chi","zho","chinese","ar","ara","arabic","hi","hin","hindi",
        "nl","nld","dutch","pl","pol","polish","sv","swe","swedish",
        "da","dan","danish","fi","fin","finnish","no","nor","norwegian",
        "el","ell","greek","he","heb","hebrew","ro","ron","romanian",
        "hu","hun","hungarian","cs","ces","czech","tr","tur","turkish"
    };
    if (knownCodes.contains(lang.toLower()))
        return lang;
    return QString();
}

// ---- Text format parsers ----

QVector<SubtitleFrame> ExternalSubtitleLoader::parseSRT(const QString &filePath)
{
    QVector<SubtitleFrame> frames;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return frames;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    file.close();

    content.replace("\r\n", "\n");
    content.replace('\r', '\n');
    QStringList blocks = content.split("\n\n", Qt::SkipEmptyParts);
    QRegularExpression timeRx("(\\d{2}:\\d{2}:\\d{2}[,\\.]\\d{3})\\s*-->\\s*"
                               "(\\d{2}:\\d{2}:\\d{2}[,\\.]\\d{3})");

    for (const QString &block : blocks) {
        QStringList lines = block.split('\n', Qt::SkipEmptyParts);
        if (lines.size() < 3) continue;
        QRegularExpressionMatch timeMatch = timeRx.match(lines[1]);
        if (!timeMatch.hasMatch()) continue;

        SubtitleFrame frame;
        frame.type = SubtitleType::Text;
        frame.startSeconds = parseSRTTime(timeMatch.captured(1));
        frame.endSeconds = parseSRTTime(timeMatch.captured(2));
        frame.pts = (int64_t)(frame.startSeconds * 1000.0);
        frame.duration = (int64_t)((frame.endSeconds - frame.startSeconds) * 1000.0);

        QStringList textLines;
        for (int i = 2; i < lines.size(); ++i)
            textLines.append(lines[i].trimmed());
        QString text = textLines.join('\n');
        text.replace(QRegularExpression("<[^>]*>"), QString());
        frame.text = text;
        frames.append(frame);
    }
    return frames;
}

QVector<SubtitleFrame> ExternalSubtitleLoader::parseASS(const QString &filePath)
{
    AssParsedFile parsed = AssParser::parse(filePath);
    if (!parsed.valid || parsed.dialogues.isEmpty())
        return {};

    auto styleMap = AssParser::buildStyleMap(parsed.styles);

    QVector<SubtitleFrame> frames;
    frames.reserve(parsed.dialogues.size());
    for (const auto &dialogue : parsed.dialogues) {
        SubtitleFrame frame = AssParser::dialogueToFrame(dialogue, styleMap, 0);
        frame.subtitleIndex = frames.size();
        frames.append(std::move(frame));
    }
    return frames;
}

QVector<SubtitleFrame> ExternalSubtitleLoader::parseVTT(const QString &filePath)
{
    QVector<SubtitleFrame> frames;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return frames;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString content = in.readAll();
    file.close();

    QRegularExpression blockRx(
        "(\\d{2}:\\d{2}:\\d{2}\\.\\d{3})\\s*-->\\s*"
        "(\\d{2}:\\d{2}:\\d{2}\\.\\d{3}).*?\n"
        "((?:.*\n?)*?)(?=\n\\d{2}:|\\Z)",
        QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption
    );

    auto it = blockRx.globalMatch(content);
    while (it.hasNext()) {
        auto match = it.next();
        SubtitleFrame frame;
        frame.type = SubtitleType::Text;
        frame.startSeconds = parseVTTTime(match.captured(1));
        frame.endSeconds = parseVTTTime(match.captured(2));
        frame.pts = (int64_t)(frame.startSeconds * 1000.0);
        frame.duration = (int64_t)((frame.endSeconds - frame.startSeconds) * 1000.0);
        QString text = match.captured(3).trimmed();
        text.replace(QRegularExpression("<[^>]*>"), QString());
        frame.text = text;
        frames.append(frame);
    }
    return frames;
}

// ---- Bitmap format loading via FFmpeg ----

ExternalSubtitleTrack ExternalSubtitleLoader::loadBitmapFile(const QString &filePath)
{
    ExternalSubtitleTrack track;
    track.filePath = filePath;
    track.codecName = detectCodec(filePath);
    track.language = extractLanguage(QFileInfo(filePath).fileName());

    // Open the file with FFmpeg and decode all subtitle frames
    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, filePath.toUtf8().constData(), nullptr, nullptr) < 0) {
        track.errorMsg = "Failed to open bitmap subtitle file";
        return track;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        track.errorMsg = "Failed to find stream info";
        return track;
    }

    // Find the first subtitle stream
    int streamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            streamIdx = (int)i;
            break;
        }
    }
    if (streamIdx < 0) {
        avformat_close_input(&fmtCtx);
        track.errorMsg = "No subtitle stream found";
        return track;
    }

    SubtitleDecoder decoder;
    decoder.decodeSubtitles(fmtCtx, streamIdx, track.frames);
    avformat_close_input(&fmtCtx);

    track.loaded = !track.frames.isEmpty();
    if (track.frames.isEmpty())
        track.errorMsg = "No subtitle frames decoded";
    return track;
}

// ---- Top-level dispatch ----

ExternalSubtitleTrack ExternalSubtitleLoader::loadFile(const QString &filePath)
{
    ExternalSubtitleTrack track;
    track.filePath = filePath;
    track.codecName = detectCodec(filePath);
    track.language = extractLanguage(QFileInfo(filePath).fileName());

    if (track.codecName == "srt") {
        track.frames = parseSRT(filePath);
    } else if (track.codecName == "ass") {
        track.frames = parseASS(filePath);
    } else if (track.codecName == "vtt") {
        track.frames = parseVTT(filePath);
    } else if (track.codecName == "sup" || track.codecName == "vobsub") {
        return loadBitmapFile(filePath);
    } else {
        track.errorMsg = "Unsupported subtitle format: " + track.codecName;
        return track;
    }

    track.loaded = !track.frames.isEmpty();
    if (track.frames.isEmpty())
        track.errorMsg = "No subtitle entries found";
    return track;
}

// ---- Time helpers ----

double ExternalSubtitleLoader::parseSRTTime(const QString &timeStr)
{
    QRegularExpression rx("(\\d+):(\\d+):(\\d+)[,\\.](\\d+)");
    auto match = rx.match(timeStr);
    if (!match.hasMatch()) return 0.0;
    int h = match.captured(1).toInt();
    int m = match.captured(2).toInt();
    int s = match.captured(3).toInt();
    int ms = match.captured(4).toInt();
    return h * 3600.0 + m * 60.0 + s + ms / 1000.0;
}

double ExternalSubtitleLoader::parseVTTTime(const QString &timeStr)
{
    QRegularExpression rx("(\\d+):(\\d+):(\\d+)\\.(\\d+)");
    auto match = rx.match(timeStr);
    if (!match.hasMatch()) return 0.0;
    int h = match.captured(1).toInt();
    int m = match.captured(2).toInt();
    int s = match.captured(3).toInt();
    int ms = match.captured(4).toInt();
    return h * 3600.0 + m * 60.0 + s + ms / 1000.0;
}

double ExternalSubtitleLoader::parseASSTime(const QString &timeStr)
{
    QRegularExpression rx("(\\d+):(\\d+):(\\d+)\\.(\\d+)");
    auto match = rx.match(timeStr);
    if (!match.hasMatch()) return 0.0;
    int h = match.captured(1).toInt();
    int m = match.captured(2).toInt();
    int s = match.captured(3).toInt();
    int cs = match.captured(4).toInt();
    return h * 3600.0 + m * 60.0 + s + cs / 100.0;
}
