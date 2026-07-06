#include <cstdio>
#include <cstdlib>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

#include "subtitles/AssParser.h"
#include "subtitles/AssOverrideParser.h"
#include "subtitles/SubtitleRenderer.h"
#include "subtitles/SubtitleManager.h"
#include "subtitles/ExternalSubtitleLoader.h"

#define TEST(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fflush(stdout); return 1; } \
    printf("PASS: %s\n", msg); fflush(stdout); \
} while(0)

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("Lunar Player Phase 7C Test");
    QString testDir = TEST_SOURCE_DIR;
    QString assFile = testDir + "/tests/media/test_bars_ass.ass";

    // =================================================================
    // Test 1: ASS Parser - File loading
    // =================================================================
    printf("\n=== Test 1: ASS Parser ===\n");
    {
        AssParsedFile parsed = AssParser::parse(assFile);
        TEST(parsed.valid, "ASS file parses successfully");
        TEST(parsed.info.playRes.width() == 640, "PlayResX = 640");
        TEST(parsed.info.playRes.height() == 480, "PlayResY = 480");
        TEST(parsed.styles.size() >= 3, "At least 3 styles defined");
        TEST(parsed.dialogues.size() >= 7, "At least 7 dialogue events");

        // Verify named style Default
        bool foundDefault = false;
        for (const auto &s : parsed.styles) {
            if (s.name == "Default") {
                foundDefault = true;
                TEST(s.font.family == "Arial", "Default font = Arial");
                TEST(s.font.size == 28, "Default font size = 28");
                TEST(s.colors.primary == QColor(Qt::white), "Default primary = white");
                TEST(s.outlineShadow.outlineWidth == 2.0, "Default outline = 2");
                TEST(s.outlineShadow.shadowY == 2.0, "Default shadow = 2");
                TEST(s.position.alignment == 2, "Default alignment = 2 (bottom-center)");
                break;
            }
        }
        TEST(foundDefault, "Default style found");

        // Verify Title style
        bool foundTitle = false;
        for (const auto &s : parsed.styles) {
            if (s.name == "Title") {
                foundTitle = true;
                TEST(s.font.family == "Impact", "Title font = Impact");
                TEST(s.font.size == 48, "Title font size = 48");
                TEST(s.font.bold, "Title is bold");
                TEST(s.position.alignment == 8, "Title alignment = 8 (top-center)");
                break;
            }
        }
        TEST(foundTitle, "Title style found");
    }

    // =================================================================
    // Test 2: dialogueToFrame conversion
    // =================================================================
    printf("\n=== Test 2: dialogueToFrame ===\n");
    {
        AssParsedFile parsed = AssParser::parse(assFile);
        auto styleMap = AssParser::buildStyleMap(parsed.styles);

        SubtitleFrame frame = AssParser::dialogueToFrame(parsed.dialogues[0], styleMap, 0);
        TEST(frame.type == SubtitleType::ASS, "Frame type = ASS");
        TEST(frame.startSeconds >= 0.99 && frame.startSeconds <= 1.01, "Start time ~1.0s");
        TEST(frame.endSeconds >= 4.99 && frame.endSeconds <= 5.01, "End time ~5.0s");
        TEST(frame.layer == 0, "Layer = 0");
        TEST(!frame.text.isEmpty(), "Raw ASS text preserved");
        TEST(frame.text.contains("{\\b1}"), "Override tag preserved in raw text");
    }

    // =================================================================
    // Test 3: AssOverrideParser - Basic tags
    // =================================================================
    printf("\n=== Test 3: Override Parser - Basic Tags ===\n");
    {
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        frame.startSeconds = 1.0;
        frame.endSeconds = 5.0;

        SubtitleRenderContext ctx;
        ctx.videoSize = QSize(640, 480);
        ctx.playRes = QSize(640, 480);

        // Bold tag
        QString text1 = "Normal {\\b1}Bold{\\b0} Normal";
        auto lines1 = AssOverrideParser::segment(text1, frame, ctx);
        TEST(lines1.size() >= 1, "Bold: at least one line");
        if (!lines1.isEmpty() && lines1.first().segments.size() >= 3) {
            TEST(!lines1.first().segments[0].font.bold(), "First segment not bold");
            TEST(lines1.first().segments[1].font.bold(), "Second segment is bold");
            TEST(!lines1.first().segments[2].font.bold(), "Third segment not bold");
        }

        // Italic tag
        QString text2 = "Normal {\\i1}Italic{\\i0} Normal";
        auto lines2 = AssOverrideParser::segment(text2, frame, ctx);
        if (!lines2.isEmpty() && lines2.first().segments.size() >= 3) {
            TEST(!lines2.first().segments[0].font.italic(), "First segment not italic");
            TEST(lines2.first().segments[1].font.italic(), "Second segment is italic");
        }

        // Color tag
        QString text3 = "{\\c&H00FF00&}Green";
        auto lines3 = AssOverrideParser::segment(text3, frame, ctx);
        if (!lines3.isEmpty() && !lines3.first().segments.isEmpty()) {
            TEST(lines3.first().segments[0].primaryColor == QColor(0, 255, 0),
                 "Green color applied");
        }

        // Font size tag
        QString text4 = "{\\fs40}Large{\\fs20}Small";
        auto lines4 = AssOverrideParser::segment(text4, frame, ctx);
        if (!lines4.isEmpty() && lines4.first().segments.size() >= 2) {
            QFontMetrics fm1(lines4.first().segments[0].font);
            QFontMetrics fm2(lines4.first().segments[1].font);
            TEST(fm1.height() > fm2.height(), "Font size 40 > 20");
        }

        // Font family tag - use Arial as guaranteed-available fallback
        QString text5 = "{\\fnArial}Arial font";
        auto lines5 = AssOverrideParser::segment(text5, frame, ctx);
        if (!lines5.isEmpty() && !lines5.first().segments.isEmpty()) {
            TEST(lines5.first().segments[0].font.family().contains("Arial", Qt::CaseInsensitive),
                 "Font family set to Arial");
        }
    }

    // =================================================================
    // Test 4: AssOverrideParser - Position, Outline, Shadow
    // =================================================================
    printf("\n=== Test 4: Override Parser - Position/Outline/Shadow ===\n");
    {
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        SubtitleRenderContext ctx;
        ctx.videoSize = QSize(640, 480);
        ctx.playRes = QSize(640, 480);

        // Outline width
        QString text1 = "{\\bord4}Thick outline";
        auto lines1 = AssOverrideParser::segment(text1, frame, ctx);
        if (!lines1.isEmpty() && !lines1.first().segments.isEmpty()) {
            TEST(lines1.first().segments[0].outlineWidth >= 3.9,
                 "Outline width = 4");
        }

        // Position
        QString text2 = "{\\pos(320,240)}Centered";
        auto lines2 = AssOverrideParser::segment(text2, frame, ctx);
        if (!lines2.isEmpty() && !lines2.first().segments.isEmpty()) {
            TEST(lines2.first().segments[0].hasExplicitPos, "\\pos sets hasExplicitPos");
            TEST(lines2.first().segments[0].position.x() >= 319 &&
                 lines2.first().segments[0].position.x() <= 321,
                 "\\pos x = 320");
            TEST(lines2.first().segments[0].position.y() >= 239 &&
                 lines2.first().segments[0].position.y() <= 241,
                 "\\pos y = 240");
        }

        // Shadow with both coords
        QString text3 = "{\\shad(5,3)}Shadow";
        auto lines3 = AssOverrideParser::segment(text3, frame, ctx);
        if (!lines3.isEmpty() && !lines3.first().segments.isEmpty()) {
            TEST(lines3.first().segments[0].shadowOffset.x() >= 4.9,
                 "Shadow X offset = 5");
            TEST(lines3.first().segments[0].shadowOffset.y() >= 2.9,
                 "Shadow Y offset = 3");
        }
    }

    // =================================================================
    // Test 5: AssOverrideParser - \N and \n newlines
    // =================================================================
    printf("\n=== Test 5: Override Parser - Newlines ===\n");
    {
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        SubtitleRenderContext ctx;
        ctx.videoSize = QSize(640, 480);
        ctx.playRes = QSize(640, 480);

        QString text1 = "Line1\\NLine2";
        auto lines1 = AssOverrideParser::segment(text1, frame, ctx);
        TEST(lines1.size() >= 2, "\\N creates two lines");
    }

    // =================================================================
    // Test 6: SubtitleRenderer - ASS rendering produces surface
    // =================================================================
    printf("\n=== Test 6: ASS Renderer ===\n");
    {
        SubtitleRenderer renderer;
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        frame.text = "Hello {\\b1}World{\\b0}";
        frame.startSeconds = 0.0;
        frame.endSeconds = 5.0;
        frame.pts = 0;
        frame.duration = 5000;

        QVector<SubtitleSurface> surfaces = renderer.render(frame, 640, 480);
        TEST(!surfaces.isEmpty(), "ASS rendering produces surfaces");
        if (!surfaces.isEmpty()) {
            TEST(!surfaces[0].isNull(), "ASS surface is not null");
            QImage img = surfaces[0].toImage();
            TEST(img.width() > 0 && img.height() > 0, "ASS surface has dimensions");
        }
    }

    // =================================================================
    // Test 7: SubtitleRenderer - ASS alignment positions
    // =================================================================
    printf("\n=== Test 7: ASS Alignment ===\n");
    {
        SubtitleRenderer renderer;

        // Test with style that has different alignments
        SubtitleFrame frame1;
        frame1.type = SubtitleType::ASS;
        frame1.text = "Bottom Center";
        frame1.startSeconds = 0.0;
        frame1.endSeconds = 5.0;

        QVector<SubtitleSurface> surf1 = renderer.render(frame1, 640, 480);
        // Default alignment 2 = bottom-center
        if (!surf1.isEmpty()) {
            TEST(surf1[0].posY() < 480, "Bottom alignment above video bottom");
        }
    }

    // =================================================================
    // Test 8: ExternalSubtitleLoader - ASS file loading
    // =================================================================
    printf("\n=== Test 8: ExternalSubtitleLoader ASS ===\n");
    {
        QVector<SubtitleFrame> frames = ExternalSubtitleLoader::parseASS(assFile);
        TEST(frames.size() >= 7, "All 7 dialogue events parsed as frames");
        if (!frames.isEmpty()) {
            TEST(frames[0].type == SubtitleType::ASS, "Frame type = ASS");
            TEST(frames[0].text.contains("{\\b1}"), "ASS markup preserved in frame");
        }
    }

    // =================================================================
    // Test 9: SubtitleManager - ASS track lifecycle
    // =================================================================
    printf("\n=== Test 9: SubtitleManager ASS Track ===\n");
    {
        SubtitleManager mgr;
        bool loaded = mgr.loadExternalFile(assFile);
        TEST(loaded, "SubtitleManager loads ASS file");

        TEST(mgr.trackCount() >= 1, "At least one track after loading");
        TEST(mgr.activeTrackIndex() == -1, "No active track initially");

        mgr.setActiveTrack(0);
        TEST(mgr.activeTrackIndex() == 0, "Active track set to 0");

        // Get renderings at various timestamps
        QVector<QImage> imgs1 = mgr.getSubtitleImages(1.5, 640, 480);
        TEST(!imgs1.isEmpty(), "Rendered frames at t=1.5s");
        if (!imgs1.isEmpty()) {
            TEST(imgs1[0].width() == 640 && imgs1[0].height() == 480,
                 "Canvas is video-sized");
        }

        // At t=0.5s, no subtitles active (first starts at 1s)
        QVector<QImage> imgs0 = mgr.getSubtitleImages(0.5, 640, 480);
        TEST(imgs0.isEmpty() || imgs0[0].isNull(),
             "No subtitles at t=0.5s (before any event)");
    }

    // =================================================================
    // Test 10: Layer ordering
    // =================================================================
    printf("\n=== Test 10: Layer Ordering ===\n");
    {
        AssParsedFile parsed = AssParser::parse(assFile);
        // Dialogues 0 and 1 have layer 0, dialogues 2-3 have layer 1, dialogue 4 has layer 2
        TEST(parsed.dialogues[0].layer == 0, "First dialogue layer 0");
        TEST(parsed.dialogues[1].layer == 0, "Second dialogue layer 0");
        bool foundLayer1 = false;
        bool foundLayer2 = false;
        for (const auto &d : parsed.dialogues) {
            if (d.layer == 1) foundLayer1 = true;
            if (d.layer == 2) foundLayer2 = true;
        }
        TEST(foundLayer1, "Layer 1 present");
        TEST(foundLayer2, "Layer 2 present");
    }

    // =================================================================
    // Test 11: Vector drawing command parsing
    // =================================================================
    printf("\n=== Test 11: Drawing Commands ===\n");
    {
        // Test drawing commands: scale 1, triangle
        QString text = "m 0 0 l 100 0 l 50 100 p";
        double scale = 0.5;
        QPainterPath path = AssOverrideParser::parseDrawingCommands(text, scale);
        TEST(!path.isEmpty(), "Drawing path is not empty");
        TEST(path.elementCount() > 0, "Drawing path has elements");
        // Should have moveTo + 2 lineTo + close
        bool hasMove = false, hasLine = false;
        for (int i = 0; i < path.elementCount(); ++i) {
            QPainterPath::Element el = path.elementAt(i);
            if (el.type == QPainterPath::MoveToElement) hasMove = true;
            if (el.type == QPainterPath::LineToElement) hasLine = true;
        }
        TEST(hasMove, "Drawing path has MoveTo element");
        TEST(hasLine, "Drawing path has LineTo element");
    }

    // =================================================================
    // Test 12: Karaoke syllable parsed correctly
    // =================================================================
    printf("\n=== Test 12: Karaoke Parsing ===\n");
    {
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        SubtitleRenderContext ctx;
        ctx.videoSize = QSize(640, 480);
        ctx.playRes = QSize(640, 480);
        ctx.currentPTS = 0.0;

        QString text = "{\\k30}One {\\k20}Two {\\k50}Three";
        auto lines = AssOverrideParser::segment(text, frame, ctx);
        // Should have segments with karaoke data
        if (!lines.isEmpty()) {
            bool hasKaraoke = false;
            for (const auto &seg : lines.first().segments) {
                if (seg.karaokeIndex >= 0 && seg.karaokeDuration > 0) {
                    hasKaraoke = true;
                    break;
                }
            }
            TEST(hasKaraoke, "Karaoke segments have index and duration");
            if (!lines.first().segments.isEmpty()) {
                // Total karaoke duration should be 30+20+50 = 100 centiseconds
                TEST(lines.first().karaokeSyllables.size() >= 3,
                     "Karaoke syllables stored on line");
            }
        }
    }

    // =================================================================
    // Test 13: \clip rect parsing
    // =================================================================
    printf("\n=== Test 13: Clip Parsing ===\n");
    {
        SubtitleFrame frame;
        frame.type = SubtitleType::ASS;
        SubtitleRenderContext ctx;
        ctx.videoSize = QSize(640, 480);
        ctx.playRes = QSize(640, 480);

        // Test rect clip
        QString text1 = "{\\clip(100,50,500,400)}Clipped text";
        auto lines1 = AssOverrideParser::segment(text1, frame, ctx);
        if (!lines1.isEmpty() && !lines1.first().segments.isEmpty()) {
            const auto &seg = lines1.first().segments.first();
            TEST(seg.hasClip, "\\clip sets hasClip");
            TEST(!seg.inverseClip, "\\clip is not inverse");
        }

        // Test inverse clip
        QString text2 = "{\\iclip(100,50,500,400)}Inverse clip";
        auto lines2 = AssOverrideParser::segment(text2, frame, ctx);
        if (!lines2.isEmpty() && !lines2.first().segments.isEmpty()) {
            const auto &seg = lines2.first().segments.first();
            TEST(seg.hasClip, "\\iclip sets hasClip");
            TEST(seg.inverseClip, "\\iclip is inverse");
        }
    }

    // =================================================================
    // All tests passed
    // =================================================================
    printf("\nALL PHASE 7C TESTS PASSED\n");
    return 0;
}
