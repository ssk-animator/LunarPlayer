#include "KaraokeTimeline.h"
#include <QtMath>

void KaraokeTimeline::build(const QVector<KaraokeSyllable> &syllables,
                            double subStartSec, double totalDurationSec)
{
    m_syllables.clear();
    if (syllables.isEmpty()) return;

    // Calculate total centiseconds from all syllables
    int totalCs = 0;
    for (const auto &s : syllables)
        totalCs += s.duration;

    if (totalCs <= 0) return;

    // Compute timing: each syllable occupies its proportion of total duration
    double secPerCs = totalDurationSec / totalCs;
    double currentTime = subStartSec;

    for (const auto &s : syllables) {
        Syllable syl;
        syl.duration = s.duration;
        syl.startSec = currentTime;
        syl.endSec = currentTime + s.duration * secPerCs;
        currentTime = syl.endSec;
        m_syllables.append(syl);
    }
}

int KaraokeTimeline::activeSyllable(double currentSec) const
{
    for (int i = 0; i < m_syllables.size(); ++i) {
        if (currentSec >= m_syllables[i].startSec && currentSec < m_syllables[i].endSec)
            return i;
    }
    return -1;
}

double KaraokeTimeline::syllableProgress(double currentSec) const
{
    int idx = activeSyllable(currentSec);
    if (idx < 0) return 0.0;
    const auto &s = m_syllables[idx];
    double dur = s.endSec - s.startSec;
    if (dur <= 0.0) return 0.0;
    return qBound(0.0, (currentSec - s.startSec) / dur, 1.0);
}

KaraokeTimeline::KaraokeSplit KaraokeTimeline::split(int syllableIndex,
                                                       double progress,
                                                       const QString &fullText) const
{
    KaraokeSplit result;
    result.pendingText = fullText;

    // For now, use character-based splitting based on progress
    if (syllableIndex < 0 || m_syllables.isEmpty()) {
        result.pendingText = fullText;
        return result;
    }

    // Calculate cumulative progress across all syllables up to current
    int totalCs = 0;
    int csUpTo = 0;
    for (int i = 0; i < m_syllables.size(); ++i) {
        totalCs += m_syllables[i].duration;
        if (i < syllableIndex)
            csUpTo += m_syllables[i].duration;
    }
    csUpTo += static_cast<int>(m_syllables[syllableIndex].duration * progress);

    if (totalCs <= 0) {
        result.pendingText = fullText;
        return result;
    }

    // Map progress to character position in text
    double charRatio = static_cast<double>(csUpTo) / totalCs;
    int splitPos = qBound(0, static_cast<int>(fullText.length() * charRatio), fullText.length());

    result.drawnText = fullText.left(splitPos);
    result.pendingText = fullText.mid(splitPos);
    return result;
}
