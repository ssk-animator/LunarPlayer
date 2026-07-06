#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include "WasapiSharedBackend.h"
#include <cstring>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

WasapiSharedBackend::WasapiSharedBackend() {}
WasapiSharedBackend::~WasapiSharedBackend() { close(); }

bool WasapiSharedBackend::open(const AudioFormat &format) {
    close();
    m_format = format;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool comWasAlreadyInit = (hr == S_FALSE);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void **>(&m_enumerator));
    if (FAILED(hr)) { fprintf(stderr, "[WASAPI-SHARE] CoCreateInstance FAILED hr=0x%08lx\n", hr); fflush(stderr); return false; }

    hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
    if (FAILED(hr)) { fprintf(stderr, "[WASAPI-SHARE] GetDefaultAudioEndpoint FAILED hr=0x%08lx\n", hr); fflush(stderr); return false; }

    hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void **>(&m_audioClient));
    if (FAILED(hr)) { fprintf(stderr, "[WASAPI-SHARE] Activate FAILED hr=0x%08lx\n", hr); fflush(stderr); return false; }

    WAVEFORMATEX *deviceFormat = nullptr;
    hr = m_audioClient->GetMixFormat(&deviceFormat);
    if (FAILED(hr) || !deviceFormat) {
        fprintf(stderr, "[WASAPI-SHARE] GetMixFormat FAILED hr=0x%08lx\n", hr);
        fflush(stderr);
        return false;
    }

    fprintf(stderr, "[WASAPI-SHARE] Device mix format: tag=%d %dHz %dch bits=%d blockAlign=%d avgBytes=%d cbSize=%d\n",
            deviceFormat->wFormatTag, deviceFormat->nSamplesPerSec, deviceFormat->nChannels,
            deviceFormat->wBitsPerSample, deviceFormat->nBlockAlign, deviceFormat->nAvgBytesPerSec,
            deviceFormat->cbSize);
    fflush(stderr);

    REFERENCE_TIME hnsBufferDuration = static_cast<REFERENCE_TIME>(
        static_cast<double>(preferredBufferFrames()) / format.sampleRate * 10000000.0);

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 0,
        hnsBufferDuration, 0,
        deviceFormat, nullptr);

    if (FAILED(hr)) {
        fprintf(stderr, "[WASAPI-SHARE] Initialize with device format FAILED hr=0x%08lx\n", hr);
        fflush(stderr);
        CoTaskMemFree(deviceFormat);
        return false;
    }

    bool isFloat = false;
    if (deviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        isFloat = true;
    } else if (deviceFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && deviceFormat->cbSize >= 22) {
        auto *wfxExt = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(deviceFormat);
        isFloat = (wfxExt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    m_usesPcm16 = !isFloat;
    m_deviceBitsPerSample = deviceFormat->wBitsPerSample;

    m_deviceChannels = deviceFormat->nChannels;
    m_deviceSampleRate = deviceFormat->nSamplesPerSec;
    m_bytesPerFrame = deviceFormat->nBlockAlign;

    CoTaskMemFree(deviceFormat);

    UINT32 bufFrames = 0;
    hr = m_audioClient->GetBufferSize(&bufFrames);
    if (FAILED(hr)) {
        fprintf(stderr, "[WASAPI-SHARE] GetBufferSize FAILED hr=0x%08lx\n", hr);
        fflush(stderr);
        return false;
    }
    m_bufferFrameCount = static_cast<int>(bufFrames);

    hr = m_audioClient->GetService(__uuidof(IAudioRenderClient),
        reinterpret_cast<void **>(&m_renderClient));
    if (FAILED(hr)) {
        fprintf(stderr, "[WASAPI-SHARE] GetService(RenderClient) FAILED hr=0x%08lx\n", hr);
        fflush(stderr);
        return false;
    }

    hr = m_audioClient->GetService(__uuidof(IAudioClock),
        reinterpret_cast<void **>(&m_audioClock));
    if (FAILED(hr)) {
        fprintf(stderr, "[WASAPI-SHARE] GetService(AudioClock) FAILED hr=0x%08lx\n", hr);
        fflush(stderr);
        return false;
    }

    m_opened = true;
    m_totalFramesWritten.store(0);
    m_underruns = 0;
    m_overruns = 0;

    const char *fmtName = isFloat ? "F32" : (m_deviceBitsPerSample == 16 ? "S16" : m_deviceBitsPerSample == 24 ? "S24" : "S32");
    fprintf(stderr, "[WASAPI-SHARE] Opened: %dHz %dch -> %s (device %dch %s), bufFrames=%d, latency=%.1fms\n",
            format.sampleRate, format.channels,
            fmtName,
            m_deviceChannels, fmtName,
            m_bufferFrameCount,
            getLatencySec() * 1000.0);
    fflush(stderr);

    return true;
}

void WasapiSharedBackend::startPlayback() {
    if (!m_opened || m_threadRunning) return;

    m_threadRunning = true;
    m_threadHandle = CreateThread(nullptr, 0, renderThreadProc, this, 0, nullptr);
    if (!m_threadHandle) {
        fprintf(stderr, "[WASAPI-SHARE] CreateThread FAILED err=%lu\n", GetLastError());
        fflush(stderr);
        m_threadRunning = false;
        return;
    }
    fprintf(stderr, "[WASAPI-SHARE] Render thread started\n");
    fflush(stderr);
}

void WasapiSharedBackend::close() {
    m_threadRunning = false;

    if (m_threadHandle) {
        WaitForSingleObject(m_threadHandle, 2000);
        CloseHandle(m_threadHandle);
        m_threadHandle = nullptr;
    }

    if (m_audioClock) { m_audioClock->Release(); m_audioClock = nullptr; }
    if (m_renderClient) { m_renderClient->Release(); m_renderClient = nullptr; }
    if (m_audioClient) { m_audioClient->Release(); m_audioClient = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    if (m_enumerator) { m_enumerator->Release(); m_enumerator = nullptr; }

    m_opened = false;
}

DWORD WINAPI WasapiSharedBackend::renderThreadProc(LPVOID param) {
    auto *self = static_cast<WasapiSharedBackend *>(param);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    self->m_audioClient->Start();

    int pollMs = 5;

    while (self->m_threadRunning) {
        Sleep(pollMs);

        UINT32 paddingFrames = 0;
        HRESULT hr = self->m_audioClient->GetCurrentPadding(&paddingFrames);
        if (FAILED(hr)) break;

        int framesAvailable = self->m_bufferFrameCount - static_cast<int>(paddingFrames);
        if (framesAvailable <= 0) continue;

        BYTE *pData = nullptr;
        hr = self->m_renderClient->GetBuffer(framesAvailable, &pData);
        if (FAILED(hr) || !pData) continue;

        int framesToRender = std::min(framesAvailable, self->m_bufferFrameCount / 2);

        float tempBuf[4800 * 8];
        int got = (self->m_ringBuf) ? self->m_ringBuf->read(tempBuf, framesToRender) : 0;

        int ch = self->m_format.channels;
        float vol = self->m_volume;
        int totalSamples = got * ch;

        if (self->m_deviceBitsPerSample == 16) {
            short *dst = reinterpret_cast<short *>(pData);
            for (int i = 0; i < totalSamples; i++) {
                float v = tempBuf[i] * vol;
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                dst[i] = static_cast<short>(v * 32767.0f);
            }
        } else if (self->m_deviceBitsPerSample == 24) {
            BYTE *dst = pData;
            for (int i = 0; i < totalSamples; i++) {
                float v = tempBuf[i] * vol;
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                int32_t s = static_cast<int32_t>(v * 8388607.0f);
                dst[0] = static_cast<BYTE>(s & 0xFF);
                dst[1] = static_cast<BYTE>((s >> 8) & 0xFF);
                dst[2] = static_cast<BYTE>((s >> 16) & 0xFF);
                dst += 3;
            }
        } else if (self->m_deviceBitsPerSample == 32 && !self->m_usesPcm16) {
            float *dst = reinterpret_cast<float *>(pData);
            for (int i = 0; i < totalSamples; i++) {
                dst[i] = tempBuf[i] * vol;
            }
        } else {
            int32_t *dst = reinterpret_cast<int32_t *>(pData);
            for (int i = 0; i < totalSamples; i++) {
                float v = tempBuf[i] * vol;
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                dst[i] = static_cast<int32_t>(v * 2147483647.0f);
            }
        }

        self->m_renderClient->ReleaseBuffer(got, 0);
        self->m_totalFramesWritten.fetch_add(got, std::memory_order_release);

        if (got < framesToRender) {
            self->m_underruns++;
        }
    }

    self->m_audioClient->Stop();

    CoUninitialize();
    return 0;
}

int WasapiSharedBackend::write(const float *data, int frames) {
    if (!m_opened || !m_ringBuf) return 0;
    return m_ringBuf->write(data, frames);
}

double WasapiSharedBackend::currentPositionSec() const {
    if (!m_audioClock) return 0.0;
    UINT64 freq = 0, pos = 0;
    m_audioClock->GetFrequency(&freq);
    m_audioClock->GetPosition(&pos, nullptr);
    return freq > 0 ? static_cast<double>(pos) / static_cast<double>(freq) : 0.0;
}

double WasapiSharedBackend::getLatencySec() const {
    if (!m_audioClient) return 0.0;
    REFERENCE_TIME hnsLatency = 0;
    m_audioClient->GetStreamLatency(&hnsLatency);
    return static_cast<double>(hnsLatency) / 10000000.0;
}

void WasapiSharedBackend::setVolume(float vol) { m_volume = std::max(0.0f, std::min(1.0f, vol)); }

void WasapiSharedBackend::reset() {
    if (m_audioClient) m_audioClient->Stop();

    // Stop the render thread so startPlayback() can create a new one
    m_threadRunning = false;
    if (m_threadHandle) {
        WaitForSingleObject(m_threadHandle, 2000);
        CloseHandle(m_threadHandle);
        m_threadHandle = nullptr;
    }

    m_totalFramesWritten.store(0);
    if (m_ringBuf) m_ringBuf->reset();

    // Restart the WASAPI audio client so it's ready for the next startPlayback()
    if (m_audioClient) m_audioClient->Start();
}

AudioBackendStats WasapiSharedBackend::stats() const {
    AudioBackendStats s;
    s.totalFramesWritten = m_totalFramesWritten.load();
    s.bufferUnderruns = m_underruns;
    s.bufferOverruns = m_overruns;
    s.averageLatencyMs = getLatencySec() * 1000.0;
    s.totalBufferCount = 1;
    return s;
}
