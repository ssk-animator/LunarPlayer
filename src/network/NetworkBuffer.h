#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QMutex>
#include <atomic>

class NetworkBuffer : public QObject
{
    Q_OBJECT
public:
    explicit NetworkBuffer(QObject *parent = nullptr);

    enum State { Idle, Buffering, Ready, Stalled, Error };

    State state() const;
    double progress() const;          // 0.0 – 1.0
    int64_t bufferedBytes() const;
    int64_t totalBytes() const;
    double bitrateBps() const;
    double latencyMs() const;
    int droppedPackets() const;

    void setTargetDurationSec(double sec);
    double targetDurationSec() const;

    void recordBytes(int64_t bytes);
    void recordPacketLoss();
    void setTotalBytes(int64_t total);
    void setState(State s);
    void reset();
    bool isPlayable() const;

    // Adaptive buffering
    double networkHealth() const; // 0.0 (poor) - 1.0 (excellent)
    void updateAdaptiveBudget(double currentBitrate);
    int64_t adaptiveTargetBytes() const;

signals:
    void stateChanged(NetworkBuffer::State state);
    void progressChanged(double pct);
    void stallWarning();

private:
    void updateBitrate();

    std::atomic<State> m_state{Idle};
    std::atomic<int64_t> m_bufferedBytes{0};
    std::atomic<int64_t> m_totalBytes{0};
    std::atomic<double> m_bitrateBps{0.0};
    std::atomic<int> m_droppedPackets{0};
    std::atomic<double> m_latencyMs{0.0};
    double m_targetDurationSec = 5.0;

    QElapsedTimer m_bitrateTimer;
    int64_t m_lastBytes = 0;
    QMutex m_mutex;
};
