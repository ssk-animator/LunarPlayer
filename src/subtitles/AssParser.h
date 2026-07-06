#ifndef ASSPARSER_H
#define ASSPARSER_H

#include "AssTypes.h"
#include "SubtitleFrame.h"
#include <QString>
#include <QVector>
#include <QHash>

struct AssDialogue {
    int layer = 0;
    double startTime = 0.0;
    double endTime = 0.0;
    QString styleName = "Default";
    QString actor;
    int marginL = 0;
    int marginR = 0;
    int marginV = 0;
    QString effect;
    QString rawText;
};

struct AssParsedFile {
    AssScriptInfo info;
    QVector<AssStyleDefinition> styles;
    QVector<AssDialogue> dialogues;
    bool valid = false;
    QString errorMsg;
};

class AssParser {
public:
    static AssParsedFile parse(const QString &filePath);
    static AssParsedFile parseFromData(const QString &data);

    // Convert a parsed ASS dialogue to a SubtitleFrame
    static SubtitleFrame dialogueToFrame(const AssDialogue &dialogue,
                                         const QHash<QString, AssStyleDefinition> &styleMap,
                                         int trackIndex = 0);

    // Build a style name → style definition lookup from parsed styles
    static QHash<QString, AssStyleDefinition> buildStyleMap(const QVector<AssStyleDefinition> &styles);

private:
    static void parseScriptInfo(const QString &line, AssScriptInfo &info);
    static AssStyleDefinition parseStyleLine(const QString &line, const QStringList &format);
    static AssDialogue parseDialogueLine(const QString &line, const QStringList &format);
    static double parseTimestamp(const QString &ts);
    static QStringList parseFormatLine(const QString &line);
    static uint32_t parseAssColor(const QString &assColor);
};

#endif // ASSPARSER_H
