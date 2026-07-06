#include "SubtitleManager.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPainter>

// Default cache budget: 128 MB
static constexpr int64_t kDefaultBudgetBytes = 128LL * 1024 * 1024;

SubtitleManager::SubtitleManager(QObject *parent)
    : QObject(parent)
    , m_cache(std::make_unique<SubtitleCache>(kDefaultBudgetBytes))
    , m_renderer(std::make_unique<SubtitleRenderer>())
    , m_renderCache(std::make_unique<AssRenderCache>(kDefaultBudgetBytes))
{
    m_renderer->setRenderCache(m_renderCache.get());
}

SubtitleManager::~SubtitleManager() = default;

void SubtitleManager::setMediaPath(const QString &mediaPath)
{
    m_mediaPath = mediaPath;
    m_tracks.clear();
    m_activeTrack = -1;
    m_decoder.reset();
    m_cache->clear();
}

void SubtitleManager::setFormatContext(AVFormatContext *fmtCtx)
{
    m_fmtCtx = fmtCtx;
}

void SubtitleManager::detectEmbeddedTracks()
{
    if (!m_fmtCtx) return;
    for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
        AVStream *st = m_fmtCtx->streams[i];
        if (st->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
        SubtitleTrack track;
        track.source = SubtitleTrack::Embedded;
        track.streamIndex = (int)i;
        track.enabled = false;
        const char *codecName = avcodec_get_name(st->codecpar->codec_id);
        track.codecName = QString::fromUtf8(codecName);
        AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
        if (lang) track.language = QString::fromUtf8(lang->value);
        AVDictionaryEntry *title = av_dict_get(st->metadata, "title", nullptr, 0);
        if (title) track.codecName += " (" + QString::fromUtf8(title->value) + ")";
        m_tracks.append(track);
    }
}

void SubtitleManager::decodeEmbeddedTrack(int trackIndex)
{
    if (!m_fmtCtx || trackIndex < 0 || trackIndex >= m_tracks.size()) return;
    SubtitleTrack &track = m_tracks[trackIndex];
    if (track.source != SubtitleTrack::Embedded) return;
    if (!track.frames.isEmpty()) return; // already decoded

    track.frames.clear();

    m_decoder = std::make_unique<SubtitleDecoder>();
    if (!m_decoder->openStream(m_fmtCtx, track.streamIndex)) {
        m_decoder.reset();
        return;
    }

    int64_t savedPos = avio_tell(m_fmtCtx->pb);
    av_seek_frame(m_fmtCtx, track.streamIndex, 0, AVSEEK_FLAG_ANY);
    AVPacket *pkt = av_packet_alloc();
    int pktCount = 0;
    while (av_read_frame(m_fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == track.streamIndex) {
            ++pktCount;
            int idx = (int)track.frames.size();
            m_decoder->decode(pkt, track.frames);
            for (int j = idx; j < track.frames.size(); ++j) {
                track.frames[j].trackIndex = track.streamIndex;
                track.frames[j].subtitleIndex = j;
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_seek_frame(m_fmtCtx, -1, savedPos, AVSEEK_FLAG_BYTE);
}

void SubtitleManager::scanExternalSubtitles()
{
    if (m_mediaPath.isEmpty()) return;
    QStringList files = ExternalSubtitleLoader::scanDirectory(m_mediaPath);
    QFileInfo fi(m_mediaPath);
    QString exactBase = fi.completeBaseName();
    QString dir = fi.absolutePath();
    QDir d(dir);
    for (const QString &ext : ExternalSubtitleLoader::supportedExtensions()) {
        QString candidate = d.absoluteFilePath(exactBase + "." + ext);
        if (QFileInfo::exists(candidate) && !files.contains(candidate))
            files.append(candidate);
    }
    for (const QString &file : files) {
        SubtitleTrack track;
        track.source = SubtitleTrack::External;
        track.filePath = file;
        track.codecName = ExternalSubtitleLoader::detectCodec(file);
        track.language = ExternalSubtitleLoader::extractLanguage(QFileInfo(file).fileName());
        m_tracks.append(track);
    }
}

bool SubtitleManager::loadExternalTrack(int index)
{
    if (index < 0 || index >= m_tracks.size()) return false;
    SubtitleTrack &track = m_tracks[index];
    if (track.source != SubtitleTrack::External) return false;
    if (!track.frames.isEmpty()) return true; // already loaded

    ExternalSubtitleTrack extTrack = ExternalSubtitleLoader::loadFile(track.filePath);
    if (!extTrack.loaded) {
        emit trackLoadError(index, extTrack.errorMsg);
        return false;
    }
    track.frames = extTrack.frames;
    track.codecName = extTrack.codecName;
    // Assign stable indices
    for (int i = 0; i < track.frames.size(); ++i) {
        track.frames[i].trackIndex = index;
        track.frames[i].subtitleIndex = i;
    }
    return true;
}

bool SubtitleManager::loadExternalFile(const QString &filePath)
{
    ExternalSubtitleTrack extTrack = ExternalSubtitleLoader::loadFile(filePath);
    if (!extTrack.loaded) return false;
    SubtitleTrack track;
    track.source = SubtitleTrack::External;
    track.filePath = filePath;
    track.codecName = extTrack.codecName;
    track.language = extTrack.language;
    track.frames = extTrack.frames;
    track.enabled = false;
    // Assign stable indices
    int trackIdx = m_tracks.size();
    for (int i = 0; i < track.frames.size(); ++i) {
        track.frames[i].trackIndex = trackIdx;
        track.frames[i].subtitleIndex = i;
    }
    m_tracks.append(track);
    emit tracksChanged();
    return true;
}

const SubtitleTrack* SubtitleManager::track(int index) const
{
    if (index < 0 || index >= m_tracks.size()) return nullptr;
    return &m_tracks[index];
}

void SubtitleManager::setActiveTrack(int index)
{
    if (index < -1 || index >= m_tracks.size()) return;
    if (m_activeTrack >= 0 && m_activeTrack < m_tracks.size())
        m_tracks[m_activeTrack].enabled = false;
    m_activeTrack = index;
    if (index >= 0 && index < m_tracks.size()) {
        m_tracks[index].enabled = true;
        if (m_tracks[index].frames.isEmpty()) {
            if (m_tracks[index].source == SubtitleTrack::External)
                loadExternalTrack(index);
            else if (m_tracks[index].source == SubtitleTrack::Embedded)
                decodeEmbeddedTrack(index);
        }
    }
    m_cache->clear();
    emit activeTrackChanged(index);
}

void SubtitleManager::clearActiveTrack()
{
    setActiveTrack(-1);
}

void SubtitleManager::setSubtitlesEnabled(bool enabled)
{
    m_enabled = enabled;
    emit subtitlesToggled(enabled);
}

QVector<QImage> SubtitleManager::getSubtitleImages(double currentSec,
                                                     int videoWidth, int videoHeight)
{
    QVector<QImage> result;
    if (!m_enabled || m_activeTrack < 0 || m_activeTrack >= m_tracks.size())
        return result;

    SubtitleTrack &track = m_tracks[m_activeTrack];
    if (track.frames.isEmpty()) return result;

    // Set render time on all frames for animation engine
    int64_t renderPtsMs = static_cast<int64_t>(currentSec * 1000.0);
    for (int i = 0; i < track.frames.size(); ++i)
        track.frames[i].renderPTS = renderPtsMs;

    // First pass: check if any frames are active
    bool anyActive = false;
    for (int i = 0; i < track.frames.size(); ++i) {
        if (track.frames[i].isActiveSec(currentSec)) {
            anyActive = true;
            break;
        }
    }
    if (!anyActive) return result;

    // Collect active frames sorted by layer
    QVector<SubtitleFrame*> activeFrames;
    for (int i = 0; i < track.frames.size(); ++i) {
        if (track.frames[i].isActiveSec(currentSec))
            activeFrames.append(&track.frames[i]);
    }

    if (activeFrames.isEmpty()) return result;

    // Sort by layer (higher layer renders on top)
    std::sort(activeFrames.begin(), activeFrames.end(),
              [](const SubtitleFrame *a, const SubtitleFrame *b) {
                  return a->layer < b->layer;
              });

    QImage canvas(videoWidth, videoHeight, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);

    for (SubtitleFrame *frame : activeFrames) {
        uint64_t key = frame->cacheKey();
        bool isAnimated = (frame->type == SubtitleType::ASS)
                        && AssRenderCache::hasAnimationTags(frame->text);

        // Skip surface cache for animated subtitles — render every frame
        if (!isAnimated) {
            SubtitleSurface cached;
            if (m_cache->lookup(key, cached)) {
                p.drawImage(cached.posX(), cached.posY(), cached.toImage());
                continue;
            }
        }

        QVector<SubtitleSurface> surfaces = m_renderer->render(*frame, videoWidth, videoHeight);
        for (const SubtitleSurface &surface : surfaces) {
            if (surface.isNull()) continue;
            if (!isAnimated)
                m_cache->insert(key, surface);
            p.drawImage(surface.posX(), surface.posY(), surface.toImage());
        }
    }

    p.end();
    result.append(canvas);
    return result;
}

void SubtitleManager::clearCache()
{
    m_cache->clear();
}

void SubtitleManager::setCacheBudgetMB(int mb)
{
    m_cache->setBudgetBytes(static_cast<int64_t>(mb) * 1024 * 1024);
}

QStringList SubtitleManager::trackDisplayNames() const
{
    QStringList names;
    for (const SubtitleTrack &track : m_tracks) {
        QString name;
        if (track.source == SubtitleTrack::Embedded) {
            name = QString("[Embedded %1]").arg(track.codecName);
        } else {
            name = QFileInfo(track.filePath).fileName();
        }
        if (!track.language.isEmpty())
            name = QString("[%1] %2").arg(track.language, name);
        names.append(name);
    }
    return names;
}

void SubtitleManager::setDefaultStyle(const SubtitleStyle &style)
{
    m_renderer->setDefaultStyle(style);
}

SubtitleStyle SubtitleManager::defaultStyle() const
{
    return m_renderer->defaultStyle();
}
