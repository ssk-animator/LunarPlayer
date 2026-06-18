#include <QtTest/QtTest>
#include <cstring>

// Include MediaSession directly (inline-style test)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

class TestFFmpegBasics : public QObject {
    Q_OBJECT

private slots:
    void testFFmpegVersion()
    {
        QVERIFY(avcodec_version() > 0);
        QVERIFY(avformat_version() > 0);
        QVERIFY(avutil_version() > 0);
    }

    void testFFmpegConfiguration()
    {
        const char *cfg = avcodec_configuration();
        QVERIFY(cfg != nullptr);
        QVERIFY(strlen(cfg) > 0);
    }

    void testFFmpegCodecs()
    {
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        QVERIFY(codec != nullptr);
        QCOMPARE(codec->id, AV_CODEC_ID_H264);

        codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
        QVERIFY(codec != nullptr);
        QCOMPARE(codec->id, AV_CODEC_ID_HEVC);

        codec = avcodec_find_decoder(AV_CODEC_ID_AV1);
        QVERIFY(codec != nullptr);
    }

    void testFFmpegProtocols()
    {
        void *opaque = nullptr;
        const char *p = avio_enum_protocols(&opaque, 0);
        QVERIFY(p != nullptr);
        opaque = nullptr;
        p = avio_enum_protocols(&opaque, 1);
        QVERIFY(p != nullptr);
    }

    void testPixelFormatConversion()
    {
        // Create a simple RGB24 -> YUV420P conversion context
        SwsContext *sws = sws_getContext(320, 240, AV_PIX_FMT_RGB24,
                                         320, 240, AV_PIX_FMT_YUV420P,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        QVERIFY(sws != nullptr);
        sws_freeContext(sws);
    }

    void testMemoryAllocation()
    {
        AVFrame *frame = av_frame_alloc();
        QVERIFY(frame != nullptr);
        av_frame_free(&frame);
        QVERIFY(frame == nullptr);

        AVPacket *pkt = av_packet_alloc();
        QVERIFY(pkt != nullptr);
        av_packet_free(&pkt);
        QVERIFY(pkt == nullptr);
    }
};

QTEST_MAIN(TestFFmpegBasics)
#include "test_ffmpeg.moc"
