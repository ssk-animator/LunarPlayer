#ifndef SUBTITLEMANAGER_H
#define SUBTITLEMANAGER_H

#include "SubtitleFrame.h"
#include "SubtitleCache.h"
#include "SubtitleDecoder.h"
#include "SubtitleRenderer.h"
#include "ExternalSubtitleLoader.h"
#include "AssRenderCache.h"
#include <QString>
#include <QVector>
#include <QObject>
#include <memory>

struct SubtitleTrack {
    enum Source { Embedded, External };
    Source source = External;
    int streamIndex = -1;
    QString filePath;
    QString language;
    QString codecName;
    bool enabled = false;
    QVector<SubtitleFrame> frames;
};

class SubtitleManager : public QObject {
    Q_OBJECT
public:
    explicit SubtitleManager(QObject *parent = nullptr);
    ~SubtitleManager() override;

    // Media session integration
    void setMediaPath(const QString &mediaPath);
    void setFormatContext(AVFormatContext *fmtCtx);

    // Embedded subtitles
    void detectEmbeddedTracks();
    void decodeEmbeddedTrack(int trackIndex);

    // External subtitles
    void scanExternalSubtitles();
    bool loadExternalTrack(int index);
    bool loadExternalFile(const QString &filePath);

    // Active track selection
    int trackCount() const { return m_tracks.size(); }
    const SubtitleTrack* track(int index) const;
    int activeTrackIndex() const { return m_activeTrack; }
    void setActiveTrack(int index);
    void clearActiveTrack();

    // Enable/disable
    bool subtitlesEnabled() const { return m_enabled; }
    void setSubtitlesEnabled(bool enabled);

    // Render all active subtitles for the current time
    QVector<QImage> getSubtitleImages(double currentSec,
                                       int videoWidth, int videoHeight);

    // Cache management
    void clearCache();
    SubtitleCache* cache() { return m_cache.get(); }
    SubtitleRenderer* renderer() { return m_renderer.get(); }

    // Configurable memory budget (bytes)
    int64_t cacheBudgetBytes() const { return m_cache->budgetBytes(); }
    void setCacheBudgetMB(int mb);

    // Track info
    QStringList trackDisplayNames() const;

    // Style
    void setDefaultStyle(const SubtitleStyle &style);
    SubtitleStyle defaultStyle() const;

signals:
    void tracksChanged();
    void activeTrackChanged(int index);
    void subtitlesToggled(bool enabled);
    void trackLoadError(int index, const QString &error);

private:
    QString m_mediaPath;
    AVFormatContext *m_fmtCtx = nullptr;

    QVector<SubtitleTrack> m_tracks;
    int m_activeTrack = -1;
    bool m_enabled = true;

    std::unique_ptr<SubtitleDecoder> m_decoder;
    std::unique_ptr<SubtitleCache> m_cache;
    std::unique_ptr<SubtitleRenderer> m_renderer;
    std::unique_ptr<AssRenderCache> m_renderCache;
};

#endif // SUBTITLEMANAGER_H
