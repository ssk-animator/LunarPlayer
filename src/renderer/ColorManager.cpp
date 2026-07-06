#include "ColorManager.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/mastering_display_metadata.h>
#include <libswscale/swscale.h>
}

#include <QDebug>
#include <cmath>

ColorManager::ColorManager(QObject *parent) : QObject(parent) {}

HDRMetadata ColorManager::parseFrameMetadata(const AVFrame *frame)
{
    HDRMetadata md;
    if (!frame) return md;

    // Transfer characteristics
    switch (frame->color_trc) {
    case AVCOL_TRC_SMPTE2084:
        md.transfer = TransferCharacteristics::PQ;
        md.format = HDRFormat::HDR10;
        break;
    case AVCOL_TRC_ARIB_STD_B67:
        md.transfer = TransferCharacteristics::HLG;
        md.format = HDRFormat::HLG;
        break;
    case AVCOL_TRC_BT709:
        md.transfer = TransferCharacteristics::BT709;
        md.format = HDRFormat::SDR;
        break;
    case AVCOL_TRC_BT2020_10:
        md.transfer = TransferCharacteristics::BT2020_10;
        break;
    case AVCOL_TRC_BT2020_12:
        md.transfer = TransferCharacteristics::BT2020_12;
        break;
    default:
        md.transfer = TransferCharacteristics::Unknown;
        break;
    }

    // Color primaries
    switch (frame->color_primaries) {
    case AVCOL_PRI_BT709:
        md.primaries = ColorPrimaries::BT709;
        break;
    case AVCOL_PRI_BT2020:
        md.primaries = ColorPrimaries::BT2020;
        break;
    case AVCOL_PRI_SMPTE432:
        md.primaries = ColorPrimaries::DCI_P3;
        break;
    default:
        md.primaries = ColorPrimaries::Unknown;
        break;
    }

    // Mastering display metadata side data
    AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    if (sd && sd->data) {
        auto *mdm = reinterpret_cast<AVMasteringDisplayMetadata*>(sd->data);
        if (mdm->has_primaries && mdm->has_luminance) {
            for (int i = 0; i < 3; ++i) {
                md.displayPrimaryX[i] = av_q2d(mdm->display_primaries[i][0]);
                md.displayPrimaryY[i] = av_q2d(mdm->display_primaries[i][1]);
            }
            md.whitePointX = av_q2d(mdm->white_point[0]);
            md.whitePointY = av_q2d(mdm->white_point[1]);
            md.maxLuminance = av_q2d(mdm->max_luminance);
            md.minLuminance = av_q2d(mdm->min_luminance);
            md.valid = true;
        }
    }

    // Content light level metadata
    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    if (sd && sd->data) {
        auto *cll = reinterpret_cast<AVContentLightMetadata*>(sd->data);
        md.maxCLL = static_cast<double>(cll->MaxCLL);
        md.maxFALL = static_cast<double>(cll->MaxFALL);
        md.valid = true;
    }

    // Detect Dolby Vision
    if (av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA)) {
        md.format = HDRFormat::DolbyVision;
    }

    return md;
}

HDRFormat ColorManager::detectFormat(const AVFrame *frame)
{
    if (!frame) return HDRFormat::None;
    HDRMetadata md = parseFrameMetadata(frame);
    return md.format;
}

int ColorManager::swsColorFlags(const HDRMetadata &, const HDRMetadata &)
{
    return SWS_BILINEAR;
}

bool ColorManager::needsToneMapping(const HDRMetadata &metadata)
{
    return (metadata.transfer == TransferCharacteristics::PQ ||
            metadata.transfer == TransferCharacteristics::HLG);
}

QString ColorManager::formatName(HDRFormat fmt)
{
    switch (fmt) {
    case HDRFormat::HDR10:       return "HDR10";
    case HDRFormat::HDR10Plus:   return "HDR10+";
    case HDRFormat::HLG:         return "HLG";
    case HDRFormat::DolbyVision: return "Dolby Vision";
    case HDRFormat::SDR:         return "SDR";
    default:                     return "Unknown";
    }
}

QString ColorManager::description(const HDRMetadata &md)
{
    QString s = formatName(md.format);
    if (md.valid) {
        s += QString(" | MaxCLL: %1 nits, MaxFALL: %2 nits")
            .arg(md.maxCLL, 0, 'f', 0)
            .arg(md.maxFALL, 0, 'f', 0);
        s += QString(" | MaxLum: %1 nits")
            .arg(md.maxLuminance, 0, 'f', 0);
    }
    return s;
}

QImage ColorManager::applyToneMap(const QImage &frame, const HDRMetadata &metadata)
{
    if (!needsToneMapping(metadata))
        return frame;

    QImage result = frame.convertToFormat(QImage::Format_ARGB32);
    double exposure = std::pow(2.0, m_exposure);

    for (int y = 0; y < result.height(); ++y) {
        auto *px = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            int r = qRed(px[x]);
            int g = qGreen(px[x]);
            int b = qBlue(px[x]);
            int a = qAlpha(px[x]);

            double dr = (r / 255.0) * exposure;
            double dg = (g / 255.0) * exposure;
            double db = (b / 255.0) * exposure;

            dr = dr / (1.0 + dr);
            dg = dg / (1.0 + dg);
            db = db / (1.0 + db);

            px[x] = qRgba(
                static_cast<int>(dr * 255.0),
                static_cast<int>(dg * 255.0),
                static_cast<int>(db * 255.0),
                a);
        }
    }
    return result;
}
