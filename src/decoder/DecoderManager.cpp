#include "DecoderManager.h"

#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>

DecoderSelection DecoderManager::selectDecoder(const DecoderConfig &config)
{
    DecoderSelection result;

    GPUInfo gpu = ::detectGPU();
    auto order = probeOrder(gpu);

    struct Candidate {
        AVHWDeviceType hwType;
        DecoderScore score;
        QString name;
    };

    QVector<Candidate> candidates;

    for (AVHWDeviceType type : order) {
        DecoderScore s = scoreDecoder(gpu, type, config.codecId,
                                      config.width, config.height, config.bitrate);
        QString name = backendName(typeToBackend(type));

        if (s.totalScore > 0) {
            candidates.push_back({type, s, name});
            qDebug() << "DecoderManager: candidate" << name
                     << "score =" << s.totalScore;
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) -> bool {
                  return a.score.totalScore > b.score.totalScore;
              });

    for (const auto &candidate : candidates) {
        QElapsedTimer timer;
        timer.start();

        AVBufferRef *hwCtx = nullptr;
        if (av_hwdevice_ctx_create(&hwCtx, candidate.hwType, nullptr, nullptr, 0) < 0) {
            qWarning() << "DecoderManager:" << candidate.name
                       << "- failed to create HW device context";
            continue;
        }

        const char *nameStr = hwDecoderName(config.codecId, candidate.hwType);
        if (!nameStr) {
            av_buffer_unref(&hwCtx);
            continue;
        }

        const AVCodec *hwCodec = avcodec_find_decoder_by_name(nameStr);
        if (!hwCodec) {
            qWarning() << "DecoderManager:" << candidate.name
                       << "- decoder" << nameStr << "not found";
            av_buffer_unref(&hwCtx);
            continue;
        }

        AVCodecContext *hwCtx2 = avcodec_alloc_context3(hwCodec);
        if (!hwCtx2) {
            av_buffer_unref(&hwCtx);
            continue;
        }

        if (config.codecpar) {
            avcodec_parameters_to_context(hwCtx2, config.codecpar);
        }
        hwCtx2->hw_device_ctx = av_buffer_ref(hwCtx);

        int ret = avcodec_open2(hwCtx2, hwCodec, nullptr);
        if (ret < 0) {
            char errBuf[256];
            av_strerror(ret, errBuf, sizeof(errBuf));
            qWarning() << "DecoderManager:" << candidate.name
                       << "- avcodec_open2 failed:" << errBuf;
            avcodec_free_context(&hwCtx2);
            av_buffer_unref(&hwCtx);
            continue;
        }

        result.codecCtx = hwCtx2;
        result.codec = hwCodec;
        result.hwDeviceCtx = hwCtx;
        result.hwPixFmt = hwCtx2->pix_fmt;
        result.score = candidate.score.totalScore;
        result.valid = true;

        result.info.backend = typeToBackend(candidate.hwType);
        result.info.decoderName = candidate.name;
        result.info.gpuName = gpu.name;
        result.info.hwType = candidate.hwType;
        result.info.hardwareAccelerated = true;
        result.info.score = candidate.score;
        result.info.initTimeMs = timer.elapsed();

        qDebug() << "DecoderManager: selected" << candidate.name
                 << "in" << result.info.initTimeMs << "ms"
                 << "pix_fmt =" << av_get_pix_fmt_name(result.hwPixFmt);

        return result;
    }

    qDebug() << "DecoderManager: no HW decoder available, falling back to Software";

    const AVCodec *swCodec = avcodec_find_decoder(config.codecId);
    if (!swCodec)
        return result;

    AVCodecContext *swCtx = avcodec_alloc_context3(swCodec);
    if (!swCtx)
        return result;

    if (config.codecpar)
        avcodec_parameters_to_context(swCtx, config.codecpar);

    if (avcodec_open2(swCtx, swCodec, nullptr) < 0) {
        avcodec_free_context(&swCtx);
        return result;
    }

    result.codecCtx = swCtx;
    result.codec = swCodec;
    result.valid = true;
    result.info.backend = DecodeBackend::Software;
    result.info.decoderName = "Software";
    result.info.gpuName = gpu.name;
    result.info.hardwareAccelerated = false;
    result.info.hwType = AV_HWDEVICE_TYPE_NONE;

    qDebug() << "DecoderManager: using Software decoder";

    return result;
}
