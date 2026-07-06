#include "AssParser.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

AssParsedFile AssParser::parse(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        AssParsedFile result;
        result.valid = false;
        result.errorMsg = QString("Cannot open file: %1").arg(filePath);
        return result;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QString data = stream.readAll();
    file.close();
    return parseFromData(data);
}

AssParsedFile AssParser::parseFromData(const QString &data)
{
    AssParsedFile result;
    result.valid = true;

    QStringList lines = data.split(QRegularExpression("\\r?\\n"));
    QString currentSection;
    QStringList styleFormat;
    QStringList eventFormat;

    auto parseColor = [](const QString &str) -> uint32_t {
        // ASS color format: &HAABBGGRR or &HBBGGRR (without alpha)
        QString c = str.trimmed();
        if (c.startsWith("&H", Qt::CaseInsensitive))
            c = c.mid(2);
        if (c.endsWith('&'))
            c.chop(1);
        // c is now AABBGGRR (8 hex) or BBGGRR (6 hex) or RGB (4 hex? unlikely)
        bool ok = false;
        if (c.length() == 8) {
            uint32_t abgr = c.toUInt(&ok, 16);
            if (ok) {
                uint8_t a = (abgr >> 24) & 0xFF;
                uint8_t b = (abgr >> 16) & 0xFF;
                uint8_t g = (abgr >> 8) & 0xFF;
                uint8_t r = abgr & 0xFF;
                return (uint32_t(qRgba(r, g, b, 255 - a)));
            }
        } else if (c.length() == 6) {
            uint32_t bgr = c.toUInt(&ok, 16);
            if (ok) {
                uint8_t b = (bgr >> 16) & 0xFF;
                uint8_t g = (bgr >> 8) & 0xFF;
                uint8_t r = bgr & 0xFF;
                return (uint32_t(qRgb(r, g, b)));
            }
        }
        return 0x00FFFFFF;
    };

    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) continue;

        // Skip comments
        if (line.startsWith(';') || line.startsWith('!'))
            continue;

        // Section headers
        if (line.startsWith('[') && line.endsWith(']')) {
            currentSection = line.mid(1, line.length() - 2).trimmed();
            continue;
        }

        if (currentSection.compare("Script Info", Qt::CaseInsensitive) == 0) {
            int colon = line.indexOf(':');
            if (colon > 0) {
                QString key = line.left(colon).trimmed();
                QString value = line.mid(colon + 1).trimmed();
                if (key.compare("PlayResX", Qt::CaseInsensitive) == 0)
                    result.info.playRes.setWidth(value.toInt());
                else if (key.compare("PlayResY", Qt::CaseInsensitive) == 0)
                    result.info.playRes.setHeight(value.toInt());
                else if (key.compare("Title", Qt::CaseInsensitive) == 0)
                    result.info.title = value;
                else if (key.compare("ScriptType", Qt::CaseInsensitive) == 0)
                    result.info.scriptType = value;
                else if (key.compare("WrapStyle", Qt::CaseInsensitive) == 0)
                    result.info.wrapStyle = value;
                else if (key.compare("Collisions", Qt::CaseInsensitive) == 0)
                    result.info.collisions = value;
            }
        }

        if (currentSection.compare("V4+ Styles", Qt::CaseInsensitive) == 0
            || currentSection.compare("V4 Styles", Qt::CaseInsensitive) == 0)
        {
            if (line.startsWith("Format:", Qt::CaseInsensitive)) {
                styleFormat = parseFormatLine(line);
            } else if (line.startsWith("Style:", Qt::CaseInsensitive)) {
                if (!styleFormat.isEmpty()) {
                    AssStyleDefinition style = parseStyleLine(line, styleFormat);
                    style.colors.primary = QColor::fromRgba(parseColor("&H00FFFFFF"));
                    style.colors.secondary = QColor::fromRgba(parseColor("&H000000FF"));
                    style.colors.outline = QColor::fromRgba(parseColor("&H00000000"));
                    style.colors.shadow = QColor::fromRgba(parseColor("&H00000000"));
                    result.styles.append(style);
                }
            }
        }

        if (currentSection.compare("Events", Qt::CaseInsensitive) == 0) {
            if (line.startsWith("Format:", Qt::CaseInsensitive)) {
                eventFormat = parseFormatLine(line);
            } else if (line.startsWith("Dialogue:", Qt::CaseInsensitive)
                       || line.startsWith("Comment:", Qt::CaseInsensitive)) {
                if (!eventFormat.isEmpty() && line.startsWith("Dialogue:", Qt::CaseInsensitive)) {
                    AssDialogue d = parseDialogueLine(line, eventFormat);
                    result.dialogues.append(d);
                }
            }
        }
    }

    if (result.styles.isEmpty()) {
        AssStyleDefinition def;
        def.name = "Default";
        result.styles.append(def);
    }

    return result;
}

QStringList AssParser::parseFormatLine(const QString &line)
{
    int colon = line.indexOf(':');
    if (colon < 0) return {};
    QString body = line.mid(colon + 1).trimmed();
    QStringList parts = body.split(',');
    for (auto &p : parts) p = p.trimmed();
    return parts;
}

AssStyleDefinition AssParser::parseStyleLine(const QString &line, const QStringList &format)
{
    int colon = line.indexOf(':');
    QString body = line.mid(colon + 1).trimmed();

    // Split handling commas inside quoted strings
    QStringList values;
    QString current;
    bool inQuote = false;
    for (int i = 0; i < body.size(); ++i) {
        if (body[i] == '"') { inQuote = !inQuote; continue; }
        if (body[i] == ',' && !inQuote) {
            values.append(current.trimmed());
            current.clear();
        } else {
            current += body[i];
        }
    }
    values.append(current.trimmed());

    auto get = [&](int idx, const QString &def) -> QString {
        return idx < values.size() ? values[idx] : def;
    };

    AssStyleDefinition style;
    for (int i = 0; i < format.size() && i < values.size(); ++i) {
        const QString &key = format[i];
        const QString &val = values[i];
        if (key.compare("Name", Qt::CaseInsensitive) == 0)
            style.name = val;
        else if (key.compare("Fontname", Qt::CaseInsensitive) == 0)
            style.font.family = val;
        else if (key.compare("Fontsize", Qt::CaseInsensitive) == 0)
            style.font.size = val.toInt();
        else if (key.compare("PrimaryColour", Qt::CaseInsensitive) == 0)
            style.colors.primary = QColor::fromRgba(parseAssColor(val));
        else if (key.compare("SecondaryColour", Qt::CaseInsensitive) == 0)
            style.colors.secondary = QColor::fromRgba(parseAssColor(val));
        else if (key.compare("OutlineColour", Qt::CaseInsensitive) == 0)
            style.colors.outline = QColor::fromRgba(parseAssColor(val));
        else if (key.compare("BackColour", Qt::CaseInsensitive) == 0)
            style.colors.shadow = QColor::fromRgba(parseAssColor(val));
        else if (key.compare("Bold", Qt::CaseInsensitive) == 0)
            style.font.bold = val.toInt() != 0;
        else if (key.compare("Italic", Qt::CaseInsensitive) == 0)
            style.font.italic = val.toInt() != 0;
        else if (key.compare("Underline", Qt::CaseInsensitive) == 0)
            style.font.underline = val.toInt() != 0;
        else if (key.compare("StrikeOut", Qt::CaseInsensitive) == 0)
            style.font.strikethrough = val.toInt() != 0;
        else if (key.compare("ScaleX", Qt::CaseInsensitive) == 0)
            style.font.scaleX = val.toDouble();
        else if (key.compare("ScaleY", Qt::CaseInsensitive) == 0)
            style.font.scaleY = val.toDouble();
        else if (key.compare("Spacing", Qt::CaseInsensitive) == 0)
            style.font.spacing = val.toDouble();
        else if (key.compare("Angle", Qt::CaseInsensitive) == 0)
            style.font.angle = val.toDouble();
        else if (key.compare("BorderStyle", Qt::CaseInsensitive) == 0)
            style.outlineShadow.borderStyle = val.toInt();
        else if (key.compare("Outline", Qt::CaseInsensitive) == 0)
            style.outlineShadow.outlineWidth = val.toDouble();
        else if (key.compare("Shadow", Qt::CaseInsensitive) == 0)
            style.outlineShadow.shadowY = val.toDouble();
        else if (key.compare("Alignment", Qt::CaseInsensitive) == 0)
            style.position.alignment = val.toInt();
        else if (key.compare("MarginL", Qt::CaseInsensitive) == 0)
            style.position.marginL = val.toInt();
        else if (key.compare("MarginR", Qt::CaseInsensitive) == 0)
            style.position.marginR = val.toInt();
        else if (key.compare("MarginV", Qt::CaseInsensitive) == 0)
            style.position.marginV = val.toInt();
        else if (key.compare("Encoding", Qt::CaseInsensitive) == 0)
            style.font.encoding = val.toInt();
    }
    return style;
}

AssDialogue AssParser::parseDialogueLine(const QString &line, const QStringList &format)
{
    int colon = line.indexOf(':');
    QString body = line.mid(colon + 1).trimmed();

    QStringList values;
    QString current;
    bool inQuote = false;
    for (int i = 0; i < body.size(); ++i) {
        if (body[i] == '"') { inQuote = !inQuote; continue; }
        if (body[i] == ',' && !inQuote) {
            values.append(current.trimmed());
            current.clear();
        } else {
            current += body[i];
        }
    }
    values.append(current);

    auto get = [&](int idx, const QString &def) -> QString {
        return idx < values.size() ? values[idx] : def;
    };

    AssDialogue d;
    for (int i = 0; i < format.size(); ++i) {
        const QString &key = format[i];
        const QString &val = get(i, "");
        if (key.compare("Layer", Qt::CaseInsensitive) == 0)
            d.layer = val.toInt();
        else if (key.compare("Start", Qt::CaseInsensitive) == 0)
            d.startTime = parseTimestamp(val);
        else if (key.compare("End", Qt::CaseInsensitive) == 0)
            d.endTime = parseTimestamp(val);
        else if (key.compare("Style", Qt::CaseInsensitive) == 0)
            d.styleName = val;
        else if (key.compare("Name", Qt::CaseInsensitive) == 0)
            d.actor = val;
        else if (key.compare("MarginL", Qt::CaseInsensitive) == 0)
            d.marginL = val.toInt();
        else if (key.compare("MarginR", Qt::CaseInsensitive) == 0)
            d.marginR = val.toInt();
        else if (key.compare("MarginV", Qt::CaseInsensitive) == 0)
            d.marginV = val.toInt();
        else if (key.compare("Effect", Qt::CaseInsensitive) == 0)
            d.effect = val;
    }

    // The last field is always Text — it may contain commas
    int textFieldIndex = format.size() - 1;
    if (textFieldIndex >= 0 && format[textFieldIndex].compare("Text", Qt::CaseInsensitive) == 0) {
        // Reconstruct text from remaining values
        QStringList textParts;
        for (int i = textFieldIndex; i < values.size(); ++i)
            textParts.append(values[i]);
        d.rawText = textParts.join(",");
    }

    return d;
}

double AssParser::parseTimestamp(const QString &ts)
{
    // Formats: H:MM:SS.cc or H:MM:SS.cc or 0:00:00.00
    static QRegularExpression re(R"(^\s*(\d+):(\d+):(\d+)[.:](\d+)\s*$)");
    auto m = re.match(ts);
    if (!m.hasMatch()) {
        static QRegularExpression re2(R"(^\s*(\d+):(\d+)[.:](\d+)\s*$)");
        auto m2 = re2.match(ts);
        if (!m2.hasMatch()) return 0.0;
        int mins = m2.captured(1).toInt();
        int secs = m2.captured(2).toInt();
        int cs = m2.captured(3).toInt();
        return mins * 60.0 + secs + cs / 100.0;
    }
    int hours = m.captured(1).toInt();
    int mins = m.captured(2).toInt();
    int secs = m.captured(3).toInt();
    int cs = m.captured(4).toInt();
    return hours * 3600.0 + mins * 60.0 + secs + cs / 100.0;
}

QHash<QString, AssStyleDefinition> AssParser::buildStyleMap(const QVector<AssStyleDefinition> &styles)
{
    QHash<QString, AssStyleDefinition> map;
    for (const auto &s : styles)
        map.insert(s.name, s);
    return map;
}

SubtitleFrame AssParser::dialogueToFrame(const AssDialogue &dialogue,
                                          const QHash<QString, AssStyleDefinition> &styleMap,
                                          int trackIndex)
{
    SubtitleFrame frame;
    frame.type = SubtitleType::ASS;
    frame.text = dialogue.rawText;
    frame.pts = static_cast<int64_t>(dialogue.startTime * 1000);
    frame.duration = static_cast<int64_t>((dialogue.endTime - dialogue.startTime) * 1000);
    frame.startSeconds = dialogue.startTime;
    frame.endSeconds = dialogue.endTime;
    frame.trackIndex = trackIndex;
    frame.layer = dialogue.layer;

    // Resolve named style
    frame.styleName = dialogue.styleName;
    auto it = styleMap.find(dialogue.styleName);
    if (it != styleMap.end()) {
        const AssStyleDefinition &s = it.value();
        frame.style.fontFamily = s.font.family;
        frame.style.fontSize = s.font.size;
        frame.style.bold = s.font.bold;
        frame.style.italic = s.font.italic;
        frame.style.underline = s.font.underline;
        frame.style.strikethrough = s.font.strikethrough;
        frame.style.color = s.colors.primary;
        frame.style.outlineColor = s.colors.outline;
        frame.style.shadowColor = s.colors.shadow;
        frame.style.outlineWidth = static_cast<int>(s.outlineShadow.outlineWidth);
        frame.style.shadowOffset = static_cast<int>(s.outlineShadow.shadowY);
        frame.style.bottomMargin = s.position.marginV;
        frame.style.safeMarginX = s.position.marginL;
        frame.style.safeMarginY = s.position.marginR;
        frame.style.alignment = s.position.alignment;
    }

    // Apply dialogue-level margin overrides
    if (dialogue.marginL != 0) frame.style.safeMarginX = dialogue.marginL;
    if (dialogue.marginR != 0) frame.style.safeMarginY = dialogue.marginR;
    if (dialogue.marginV != 0) frame.style.bottomMargin = dialogue.marginV;

    return frame;
}

uint32_t AssParser::parseAssColor(const QString &assColor)
{
    QString c = assColor.trimmed();
    if (c.startsWith("&H", Qt::CaseInsensitive))
        c = c.mid(2);
    if (c.endsWith('&'))
        c.chop(1);
    bool ok = false;
    if (c.length() == 8) {
        uint32_t abgr = c.toUInt(&ok, 16);
        if (ok) {
            uint8_t a = (abgr >> 24) & 0xFF;
            uint8_t b = (abgr >> 16) & 0xFF;
            uint8_t g = (abgr >> 8) & 0xFF;
            uint8_t r = abgr & 0xFF;
            return qRgba(r, g, b, 255 - a);
        }
    } else if (c.length() == 6) {
        uint32_t bgr = c.toUInt(&ok, 16);
        if (ok) {
            uint8_t b = (bgr >> 16) & 0xFF;
            uint8_t g = (bgr >> 8) & 0xFF;
            uint8_t r = bgr & 0xFF;
            return qRgb(r, g, b);
        }
    }
    return 0x00FFFFFF;
}
