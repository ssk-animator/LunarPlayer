#ifndef COMPARECONTROLLER_H
#define COMPARECONTROLLER_H

#include <QObject>

class MediaSession;

enum class CompareLockMode {
    FrameLock,   // keep both at same frame number
    TimeLock     // keep both at same timestamp
};

class CompareController : public QObject {
    Q_OBJECT
public:
    explicit CompareController(QObject *parent = nullptr);

    void setSessions(MediaSession *a, MediaSession *b);
    void setLockMode(CompareLockMode mode);
    CompareLockMode lockMode() const { return m_lockMode; }
    void setSyncLocked(bool locked) { m_syncLocked = locked; }
    bool isSyncLocked() const { return m_syncLocked; }

    // Synchronized operations (only when syncLocked)
    void seekSync(double sec);
    void seekToFrameSync(int frameNum);
    bool stepBoth(int direction);
    bool readBoth();  // returns true if at least one advanced

private:
    MediaSession *m_a = nullptr;
    MediaSession *m_b = nullptr;
    CompareLockMode m_lockMode = CompareLockMode::TimeLock;
    bool m_syncLocked = true;
};

#endif // COMPARECONTROLLER_H
