#ifndef AUDIOCLOCK_H
#define AUDIOCLOCK_H

#include <atomic>
#include <cstdint>

class AudioClock {
public:
    void setBase(double ptsSec, double deviceTimeSec) {
        m_basePts.store(ptsSec, std::memory_order_release);
        m_baseDeviceTime.store(deviceTimeSec, std::memory_order_release);
    }

    double position(double deviceTimeSec) const {
        double baseDevice = m_baseDeviceTime.load(std::memory_order_acquire);
        double basePts = m_basePts.load(std::memory_order_acquire);
        double speed = m_speed.load(std::memory_order_acquire);
        return basePts + (deviceTimeSec - baseDevice) * speed;
    }

    void setSpeed(double speed) { m_speed.store(speed, std::memory_order_release); }
    double speed() const { return m_speed.load(std::memory_order_acquire); }

    void reset() {
        m_basePts.store(0.0, std::memory_order_relaxed);
        m_baseDeviceTime.store(0.0, std::memory_order_relaxed);
        m_speed.store(1.0, std::memory_order_relaxed);
    }

    double basePts() const { return m_basePts.load(std::memory_order_acquire); }

private:
    std::atomic<double> m_basePts{0.0};
    std::atomic<double> m_baseDeviceTime{0.0};
    std::atomic<double> m_speed{1.0};
};

#endif
