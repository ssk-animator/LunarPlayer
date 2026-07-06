#include "NetworkBuffer.h"
#include <algorithm>
#include <cmath>

NetworkBuffer::NetworkBuffer(QObject *parent)
    : QObject(parent)
{
    m_bitrateTimer.start();
}

NetworkBuffer::State NetworkBuffer::state() const { return m_state.load(); }
double NetworkBuffer::progress() const
{
    int64_t tot = m_totalBytes.load();
    if (tot <= 0) return 0.0;
    return std::min(1.0, static_cast<double>(m_bufferedBytes.load()) / tot);
}
int64_t NetworkBuffer::bufferedBytes() const { return m_bufferedBytes.load(); }
int64_t NetworkBuffer::totalBytes() const { return m_totalBytes.load(); }
double NetworkBuffer::bitrateBps() const { return m_bitrateBps.load(); }
double NetworkBuffer::latencyMs() const { return m_latencyMs.load(); }
int NetworkBuffer::droppedPackets() const { return m_droppedPackets.load(); }

void NetworkBuffer::setTargetDurationSec(double sec) { m_targetDurationSec = std::max(1.0, sec); }
double NetworkBuffer::targetDurationSec() const { return m_targetDurationSec; }

void NetworkBuffer::recordBytes(int64_t bytes)
{
    m_bufferedBytes.fetch_add(bytes);
    if (m_bitrateTimer.elapsed() >= 1000) {
        updateBitrate();
        m_bitrateTimer.restart();
    }
    emit progressChanged(progress());
}

void NetworkBuffer::recordPacketLoss()
{
    m_droppedPackets.fetch_add(1);
}

void NetworkBuffer::setTotalBytes(int64_t total)
{
    m_totalBytes.store(total);
}

void NetworkBuffer::setState(State s)
{
    State old = m_state.exchange(s);
    if (old != s) {
        emit stateChanged(s);
        if (s == Stalled)
            emit stallWarning();
    }
}

void NetworkBuffer::reset()
{
    m_bufferedBytes.store(0);
    m_totalBytes.store(0);
    m_bitrateBps.store(0.0);
    m_droppedPackets.store(0);
    m_latencyMs.store(0.0);
    setState(Idle);
    m_bitrateTimer.restart();
    m_lastBytes = 0;
}

bool NetworkBuffer::isPlayable() const
{
    State s = m_state.load();
    return s == Ready || s == Buffering || s == Stalled;
}

void NetworkBuffer::updateBitrate()
{
    QMutexLocker lock(&m_mutex);
    int64_t current = m_bufferedBytes.load();
    int64_t delta = current - m_lastBytes;
    m_lastBytes = current;
    double rate = delta * 8.0; // bits in the last second
    if (rate > 0.0)
        m_bitrateBps.store(rate);
}

double NetworkBuffer::networkHealth() const
{
    int64_t buf = m_bufferedBytes.load();
    int64_t tot = m_totalBytes.load();
    double ratio = (tot > 0) ? static_cast<double>(buf) / tot : 1.0;
    int drops = m_droppedPackets.load();
    double dropPenalty = std::min(1.0, drops * 0.1);
    return std::max(0.0, std::min(1.0, ratio - dropPenalty));
}

void NetworkBuffer::updateAdaptiveBudget(double currentBitrate)
{
    double health = networkHealth();
    if (health > 0.8) {
        // Good connection: smaller target (reduce latency)
        m_targetDurationSec = std::max(2.0, m_targetDurationSec * 0.95);
    } else if (health < 0.3) {
        // Poor connection: increase target (more buffering)
        m_targetDurationSec = std::min(30.0, m_targetDurationSec * 1.1);
    }
    if (droppedPackets() > 5) {
        m_targetDurationSec = std::min(30.0, m_targetDurationSec * 1.25);
    }
}

int64_t NetworkBuffer::adaptiveTargetBytes() const
{
    double bitrate = m_bitrateBps.load();
    if (bitrate <= 0) return 10 * 1024 * 1024; // 10MB fallback
    return static_cast<int64_t>(bitrate / 8.0 * m_targetDurationSec);
}
