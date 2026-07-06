#ifndef SUBTITLEDECODER_H
#define SUBTITLEDECODER_H

#include "SubtitleFrame.h"
#include <QString>
#include <QVector>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

class SubtitleDecoder {
public:
    SubtitleDecoder();
    ~SubtitleDecoder();

    bool openStream(AVFormatContext *fmtCtx, int streamIndex);
    bool openFile(const QString &filePath);
    void close();

    bool isOpen() const { return m_codecCtx != nullptr; }
    int streamIndex() const { return m_streamIndex; }
    AVStream* stream() const { return m_stream; }
    AVCodecContext* codecCtx() const { return m_codecCtx; }

    bool decode(AVPacket *pkt, QVector<SubtitleFrame> &frames);
    bool decodeSubtitles(AVFormatContext *fmtCtx, int streamIndex,
                         QVector<SubtitleFrame> &outFrames);

private:
    // Convert a single AVSubtitle packet into one or more SubtitleFrames
    QVector<SubtitleFrame> convertSubtitle(const AVSubtitle &sub,
                                            AVRational timeBase,
                                            int streamIndex);

    // Composite multiple bitmap rects into a single SubtitleFrame
    SubtitleFrame composeBitmapSubtitle(const AVSubtitle &sub,
                                         AVRational timeBase,
                                         int streamIndex);

    int m_streamIndex = -1;
    AVStream *m_stream = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    const AVCodec *m_codec = nullptr;
};

#endif // SUBTITLEDECODER_H
