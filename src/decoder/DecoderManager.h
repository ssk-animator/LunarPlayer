#ifndef DECODERMANAGER_H
#define DECODERMANAGER_H

#include <QVector>
#include <QElapsedTimer>
#include "DecoderInfo.h"
#include "HWAccel.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/error.h>
}

struct DecoderConfig
{
    AVCodecID codecId = AV_CODEC_ID_NONE;
    const AVCodecParameters *codecpar = nullptr;
    int width = 0;
    int height = 0;
    int64_t bitrate = 0;
};

struct DecoderSelection
{
    AVCodecContext *codecCtx = nullptr;
    const AVCodec *codec = nullptr;
    AVBufferRef *hwDeviceCtx = nullptr;
    AVPixelFormat hwPixFmt = AV_PIX_FMT_NONE;
    DecoderInfo info;
    int score = 0;
    bool valid = false;
};

class DecoderManager
{
public:
    static DecoderSelection selectDecoder(const DecoderConfig &config);
};

#endif
