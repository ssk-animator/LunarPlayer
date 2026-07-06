#include "AssOverrideParser.h"
#include <QRegularExpression>
#include <QtMath>

static uint32_t parseAssColorValue(const QString &c)
{
    QString s = c.trimmed();
    if (s.startsWith("&H", Qt::CaseInsensitive)) s = s.mid(2);
    if (s.endsWith('&')) s.chop(1);
    bool ok = false;
    if (s.length() == 8) {
        uint32_t abgr = s.toUInt(&ok, 16);
        if (ok) {
            uint8_t a = (abgr >> 24) & 0xFF;
            uint8_t b = (abgr >> 16) & 0xFF;
            uint8_t g = (abgr >> 8) & 0xFF;
            uint8_t r = abgr & 0xFF;
            return qRgba(r, g, b, 255 - a);
        }
    } else if (s.length() == 6) {
        uint32_t bgr = s.toUInt(&ok, 16);
        if (ok) {
            uint8_t b = (bgr >> 16) & 0xFF;
            uint8_t g = (bgr >> 8) & 0xFF;
            uint8_t r = bgr & 0xFF;
            return qRgb(r, g, b);
        }
    }
    return 0x00FFFFFF;
}

// ---- AssOverrideParser ----

QVector<AssSegmentedLine> AssOverrideParser::segment(
    const QString &assText,
    const SubtitleFrame &frame,
    const SubtitleRenderContext &ctx,
    const QHash<QString, AssStyleDefinition> &styleMap)
{
    QVector<AssSegmentedLine> lines;
    lines.append(AssSegmentedLine());

    OverrideState state;
    // Look up by style name (not font family) to get full style definition
    auto it = styleMap.constFind(frame.styleName.isEmpty()
                                 ? frame.style.fontFamily
                                 : frame.styleName);
    if (it != styleMap.constEnd()) {
        state.font = it->font;
        state.colors = it->colors;
        state.outlineShadow = it->outlineShadow;
        state.position = it->position;
    } else {
        state.font.family = frame.style.fontFamily;
        state.font.size = frame.style.fontSize;
        state.font.bold = frame.style.bold;
        state.font.italic = frame.style.italic;
        state.font.underline = frame.style.underline;
        state.font.strikethrough = frame.style.strikethrough;
        state.colors.primary = frame.style.color;
        state.colors.outline = frame.style.outlineColor;
        state.colors.shadow = frame.style.shadowColor;
        state.outlineShadow.outlineWidth = static_cast<double>(frame.style.outlineWidth);
        state.outlineShadow.shadowY = static_cast<double>(frame.style.shadowOffset);
        state.position.alignment = frame.style.alignment;
        state.position.marginL = frame.style.safeMarginX;
        state.position.marginV = frame.style.safeMarginY;
    }

    int karaokeConsumedStart = 0;
    int karaokeConsumed = 0;

    auto flushLineKaraoke = [&]() {
        if (karaokeConsumed > karaokeConsumedStart && !lines.isEmpty()) {
            QVector<KaraokeSyllable> lineKaraoke;
            int end = qMin(karaokeConsumed, state.karaoke.size());
            for (int ki = karaokeConsumedStart; ki < end; ++ki)
                lineKaraoke.append(state.karaoke[ki]);
            lines.last().karaokeSyllables = lineKaraoke;
        }
        karaokeConsumedStart = karaokeConsumed;
    };

    auto emitSegment = [&](const QString &text) {
        if (text.isEmpty()) return;
        AssTextSegment seg;
        seg.text = text;
        seg.font = QFont(state.font.family, qMax(1, state.font.size));
        seg.font.setBold(state.font.bold);
        seg.font.setItalic(state.font.italic);
        seg.font.setUnderline(state.font.underline);
        seg.font.setStrikeOut(state.font.strikethrough);
        if (!qFuzzyIsNull(state.font.spacing))
            seg.font.setLetterSpacing(QFont::AbsoluteSpacing, state.font.spacing);
        seg.primaryColor = state.colors.primary;
        seg.outlineColor = state.colors.outline;
        seg.shadowColor = state.colors.shadow;
        seg.secondaryColor = state.colors.secondary;
        if (karaokeConsumed < state.karaoke.size()) {
            seg.karaokeIndex = karaokeConsumed;
            seg.karaokeDuration = state.karaoke[karaokeConsumed].duration;
            ++karaokeConsumed;
        }
        seg.outlineWidth = state.outlineShadow.outlineWidth;
        seg.shadowOffset = QPointF(state.outlineShadow.shadowX, state.outlineShadow.shadowY);
        seg.opacity = 1.0;
        seg.blur = state.outlineShadow.blur;
        if (state.position.hasPos) {
            seg.hasExplicitPos = true;
            seg.position = QPointF(state.position.posX, state.position.posY);
        }
        seg.scale = QPointF(state.scaleX / 100.0, state.scaleY / 100.0);
        seg.tracking = state.tracking;

        // Animation data
        seg.hasMove = state.hasMove;
        seg.moveData = state.moveData;
        seg.rotationX = state.rotationX;
        seg.rotationY = state.rotationY;
        seg.rotationZ = state.rotationZ;
        seg.overrideAlpha = state.overrideAlpha;
        seg.fadeData = state.fadeData;
        seg.transforms = state.transforms;

        // Clip data
        seg.hasClip = state.clip.hasClip;
        seg.inverseClip = state.clip.inverse;
        if (state.clip.isRect) {
            seg.clipRect = state.clip.rect;
        } else {
            seg.clipPath = state.clip.path;
        }

        if (state.drawingMode) {
            seg.isDrawing = true;
            seg.drawPath = parseDrawingCommands(text, state.drawScale);
        }
        lines.last().segments.append(seg);
    };

    // Walk through text character by character
    bool inBlock = false;
    QString blockContent;
    QString running;

    for (int i = 0; i < assText.size(); ++i) {
        QChar c = assText[i];

        if (inBlock) {
            if (c == '}') {
                inBlock = false;
                if (!blockContent.isEmpty()) {
                    OverrideState updated = parseOverrideBlock(blockContent, state, state.font, ctx.playRes);
                    state.font = updated.font;
                    state.colors = updated.colors;
                    state.outlineShadow = updated.outlineShadow;
                    state.position = updated.position;
                    state.drawingMode = updated.drawingMode;
                    state.drawScale = updated.drawScale;
                    state.drawPath = updated.drawPath;
                    if (!updated.karaoke.isEmpty()) state.karaoke = updated.karaoke;
                    if (!updated.transforms.isEmpty()) state.transforms = updated.transforms;
                    if (updated.clip.hasClip) state.clip = updated.clip;
                    if (updated.fadeIn > 0.0) state.fadeIn = updated.fadeIn;
                    if (updated.fadeOut > 0.0) state.fadeOut = updated.fadeOut;
                    state.rotationX = updated.rotationX;
                    state.rotationY = updated.rotationY;
                    state.rotationZ = updated.rotationZ;
                    if (updated.overrideAlpha >= 0) state.overrideAlpha = updated.overrideAlpha;
                    state.fadeData = updated.fadeData;
                    if (updated.hasMove) { state.hasMove = updated.hasMove; state.moveData = updated.moveData; }
                    state.scaleX = updated.scaleX;
                    state.scaleY = updated.scaleY;
                    state.tracking = updated.tracking;
                }
                blockContent.clear();
            } else {
                blockContent += c;
            }
            continue;
        }

        if (c == '{') {
            if (!running.isEmpty()) { emitSegment(running); running.clear(); }
            inBlock = true;
            blockContent.clear();
            continue;
        }

        if (c == '\\' && i + 1 < assText.size()) {
            QChar next = assText[i + 1];
            if (next == 'N' || next == 'n') {
                if (!running.isEmpty()) { emitSegment(running); running.clear(); }
                flushLineKaraoke();
                lines.append(AssSegmentedLine());
                ++i;
                continue;
            }
            if (next == 'h') {
                running += QChar(0x00A0);
                ++i;
                continue;
            }
        }

        running += c;
    }

    if (!running.isEmpty()) emitSegment(running);
    flushLineKaraoke();

    // Remove empty trailing lines
    while (lines.size() > 1 && lines.last().segments.isEmpty())
        lines.removeLast();

    return lines;
}

AssOverrideParser::OverrideState AssOverrideParser::parseOverrideBlock(
    const QString &block,
    const OverrideState &current,
    const FontStyle &baseFont,
    const QSize &playRes)
{
    OverrideState state = current;

    // Split block into individual tags separated by backslash
    int i = 0;
    while (i < block.size()) {
        if (block[i] == '\\') { ++i; continue; }
        int start = i;
        while (i < block.size() && block[i] != '\\') {
            if (block[i] == '(') {
                int depth = 1;
                ++i;
                while (i < block.size() && depth > 0) {
                    if (block[i] == '(') ++depth;
                    else if (block[i] == ')') --depth;
                    ++i;
                }
            } else {
                ++i;
            }
        }
        QString tag = block.mid(start, i - start).trimmed();
        if (!tag.isEmpty()) applyTag(tag, state, baseFont, playRes);
    }

    return state;
}

void AssOverrideParser::applyTag(const QString &tag, OverrideState &state,
                                  const FontStyle &baseFont, const QSize &playRes)
{
    if (tag.isEmpty()) return;

    // Parse name and parameter
    QString name;
    QString param;

    // Split at the boundary between alpha chars and rest
    int nameEnd = 0;
    while (nameEnd < tag.size() && tag[nameEnd].isLetter()) ++nameEnd;
    // Handle cases like "1c", "2a", "3c" where name starts with digit
    if (nameEnd == 0 && tag.size() > 1 && tag[0].isDigit() && tag[1].isLetter()) {
        nameEnd = 2;
        while (nameEnd < tag.size() && tag[nameEnd].isLetter()) ++nameEnd;
    }
    name = tag.left(nameEnd);
    param = tag.mid(nameEnd);

    auto intVal = [](const QString &s, int def) -> int {
        bool ok = false;
        int v = s.toInt(&ok);
        return ok ? v : def;
    };

    auto doubleVal = [](const QString &s, double def) -> double {
        bool ok = false;
        double v = s.toDouble(&ok);
        return ok ? v : def;
    };

    // Parse parenthesized parameters: (x,y) or (x1,y1,x2,y2,...)
    QStringList params;
    if (param.startsWith('(') && param.endsWith(')')) {
        QString inner = param.mid(1, param.size() - 2);
        int depth = 0;
        QString cur;
        for (int i = 0; i < inner.size(); ++i) {
            if (inner[i] == '(') { ++depth; cur += inner[i]; }
            else if (inner[i] == ')') { --depth; cur += inner[i]; }
            else if (inner[i] == ',' && depth == 0) {
                params.append(cur.trimmed());
                cur.clear();
            } else {
                cur += inner[i];
            }
        }
        if (!cur.isEmpty()) params.append(cur.trimmed());
    } else if (!param.isEmpty()) {
        params.append(param.trimmed());
    }

    // Apply tag
    if (name == "b") {
        int v = intVal(param, -1);
        if (v == 0) state.font.bold = false;
        else if (v == 1) state.font.bold = true;
        else state.font.bold = !state.font.bold;

    } else if (name == "i") {
        int v = intVal(param, -1);
        if (v == 0) state.font.italic = false;
        else if (v == 1) state.font.italic = true;
        else state.font.italic = !state.font.italic;

    } else if (name == "u") {
        int v = intVal(param, -1);
        if (v == 0) state.font.underline = false;
        else if (v == 1) state.font.underline = true;
        else state.font.underline = !state.font.underline;

    } else if (name == "s") {
        int v = intVal(param, -1);
        if (v == 0) state.font.strikethrough = false;
        else if (v == 1) state.font.strikethrough = true;
        else state.font.strikethrough = !state.font.strikethrough;

    } else if (name == "fn") {
        if (!param.isEmpty()) state.font.family = param;

    } else if (name == "fs") {
        if (param.startsWith('+') || param.startsWith('-'))
            state.font.size = qMax(1, state.font.size + intVal(param, 0));
        else {
            int v = intVal(param, 0);
            if (v > 0) state.font.size = v;
        }

    } else if (name == "fscx") {
        state.font.scaleX = doubleVal(param, 100.0);
        state.scaleX = state.font.scaleX;

    } else if (name == "fscy") {
        state.font.scaleY = doubleVal(param, 100.0);
        state.scaleY = state.font.scaleY;

    } else if (name == "fsp") {
        state.font.spacing = doubleVal(param, 0.0);
        state.tracking = state.font.spacing;

    } else if (name == "c" || name == "1c") {
        state.colors.primary = QColor::fromRgba(parseAssColorValue(param));

    } else if (name == "2c") {
        state.colors.secondary = QColor::fromRgba(parseAssColorValue(param));

    } else if (name == "3c") {
        state.colors.outline = QColor::fromRgba(parseAssColorValue(param));

    } else if (name == "4c") {
        state.colors.shadow = QColor::fromRgba(parseAssColorValue(param));

    } else if (name == "alpha" || name == "1a") {
        int v = intVal(param, 0);
        state.colors.primary.setAlpha(qBound(0, 255 - v, 255));
        state.overrideAlpha = qBound(0, 255 - v, 255);

    } else if (name == "2a") {
        int v = intVal(param, 0);
        state.colors.secondary.setAlpha(qBound(0, 255 - v, 255));

    } else if (name == "3a") {
        int v = intVal(param, 0);
        state.colors.outline.setAlpha(qBound(0, 255 - v, 255));

    } else if (name == "4a") {
        int v = intVal(param, 0);
        state.colors.shadow.setAlpha(qBound(0, 255 - v, 255));

    } else if (name == "bord") {
        state.outlineShadow.outlineWidth = doubleVal(param, 2.0);

    } else if (name == "shad") {
        if (params.size() >= 2) {
            state.outlineShadow.shadowX = doubleVal(params[0], 0.0);
            state.outlineShadow.shadowY = doubleVal(params[1], 0.0);
        } else {
            double v = doubleVal(param, 2.0);
            state.outlineShadow.shadowX = v;
            state.outlineShadow.shadowY = v;
        }

    } else if (name == "be") {
        state.outlineShadow.blur = static_cast<double>(intVal(param, 0));

    } else if (name == "blur") {
        state.outlineShadow.blur = doubleVal(param, 0.0);

    } else if (name == "frx") {
        state.rotationX = doubleVal(param, 0.0);

    } else if (name == "fry") {
        state.rotationY = doubleVal(param, 0.0);

    } else if (name == "frz") {
        state.rotationZ = doubleVal(param, 0.0);

    } else if (name == "pos" && params.size() >= 2) {
        state.position.hasPos = true;
        state.position.posX = doubleVal(params[0], 0.0);
        state.position.posY = doubleVal(params[1], 0.0);

    } else if (name == "move") {
        if (params.size() >= 4) {
            state.hasMove = true;
            state.moveData.from = QPointF(doubleVal(params[0], 0.0),
                                          doubleVal(params[1], 0.0));
            state.moveData.to = QPointF(doubleVal(params[2], 0.0),
                                        doubleVal(params[3], 0.0));
            state.moveData.t1 = params.size() >= 5 ? doubleVal(params[4], 0.0) : 0.0;
            state.moveData.t2 = params.size() >= 6 ? doubleVal(params[5], 0.0) : 0.0;
            // Use initial position as start of move
            state.position.hasPos = true;
            state.position.posX = state.moveData.from.x();
            state.position.posY = state.moveData.from.y();
        }

    } else if (name == "t") {
        // \t([<accel>,]<t1>,<t2>,<style modifiers>)
        double accel = 1.0;
        double t1, t2;
        int idx = 0;
        // Try to parse accel if present (starts with digit)
        bool hasAccel = false;
        if (!params.isEmpty() && !params[0].isEmpty()) {
            // Determine if first param is accel (fractional or integer < 1) or t1
            double first = doubleVal(params[0], -1.0);
            if (first > 0.0 && first < 1.0) {
                accel = first;
                hasAccel = true;
                idx = 1;
            }
        }
        // Parse t1, t2
        if (params.size() > idx) t1 = doubleVal(params[idx], 0.0); else t1 = 0.0;
        if (params.size() > idx + 1) t2 = doubleVal(params[idx + 1], 0.0); else t2 = 0.0;
        idx += hasAccel ? 3 : 2;
        // Remaining params are style modifiers - parse them
        QVector<AssTransform::Target> targets;
        for (int ti = idx; ti < params.size(); ++ti) {
            QString subTag = params[ti].trimmed();
            if (subTag.isEmpty()) continue;
            // Parse each modifier: e.g. \fscx150\fscy200 or \c&HFF0000&\bord5
            int si = 0;
            while (si < subTag.size()) {
                if (subTag[si] == '\\') { ++si; continue; }
                int tagStart = si;
                while (si < subTag.size() && subTag[si] != '\\') ++si;
                QString tTag = subTag.mid(tagStart, si - tagStart).trimmed();
                if (tTag.isEmpty()) continue;
                // Parse the tag name and value
                int nEnd = 0;
                while (nEnd < tTag.size() && tTag[nEnd].isLetter()) ++nEnd;
                if (nEnd == 0 && tTag.size() > 1 && tTag[0].isDigit() && tTag[1].isLetter())
                    nEnd = 2;
                QString tName = tTag.left(nEnd);
                QString tVal = tTag.mid(nEnd);
                AssTransform::Target target;
                target.params.append(doubleVal(tVal, 0.0));
                // Known targets
                if (tName == "fscx")       target.type = AssTransform::Target::Fscx;
                else if (tName == "fscy")  target.type = AssTransform::Target::Fscy;
                else if (tName == "frx")   target.type = AssTransform::Target::Frx;
                else if (tName == "fry")   target.type = AssTransform::Target::Fry;
                else if (tName == "frz")   target.type = AssTransform::Target::Frz;
                else if (tName == "bord")  target.type = AssTransform::Target::Bord;
                else if (tName == "shad")  target.type = AssTransform::Target::Shad;
                else if (tName == "alpha") target.type = AssTransform::Target::Alpha;
                else if (tName == "fs")    target.type = AssTransform::Target::Fs;
                else if (tName == "fsp")   target.type = AssTransform::Target::Fsp;
                else if (tName == "fn")    target.type = AssTransform::Target::Fn;
                else if (tName == "c" || tName == "1c")
                    target.type = AssTransform::Target::C1;
                else if (tName == "2c")    target.type = AssTransform::Target::C2;
                else if (tName == "3c")    target.type = AssTransform::Target::C3;
                else if (tName == "4c")    target.type = AssTransform::Target::C4;
                else if (tName == "1a")    target.type = AssTransform::Target::A1;
                else if (tName == "2a")    target.type = AssTransform::Target::A2;
                else if (tName == "3a")    target.type = AssTransform::Target::A3;
                else if (tName == "4a")    target.type = AssTransform::Target::A4;
                else continue; // unsupported target
                targets.append(target);
            }
        }
        if (!targets.isEmpty()) {
            AssTransform xf;
            xf.accel = accel;
            xf.t1 = t1;
            xf.t2 = t2;
            xf.targets = targets;
            state.transforms.append(xf);
        }

    } else if (name == "clip" || name == "iclip") {
        bool inverse = (name == "iclip");
        if (params.size() >= 4) {
            // Rect clip: \clip(x1,y1,x2,y2) — 4 numeric params
            bool allNumeric = true;
            for (int pi = 0; pi < 4 && pi < params.size(); ++pi) {
                bool ok;
                params[pi].toDouble(&ok);
                if (!ok) { allNumeric = false; break; }
            }
            if (allNumeric && params.size() == 4) {
                double x1 = doubleVal(params[0], 0.0);
                double y1 = doubleVal(params[1], 0.0);
                double x2 = doubleVal(params[2], 0.0);
                double y2 = doubleVal(params[3], 0.0);
                state.clip.rect = QRectF(x1, y1, x2 - x1, y2 - y1);
                state.clip.isRect = true;
                state.clip.hasClip = true;
                state.clip.inverse = inverse;
            } else {
                // Vector clip — store scale + raw coords for later parsing
                // Full vector drawing parser in Phase 7C.7
                double scale = 1.0;
                int startIdx = 0;
                bool ok;
                params[0].toDouble(&ok);
                if (ok) { scale = doubleVal(params[0], 1.0); startIdx = 1; }
                QPainterPath clipPath;
                double sx = scale;
                double sy = scale;
                double cx = 0, cy = 0;
                double lastCX = 0, lastCY = 0;
                for (int pi = startIdx; pi < params.size() - 1; ) {
                    QString cmd = params[pi].trimmed().toLower();
                    if (cmd == "m" && pi + 2 < params.size()) {
                        cx = doubleVal(params[pi+1], 0.0) * sx;
                        cy = doubleVal(params[pi+2], 0.0) * sy;
                        clipPath.moveTo(cx, cy);
                        pi += 3;
                    } else if (cmd == "l" && pi + 2 < params.size()) {
                        cx = doubleVal(params[pi+1], 0.0) * sx;
                        cy = doubleVal(params[pi+2], 0.0) * sy;
                        clipPath.lineTo(cx, cy);
                        pi += 3;
                    } else if (cmd == "b" && pi + 6 < params.size()) {
                        double c1x = doubleVal(params[pi+1], 0.0) * sx;
                        double c1y = doubleVal(params[pi+2], 0.0) * sy;
                        double c2x = doubleVal(params[pi+3], 0.0) * sx;
                        double c2y = doubleVal(params[pi+4], 0.0) * sy;
                        cx = doubleVal(params[pi+5], 0.0) * sx;
                        cy = doubleVal(params[pi+6], 0.0) * sy;
                        clipPath.cubicTo(c1x, c1y, c2x, c2y, cx, cy);
                        lastCX = c2x; lastCY = c2y;
                        pi += 7;
                    } else if (cmd == "s" && pi + 4 < params.size()) {
                        // Smooth bezier: uses last control point as reflection
                        double c1x = 2 * cx - lastCX;
                        double c1y = 2 * cy - lastCY;
                        lastCX = doubleVal(params[pi+1], 0.0) * sx;
                        lastCY = doubleVal(params[pi+2], 0.0) * sy;
                        cx = doubleVal(params[pi+3], 0.0) * sx;
                        cy = doubleVal(params[pi+4], 0.0) * sy;
                        clipPath.cubicTo(c1x, c1y, lastCX, lastCY, cx, cy);
                        pi += 5;
                    } else if (cmd == "p") {
                        clipPath.closeSubpath();
                        ++pi;
                    } else {
                        ++pi;
                    }
                }
                state.clip.path = clipPath;
                state.clip.isRect = false;
                state.clip.hasClip = true;
                state.clip.inverse = inverse;
            }
        }

    } else if (name == "p") {
        int v = intVal(param, 0);
        state.drawingMode = (v > 0);
        state.drawScale = v;

    } else if (name == "fad" && params.size() >= 2) {
        state.fadeData.fadeIn = doubleVal(params[0], 0.0) / 1000.0;
        state.fadeData.fadeOut = doubleVal(params[1], 0.0) / 1000.0;
        state.fadeData.isFade = false;

    } else if (name == "fade" && params.size() >= 7) {
        state.fadeData.a1 = intVal(params[0], 0);
        state.fadeData.a2 = intVal(params[1], 255);
        state.fadeData.a3 = intVal(params[2], 0);
        state.fadeData.t1 = doubleVal(params[3], 0.0) / 1000.0;
        state.fadeData.t2 = doubleVal(params[4], 0.0) / 1000.0;
        state.fadeData.t3 = doubleVal(params[5], 0.0) / 1000.0;
        state.fadeData.isFade = true;

    } else if (name == "k" || name == "kf" || name == "ko") {
        int cs = intVal(param, 0);
        if (cs > 0) {
            KaraokeSyllable syl;
            syl.duration = cs;
            state.karaoke.append(syl);
        }
    }
    // Unknown tags are silently ignored per stability requirements
}

QPainterPath AssOverrideParser::parseDrawingCommands(const QString &text, double scale)
{
    QPainterPath path;
    if (text.trimmed().isEmpty()) return path;

    double sx = qMax(scale, 0.01);
    double sy = sx;
    double cx = 0, cy = 0;
    double lastCX = 0, lastCY = 0;
    bool hasMove = false;

    // Split by spaces or command letters
    QStringList tokens;
    QString cur;
    for (int i = 0; i < text.size(); ++i) {
        QChar c = text[i];
        if (c.isLetter() && !cur.isEmpty()) {
            tokens.append(cur);
            cur.clear();
        }
        if (c.isSpace()) {
            if (!cur.isEmpty()) { tokens.append(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.isEmpty()) tokens.append(cur);

    int ti = 0;
    while (ti < tokens.size()) {
        QString cmd = tokens[ti].toLower();
        if (cmd == "m" && ti + 2 < tokens.size()) {
            cx = tokens[ti + 1].toDouble() * sx;
            cy = tokens[ti + 2].toDouble() * sy;
            path.moveTo(cx, cy);
            hasMove = true;
            ti += 3;
        } else if (cmd == "n" && ti + 2 < tokens.size()) {
            cx = tokens[ti + 1].toDouble() * sx;
            cy = tokens[ti + 2].toDouble() * sy;
            path.moveTo(cx, cy);
            hasMove = true;
            ti += 3;
        } else if (cmd == "l" && ti + 2 < tokens.size()) {
            if (!hasMove) { path.moveTo(0, 0); hasMove = true; }
            cx = tokens[ti + 1].toDouble() * sx;
            cy = tokens[ti + 2].toDouble() * sy;
            path.lineTo(cx, cy);
            ti += 3;
        } else if (cmd == "b" && ti + 6 < tokens.size()) {
            if (!hasMove) { path.moveTo(0, 0); hasMove = true; }
            double c1x = tokens[ti + 1].toDouble() * sx;
            double c1y = tokens[ti + 2].toDouble() * sy;
            double c2x = tokens[ti + 3].toDouble() * sx;
            double c2y = tokens[ti + 4].toDouble() * sy;
            cx = tokens[ti + 5].toDouble() * sx;
            cy = tokens[ti + 6].toDouble() * sy;
            path.cubicTo(c1x, c1y, c2x, c2y, cx, cy);
            lastCX = c2x; lastCY = c2y;
            ti += 7;
        } else if (cmd == "s" && ti + 4 < tokens.size()) {
            if (!hasMove) { path.moveTo(0, 0); hasMove = true; }
            double c1x = 2 * cx - lastCX;
            double c1y = 2 * cy - lastCY;
            double c2x = tokens[ti + 1].toDouble() * sx;
            double c2y = tokens[ti + 2].toDouble() * sy;
            cx = tokens[ti + 3].toDouble() * sx;
            cy = tokens[ti + 4].toDouble() * sy;
            path.cubicTo(c1x, c1y, c2x, c2y, cx, cy);
            lastCX = c2x; lastCY = c2y;
            ti += 5;
        } else if (cmd == "c" || cmd == "p") {
            path.closeSubpath();
            hasMove = false;
            ++ti;
        } else {
            ++ti;
        }
    }

    return path;
}
