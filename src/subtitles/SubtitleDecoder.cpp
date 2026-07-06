#include "SubtitleDecoder.h"
#include <QDebug>
#include <QPainter>

// FFmpeg headers for palette conversion
extern "C" {
#include <libswscale/swscale.h>
}

SubtitleDecoder::SubtitleDecoder() = default;

SubtitleDecoder::~SubtitleDecoder()
{
    close();
}

bool SubtitleDecoder::openStream(AVFormatContext *fmtCtx, int streamIndex)
{
    close();
    if (!fmtCtx || streamIndex < 0 || streamIndex >= (int)fmtCtx->nb_streams)
        return false;
    AVStream *st = fmtCtx->streams[streamIndex];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
        return false;
    m_codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!m_codec) return false;
    m_codecCtx = avcodec_alloc_context3(m_codec);
    if (!m_codecCtx) return false;
    if (avcodec_parameters_to_context(m_codecCtx, st->codecpar) < 0) {
        avcodec_free_context(&m_codecCtx);
        return false;
    }
    if (avcodec_open2(m_codecCtx, m_codec, nullptr) < 0) {
        avcodec_free_context(&m_codecCtx);
        return false;
    }
    m_streamIndex = streamIndex;
    m_stream = st;
    return true;
}

bool SubtitleDecoder::openFile(const QString &filePath)
{
    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, filePath.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }
    for (unsigned i = 0; i < fmtCtx->nb_streams; ++i) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            if (openStream(fmtCtx, (int)i))
                return true;
        }
    }
    avformat_close_input(&fmtCtx);
    return false;
}

void SubtitleDecoder::close()
{
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
        m_codecCtx = nullptr;
    }
    m_codec = nullptr;
    m_stream = nullptr;
    m_streamIndex = -1;
}

bool SubtitleDecoder::decode(AVPacket *pkt, QVector<SubtitleFrame> &frames)
{
    if (!m_codecCtx || !pkt) return false;
    AVSubtitle sub;
    memset(&sub, 0, sizeof(sub));
    int gotSub = 0;
    int ret = avcodec_decode_subtitle2(m_codecCtx, &sub, &gotSub, pkt);
    if (ret < 0 || !gotSub) return false;
    QVector<SubtitleFrame> converted = convertSubtitle(sub, m_stream->time_base, m_streamIndex);
    for (SubtitleFrame &f : converted)
        frames.append(std::move(f));
    avsubtitle_free(&sub);
    return true;
}

bool SubtitleDecoder::decodeSubtitles(AVFormatContext *fmtCtx, int streamIndex,
                                       QVector<SubtitleFrame> &outFrames)
{
    if (!openStream(fmtCtx, streamIndex))
        return false;
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) return false;
    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == m_streamIndex) {
            decode(pkt, outFrames);
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return !outFrames.isEmpty();
}

QVector<SubtitleFrame> SubtitleDecoder::convertSubtitle(const AVSubtitle &sub,
                                                          AVRational timeBase,
                                                          int streamIndex)
{
    if (sub.num_rects > 0 && sub.rects[0]->type == SUBTITLE_BITMAP) {
        QVector<SubtitleFrame> result;
        result.append(composeBitmapSubtitle(sub, timeBase, streamIndex));
        return result;
    }

    // Text subtitle path
    SubtitleFrame frame;
    frame.type = SubtitleType::Text;
    frame.pts = sub.pts;
    frame.duration = sub.end_display_time;
    frame.startSeconds = av_q2d(timeBase) * sub.pts;
    frame.endSeconds = frame.startSeconds + (sub.end_display_time / 1000.0);
    frame.trackIndex = streamIndex;
    frame.subtitleIndex = 0;

    if (sub.num_rects > 0 && sub.rects[0]->text)
        frame.text = QString::fromUtf8(sub.rects[0]->text);

    return {frame};
}

SubtitleFrame SubtitleDecoder::composeBitmapSubtitle(const AVSubtitle &sub,
                                                      AVRational timeBase,
                                                      int streamIndex)
{
    SubtitleFrame frame;
    frame.type = SubtitleType::Bitmap;
    frame.pts = sub.pts;
    frame.duration = sub.end_display_time;
    frame.startSeconds = av_q2d(timeBase) * sub.pts;
    frame.endSeconds = frame.startSeconds + (sub.end_display_time / 1000.0);
    frame.trackIndex = streamIndex;
    frame.subtitleIndex = 0;

    if (sub.num_rects == 0)
        return frame;

    // Determine bounding box across all rects
    int minX = INT_MAX, minY = INT_MAX;
    int maxX = 0, maxY = 0;
    int targetW = 0, targetH = 0;

    for (unsigned i = 0; i < sub.num_rects; ++i) {
        AVSubtitleRect *rect = sub.rects[i];
        minX = qMin(minX, rect->x);
        minY = qMin(minY, rect->y);
        maxX = qMax(maxX, rect->x + rect->w);
        maxY = qMax(maxY, rect->y + rect->h);
        targetW = qMax(targetW, rect->w);
        targetH = qMax(targetH, rect->h);
    }

    // Use the PGS composition dimensions if available, otherwise rect bounds
    int canvasW = sub.format == 1 ? qMax(maxX, targetW) : maxX;
    int canvasH = sub.format == 1 ? qMax(maxY, targetH) : maxY;
    if (canvasW <= 0 || canvasH <= 0) {
        canvasW = maxX;
        canvasH = maxY;
    }

    frame.posX = 0;
    frame.posY = 0;
    frame.displayWidth = canvasW;
    frame.displayHeight = canvasH;

    QImage composite(canvasW, canvasH, QImage::Format_ARGB32_Premultiplied);
    composite.fill(Qt::transparent);
    QPainter p(&composite);

    for (unsigned i = 0; i < sub.num_rects; ++i) {
        AVSubtitleRect *rect = sub.rects[i];
        if (rect->w <= 0 || rect->h <= 0) continue;

        QImage rectImg(rect->data[0], rect->w, rect->h,
                       rect->linesize[0], QImage::Format_Indexed8);

        // Build palette from rect palette data
        QVector<QRgb> colorTable;
        colorTable.reserve(rect->nb_colors);
        uint32_t *pal = reinterpret_cast<uint32_t*>(rect->data[1]);
        for (int c = 0; c < rect->nb_colors; ++c) {
            // PGS palette: 4 bytes per entry (Y, Cb, Cr, A)
            // For indexed formats, the palette data is already RGBA.
            // For DVD subtitles, palette is YUV and needs conversion.
            uint8_t *palEntry = reinterpret_cast<uint8_t*>(&pal[c]);
            uint8_t y = palEntry[0];
            uint8_t cb = palEntry[1];
            uint8_t cr = palEntry[2];
            uint8_t a = palEntry[3];
            // Simple YUV→RGB approximation for palette entries
            int yy = y - 16;
            int ccb = cb - 128;
            int ccr = cr - 128;
            int r = qBound(0, (298 * yy + 409 * ccr + 128) >> 8, 255);
            int g = qBound(0, (298 * yy - 100 * ccb - 208 * ccr + 128) >> 8, 255);
            int b = qBound(0, (298 * yy + 516 * ccb + 128) >> 8, 255);
            colorTable.append(qRgba(r, g, b, a));
        }
        if (!colorTable.isEmpty())
            rectImg.setColorTable(colorTable);

        // Convert to ARGB for compositing
        QImage argb = rectImg.convertToFormat(QImage::Format_ARGB32_Premultiplied);

        // Track subtitle index for each rect (if multiple objects)
        // Draw at the position specified by the PGS composition
        p.drawImage(QPoint(rect->x, rect->y), argb);
    }
    p.end();

    frame.bitmap = composite;
    return frame;
}
