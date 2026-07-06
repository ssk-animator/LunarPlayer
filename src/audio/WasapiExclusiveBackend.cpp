#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include "WasapiExclusiveBackend.h"
#include <cstring>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

WasapiExclusiveBackend::WasapiExclusiveBackend() {}
WasapiExclusiveBackend::~WasapiExclusiveBackend() { close(); }

bool WasapiExclusiveBackend::initExclusive() {
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void **>(&m_enumerator));
    if (FAILED(hr)) return false;

    hr = m_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &m_device);
    if (FAILED(hr)) return false;

    hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void **>(&m_audioClient));
    if (FAILED(hr)) return false;

    WAVEFORMATEX waveFmt = {};
    waveFmt.wFormatTag = WAVE_FORMAT_PCM;
    waveFmt.nChannels = static_cast<WORD>(m_format.channels);
    waveFmt.nSamplesPerSec = m_format.sampleRate;
    waveFmt.wBitsPerSample = 32;
    waveFmt.nBlockAlign = static_cast<WORD>(m_format.channels * 4);
    waveFmt.nAvgBytesPerSec = m_format.sampleRate * m_format.channels * 4;
    waveFmt.cbSize = 0;

    WAVEFORMATEX *closest = nullptr;
    hr = m_audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &waveFmt, &closest);

    if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT) {
        waveFmt.wBitsPerSample = 16;
        waveFmt.nBlockAlign = static_cast<WORD>(m_format.channels * 2);
        waveFmt.nAvgBytesPerSec = m_format.sampleRate * m_format.channels * 2;
        hr = m_audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &waveFmt, &closest);
    }

    if (closest) CoTaskMemFree(closest);
    if (hr != S_OK) return false;

    REFERENCE_TIME hnsBufferDuration = static_cast<REFERENCE_TIME>(
        static_cast<double>(m_bufferFrameCount) / m_format.sampleRate * 10000000.0);

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsBufferDuration,
        hnsBufferDuration,
        &waveFmt,
        nullptr);
    if (FAILED(hr)) return false;

    UINT32 bufFrames = 0;
    m_audioClient->GetBufferSize(&bufFrames);
    m_bufferFrameCount = static_cast<int>(bufFrames);
    m_bytesPerFrame = waveFmt.nBlockAlign;

    hr = m_audioClient->GetService(__uuidof(IAudioRenderClient),
        reinterpret_cast<void **>(&m_renderClient));
    if (FAILED(hr)) return false;

    m_audioClient->GetService(__uuidof(IAudioClock),
        reinterpret_cast<void **>(&m_audioClock));

    m_eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_eventHandle) return false;

    hr = m_audioClient->SetEventHandle(m_eventHandle);
    if (FAILED(hr)) return false;

    return true;
}

bool WasapiExclusiveBackend::open(const AudioFormat &format) {
    close();
    m_format = format;
    m_bufferFrameCount = preferredBufferFrames();

    if (!initExclusive()) return false;

    m_audioClient->Start();
    m_opened = true;
    m_totalFramesWritten.store(0);
    m_underruns = 0;
    m_overruns = 0;

    return true;
}

void WasapiExclusiveBackend::close() {
    if (m_eventHandle) { CloseHandle(m_eventHandle); m_eventHandle = nullptr; }
    if (m_audioClock) { m_audioClock->Release(); m_audioClock = nullptr; }
    if (m_renderClient) { m_renderClient->Release(); m_renderClient = nullptr; }
    if (m_audioClient) { m_audioClient->Release(); m_audioClient = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }
    if (m_enumerator) { m_enumerator->Release(); m_enumerator = nullptr; }
    m_opened = false;
}

int WasapiExclusiveBackend::write(const float *data, int frames) {
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_opened || !m_renderClient) return 0;

    if (m_eventHandle) {
        DWORD wait = WaitForSingleObject(m_eventHandle, 100);
        if (wait != WAIT_OBJECT_0) return 0;
    }

    UINT32 paddingFrames = 0;
    if (m_audioClient->GetCurrentPadding(&paddingFrames) != S_OK) return 0;

    int framesAvailable = m_bufferFrameCount - static_cast<int>(paddingFrames);
    int toWrite = std::min(frames, framesAvailable);
    if (toWrite <= 0) return 0;

    BYTE *pData = nullptr;
    HRESULT hr = m_renderClient->GetBuffer(toWrite, &pData);
    if (FAILED(hr) || !pData) return 0;

    const float *src = data;
    float *dst = reinterpret_cast<float *>(pData);
    int channels = m_format.channels;
    float vol = m_volume;

    for (int i = 0; i < toWrite * channels; i++) {
        dst[i] = src[i] * vol;
    }

    m_renderClient->ReleaseBuffer(toWrite, 0);
    m_totalFramesWritten.fetch_add(toWrite, std::memory_order_release);
    return toWrite;
}

double WasapiExclusiveBackend::currentPositionSec() const {
    if (!m_audioClock) return 0.0;
    UINT64 freq = 0, pos = 0;
    m_audioClock->GetFrequency(&freq);
    m_audioClock->GetPosition(&pos, nullptr);
    return freq > 0 ? static_cast<double>(pos) / static_cast<double>(freq) : 0.0;
}

double WasapiExclusiveBackend::getLatencySec() const {
    return m_bufferFrameCount > 0 ? static_cast<double>(m_bufferFrameCount) / m_format.sampleRate : 0.0;
}

void WasapiExclusiveBackend::setVolume(float vol) { m_volume = std::max(0.0f, std::min(1.0f, vol)); }

void WasapiExclusiveBackend::reset() {
    if (m_audioClient) m_audioClient->Stop();
    m_totalFramesWritten.store(0);
}

AudioBackendStats WasapiExclusiveBackend::stats() const {
    AudioBackendStats s;
    s.totalFramesWritten = m_totalFramesWritten.load();
    s.bufferUnderruns = m_underruns;
    s.bufferOverruns = m_overruns;
    s.averageLatencyMs = getLatencySec() * 1000.0;
    s.totalBufferCount = 1;
    return s;
}

static GUID bitstreamFormatGuid(BitstreamCodec codec) {
    switch (codec) {
    case BitstreamCodec::AC3:    return KSDATAFORMAT_SUBTYPE_AC3_AUDIO;
    case BitstreamCodec::EAC3:   return KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS;
    case BitstreamCodec::DTS:    return KSDATAFORMAT_SUBTYPE_DTS_AUDIO;
    case BitstreamCodec::DTS_HD: return KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD;
    case BitstreamCodec::TrueHD: {
        static const GUID guid = {0x4AF2B554, 0x3E7A, 0x4C75, {0x9C, 0x58, 0x2A, 0x8F, 0xEC, 0xAE, 0x42, 0xD3}};
        return guid;
    }
    default: return GUID_NULL;
    }
}

bool WasapiExclusiveBackend::deviceSupportsPassthrough(BitstreamCodec codec) const {
    if (!m_device) return false;

    IAudioClient *testClient = nullptr;
    HRESULT hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void **>(&testClient));
    if (FAILED(hr) || !testClient) return false;

    GUID subType = bitstreamFormatGuid(codec);
    if (subType == GUID_NULL) { testClient->Release(); return false; }

    WAVEFORMATEXTENSIBLE wfxt = {};
    wfxt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfxt.Format.nChannels = 2;
    wfxt.Format.nSamplesPerSec = 48000;
    wfxt.Format.wBitsPerSample = 16;
    wfxt.Format.nBlockAlign = 4;
    wfxt.Format.nAvgBytesPerSec = 192000;
    wfxt.Format.cbSize = 22;
    wfxt.SubFormat = subType;
    wfxt.Samples.wValidBitsPerSample = 16;
    wfxt.dwChannelMask = KSAUDIO_SPEAKER_STEREO;

    WAVEFORMATEX *closest = nullptr;
    hr = testClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,
        reinterpret_cast<WAVEFORMATEX *>(&wfxt), &closest);

    if (closest) CoTaskMemFree(closest);
    testClient->Release();
    return hr == S_OK;
}

bool WasapiExclusiveBackend::openPassthrough(BitstreamCodec codec, int sampleRate, int bitrate) {
    close();
    if (!m_device) return false;

    HRESULT hr = m_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void **>(&m_audioClient));
    if (FAILED(hr) || !m_audioClient) return false;

    GUID subType = bitstreamFormatGuid(codec);

    WAVEFORMATEXTENSIBLE wfxt = {};
    wfxt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfxt.Format.nChannels = 2;
    wfxt.Format.nSamplesPerSec = sampleRate > 0 ? sampleRate : 48000;
    wfxt.Format.wBitsPerSample = 16;
    wfxt.Format.nBlockAlign = 4;
    wfxt.Format.nAvgBytesPerSec = bitrate > 0 ? bitrate / 8 : 192000;
    wfxt.Format.cbSize = 22;
    wfxt.SubFormat = subType;
    wfxt.Samples.wValidBitsPerSample = 16;
    wfxt.dwChannelMask = KSAUDIO_SPEAKER_STEREO;

    REFERENCE_TIME hnsBufferDuration = static_cast<REFERENCE_TIME>(
        static_cast<double>(48000) / wfxt.Format.nSamplesPerSec * 10000000.0);

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsBufferDuration, hnsBufferDuration,
        reinterpret_cast<WAVEFORMATEX *>(&wfxt), nullptr);
    if (FAILED(hr)) { m_audioClient->Release(); m_audioClient = nullptr; return false; }

    UINT32 bufFrames = 0;
    m_audioClient->GetBufferSize(&bufFrames);
    m_bufferFrameCount = static_cast<int>(bufFrames);
    m_bytesPerFrame = wfxt.Format.nBlockAlign;

    hr = m_audioClient->GetService(__uuidof(IAudioRenderClient),
        reinterpret_cast<void **>(&m_renderClient));
    if (FAILED(hr)) { m_audioClient->Release(); m_audioClient = nullptr; return false; }

    m_audioClient->GetService(__uuidof(IAudioClock),
        reinterpret_cast<void **>(&m_audioClock));

    m_eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_eventHandle) return false;

    hr = m_audioClient->SetEventHandle(m_eventHandle);
    if (FAILED(hr)) return false;

    hr = m_audioClient->Start();
    if (FAILED(hr)) return false;

    m_passthroughCodec = codec;
    m_passthroughMode = true;
    m_opened = true;
    m_totalFramesWritten.store(0);

    fprintf(stderr, "[WASAPI-EXCL] Passthrough opened: codec=%d sampleRate=%d\n",
            (int)codec, wfxt.Format.nSamplesPerSec);
    fflush(stderr);

    return true;
}

int WasapiExclusiveBackend::writePassthrough(const uint8_t *data, int size) {
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_opened || !m_renderClient || !m_passthroughMode) return 0;

    if (m_eventHandle) {
        DWORD wait = WaitForSingleObject(m_eventHandle, 100);
        if (wait != WAIT_OBJECT_0) return 0;
    }

    UINT32 paddingFrames = 0;
    if (m_audioClient->GetCurrentPadding(&paddingFrames) != S_OK) return 0;

    int bytesAvailable = m_bufferFrameCount * m_bytesPerFrame -
                         static_cast<int>(paddingFrames) * m_bytesPerFrame;
    int toWrite = std::min(size, bytesAvailable);
    if (toWrite <= 0) return 0;

    BYTE *pData = nullptr;
    HRESULT hr = m_renderClient->GetBuffer(toWrite / m_bytesPerFrame, &pData);
    if (FAILED(hr) || !pData) return 0;

    std::memcpy(pData, data, toWrite);

    m_renderClient->ReleaseBuffer(toWrite / m_bytesPerFrame, 0);
    m_totalFramesWritten.fetch_add(toWrite / m_bytesPerFrame, std::memory_order_release);
    return toWrite;
}
