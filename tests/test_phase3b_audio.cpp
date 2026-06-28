// Phase 3B Audio Playback Validation
// Run: build\LunarPlayerPhase3B.exe

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QThread>
#include <cstdio>
#include <cmath>

#include "ui/MainWindow.h"
#include "decoder/AudioDecoder.h"
#include "audio/AudioOutput.h"

static bool failFlag = false;
#define VALIDATE(cond, msg) do { \
    if (!(cond)) { failFlag = true; printf("  FAIL: %s\n", msg); fflush(stdout); } \
    else { printf("  PASS: %s\n", msg); fflush(stdout); } \
} while(0)

static void pump(int ms)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QApplication::processEvents(); QThread::msleep(5); }
}

static QString mediaPath(const QString &file)
{
    QString base = QDir::cleanPath(
        QApplication::applicationDirPath() + "/../../tests/media");
    if (!QDir(base).exists())
        base = QDir::cleanPath(
            QApplication::applicationDirPath() + "/../tests/media");
    return QDir(base).filePath(file);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QString testFile = mediaPath("test_av_2s.mp4");

    printf("\n=== Phase 3B Audio Playback Validation ===\n\n");

    // ----- Test 1: AudioDecoder direct pipeline -----
    printf("[1] AudioDecoder + AudioOutput Pipeline\n");
    {
        AudioDecoder decoder;
        AudioOutput output;
        AVFormatContext *fmt = nullptr;

        int ret = avformat_open_input(&fmt, testFile.toUtf8().constData(), nullptr, nullptr);
        if (ret == 0) ret = avformat_find_stream_info(fmt, nullptr);
        VALIDATE(ret >= 0, "file opens via FFmpeg");

        if (ret >= 0) {
            int audioIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
            VALIDATE(audioIdx >= 0, "audio stream found");

            if (audioIdx >= 0) {
                AVCodecParameters *cp = fmt->streams[audioIdx]->codecpar;
                printf("  codec_id=%d rate=%ld ch=%d\n",
                       cp->codec_id, cp->sample_rate, cp->ch_layout.nb_channels);
                VALIDATE(cp->sample_rate > 0, "valid sample rate");
                VALIDATE(cp->ch_layout.nb_channels > 0, "valid channels");

                VALIDATE(decoder.open(fmt, audioIdx), "decoder opens");
                printf("  decoder: %dHz %dch\n", decoder.sampleRate(), decoder.channels());

                VALIDATE(output.open(decoder.sampleRate(), decoder.channels()), "output opens");

                int totalFrames = 0, pkts = 0;
                AVPacket *pkt = av_packet_alloc();
                while (pkts < 10 && av_read_frame(fmt, pkt) >= 0) {
                    if (pkt->stream_index == audioIdx) {
                        float buf[8192 * 2] = {};
                        int frames = decoder.decode(pkt, buf, 8192);
                        if (frames > 0) {
                            totalFrames += frames;
                            pkts++;
                            output.write(buf, frames);
                        }
                    }
                    av_packet_unref(pkt);
                }
                av_packet_free(&pkt);
                printf("  decoded %d pkts -> %d frames (%.2fs)\n",
                       pkts, totalFrames, (float)totalFrames / decoder.sampleRate());
                VALIDATE(totalFrames > 0, "audio decoded successfully");

                pump(200);
                output.reset();
                output.close();
                decoder.close();
            }
            avformat_close_input(&fmt);
        }
        VALIDATE(!decoder.isOpen(), "decoder closed");
        VALIDATE(!output.isOpen(), "output closed");
    }
    printf("\n");

    // ----- Test 2: AudioOutput unit -----
    printf("[2] AudioOutput Unit\n");
    {
        AudioOutput out;
        VALIDATE(!out.isOpen(), "closed initially");
        VALIDATE(out.open(48000, 2), "open 48kHz stereo");
        float buf[4096 * 2] = {};
        VALIDATE(out.write(buf, 4096) > 0, "write ok");
        pump(200);
        VALIDATE(out.currentPositionSec() >= 0.0, "position ok");
        out.setVolume(0.5f);
        VALIDATE(qAbs(out.volume() - 0.5f) < 0.01f, "volume ok");
        out.close();
        VALIDATE(!out.isOpen(), "closed");
    }
    printf("\n");

    // ----- Test 3: End-to-end via MainWindow -----
    printf("[3] MainWindow Playback + Volume\n");
    {
        MainWindow mw;
        mw.resize(800, 500);
        mw.show();
        pump(200);

        mw.loadFile(testFile);
        printf("  playing 1.5s...\n");
        pump(1500);
        printf("  pos=%.2fs\n", mw.m_currentPosSec);
        VALIDATE(mw.m_currentPosSec > 0.5, "position advanced");

        mw.m_volumeSlider->setValue(0);
        pump(300);
        printf("  muted\n");

        mw.m_volumeSlider->setValue(80);
        pump(300);
        printf("  restored\n");

        VALIDATE(true, "no crash during volume");
        mw.closeFile();
    }
    printf("\n");

    printf("=== Phase 3B: %s ===\n",
           failFlag ? "FAIL" : "ALL PASS");
    return failFlag ? 1 : 0;
}
