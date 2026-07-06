#ifndef KARAOKETIMELINE_H
#define KARAOKETIMELINE_H

#include "AssTypes.h"
#include <QVector>

class KaraokeTimeline {
public:
    // Syllable timing entry
    struct Syllable {
        int duration = 0;    // centiseconds (as in \k tag)
        double startSec = 0.0;
        double endSec = 0.0;
        QString text;
    };

    // Build timeline from karaoke syllables parsed by override parser
    void build(const QVector<KaraokeSyllable> &syllables, double subStartSec, double totalDurationSec);

    // Get the syllable index active at a given time (returns -1 if none)
    int activeSyllable(double currentSec) const;

    // Get interpolation progress within current syllable (0.0 to 1.0)
    double syllableProgress(double currentSec) const;

    // Splits text into drawn (highlighted) and pending portions
    struct KaraokeSplit {
        QString drawnText;
        QString pendingText;
    };
    KaraokeSplit split(int syllableIndex, double progress,
                       const QString &fullText) const;

    // Access syllables
    const QVector<Syllable>& syllables() const { return m_syllables; }
    bool isEmpty() const { return m_syllables.isEmpty(); }

private:
    QVector<Syllable> m_syllables;
};

#endif
