#ifndef AUDIODSP_H
#define AUDIODSP_H

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>

class AudioDSPModule {
public:
    virtual ~AudioDSPModule() = default;
    virtual std::string name() const = 0;
    virtual bool isEnabled() const { return m_enabled; }
    virtual void setEnabled(bool enabled) { m_enabled = enabled; }
    virtual void process(float *data, int frames, int channels) = 0;
    virtual void reset() {}

    double lastProcessMs() const { return m_lastProcessMs; }
    void setLastProcessMs(double ms) { m_lastProcessMs = ms; }

protected:
    bool m_enabled = true;
    double m_lastProcessMs = 0.0;
};

class AudioGain : public AudioDSPModule {
public:
    std::string name() const override { return "Gain"; }
    void setGain(float gain) { m_gain = gain; }
    float gain() const { return m_gain; }

    void process(float *data, int frames, int channels) override {
        if (!m_enabled || m_gain == 1.0f) return;
        int total = frames * channels;
        for (int i = 0; i < total; i++) {
            data[i] *= m_gain;
        }
    }

private:
    float m_gain = 1.0f;
};

class AudioPeakLimiter : public AudioDSPModule {
public:
    std::string name() const override { return "Peak Limiter"; }
    void setThreshold(float threshold) { m_threshold = threshold; }
    float threshold() const { return m_threshold; }

    void process(float *data, int frames, int channels) override {
        if (!m_enabled) return;
        int total = frames * channels;
        for (int i = 0; i < total; i++) {
            float v = data[i];
            if (v > m_threshold) {
                data[i] = m_threshold + (1.0f - m_threshold) * std::tanh((v - m_threshold) / (1.0f - m_threshold));
            } else if (v < -m_threshold) {
                data[i] = -m_threshold + (-1.0f + m_threshold) * std::tanh((v + m_threshold) / (-1.0f + m_threshold));
            }
        }
    }

private:
    float m_threshold = 0.95f;
};

class AudioSimpleEQ : public AudioDSPModule {
public:
    std::string name() const override { return "EQ"; }

    void setLowGain(float db) { m_lowGain = std::pow(10.0f, db / 20.0f); }
    void setMidGain(float db) { m_midGain = std::pow(10.0f, db / 20.0f); }
    void setHighGain(float db) { m_highGain = std::pow(10.0f, db / 20.0f); }

    void process(float *data, int frames, int channels) override {
        if (!m_enabled) return;
        for (int c = 0; c < channels; c++) {
            float lp = m_prevL[c];
            float hp = m_prevH[c];
            for (int f = 0; f < frames; f++) {
                float s = data[f * channels + c];
                lp = lp + (s - lp) * 0.05f;
                hp = hp + (s - hp) * 0.05f;
                float low = lp;
                float high = s - hp;
                float mid = s - low - high;
                data[f * channels + c] = low * m_lowGain + mid * m_midGain + high * m_highGain;
            }
            m_prevL[c] = lp;
            m_prevH[c] = hp;
        }
    }

    void reset() override {
        std::memset(m_prevL, 0, sizeof(m_prevL));
        std::memset(m_prevH, 0, sizeof(m_prevH));
    }

private:
    float m_lowGain = 1.0f;
    float m_midGain = 1.0f;
    float m_highGain = 1.0f;
    static const int MAX_CHANNELS = 8;
    float m_prevL[MAX_CHANNELS] = {};
    float m_prevH[MAX_CHANNELS] = {};
};

class AudioCompressor : public AudioDSPModule {
public:
    std::string name() const override { return "Compressor"; }
    void setThreshold(float threshold) { m_threshold = threshold; }
    void setRatio(float ratio) { m_ratio = ratio; }

    void process(float *data, int frames, int channels) override {
        if (!m_enabled) return;
        for (int c = 0; c < channels; c++) {
            float env = m_env[c];
            for (int f = 0; f < frames; f++) {
                float s = data[f * channels + c];
                float absS = std::fabs(s);
                env = std::max(absS, env * 0.999f);
                if (env > m_threshold) {
                    float over = env - m_threshold;
                    float compressed = m_threshold + over / m_ratio;
                    float gain = compressed / env;
                    s *= gain;
                }
                data[f * channels + c] = s;
            }
            m_env[c] = env;
        }
    }

    void reset() override { std::memset(m_env, 0, sizeof(m_env)); }

private:
    float m_threshold = 0.8f;
    float m_ratio = 4.0f;
    static const int MAX_CHANNELS = 8;
    float m_env[MAX_CHANNELS] = {};
};

class AudioDSPChain {
public:
    void addModule(std::shared_ptr<AudioDSPModule> module) {
        m_modules.push_back(module);
    }

    void process(float *data, int frames, int channels) {
        for (auto &mod : m_modules) {
            if (mod->isEnabled()) {
                auto t0 = std::chrono::steady_clock::now();
                mod->process(data, frames, channels);
                auto t1 = std::chrono::steady_clock::now();
                mod->setLastProcessMs(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
        }
    }

    void reset() {
        for (auto &mod : m_modules) mod->reset();
    }

    std::vector<std::shared_ptr<AudioDSPModule>> &modules() { return m_modules; }
    const std::vector<std::shared_ptr<AudioDSPModule>> &modules() const { return m_modules; }

private:
    std::vector<std::shared_ptr<AudioDSPModule>> m_modules;
};

#endif
