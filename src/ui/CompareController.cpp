#include "CompareController.h"
#include "MainWindow.h"
#include <QDebug>

CompareController::CompareController(QObject *parent)
    : QObject(parent) {}

void CompareController::setSessions(MediaSession *a, MediaSession *b)
{
    m_a = a;
    m_b = b;
}

void CompareController::setLockMode(CompareLockMode mode)
{
    m_lockMode = mode;
}

void CompareController::seekSync(double sec)
{
    if (!m_a || !m_b) return;
    m_a->seekSec(sec);
    m_b->seekSec(sec);
}

void CompareController::seekToFrameSync(int frameNum)
{
    if (!m_a || !m_b) return;
    m_a->seekToFrame(frameNum);
    if (m_lockMode == CompareLockMode::FrameLock)
        m_b->seekToFrame(frameNum);
    else
        m_b->seekToFrame(frameNum); // same for now
}

bool CompareController::stepBoth(int direction)
{
    if (!m_a || !m_b) return false;
    bool okA = (direction > 0) ? m_a->stepForward() : m_a->stepBackward();
    bool okB = (direction > 0) ? m_b->stepForward() : m_b->stepBackward();
    return okA || okB;
}

bool CompareController::readBoth()
{
    if (!m_a || !m_b) return false;
    bool aOk = m_a->readFrame();
    bool bOk = m_b->readFrame();
    return aOk || bOk;
}
