#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include "WaveOutBackend.h"
#include <cstring>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

static void CALLBACK waveOutCallbackStatic(HWAVEOUT hwo, UINT uMsg,
                                            DWORD_PTR dwInstance, DWORD_PTR dwParam1,
                                            DWORD_PTR /*dwParam2*/)
{
    if (uMsg != WOM_DONE) return;
    WaveOutBackend *self = reinterpret_cast<WaveOutBackend*>(dwInstance);
    WAVEHDR *hdr = reinterpret_cast<WAVEHDR*>(dwParam1);
    int idx = static_cast<int>(hdr->dwUser);
    self->onBufferDone(idx);
}

WaveOutBackend::WaveOutBackend()
{
    for (int i = 0; i < NUM_BUFFERS; i++) {
        m_buffers[i].data = new short[BUFFER_FRAMES * 32];  // max 32 channels
        m_headers[i] = nullptr;
    }
}

WaveOutBackend::~WaveOutBackend()
{
    close();
    for (int i = 0; i < NUM_BUFFERS; i++)
        delete[] m_buffers[i].data;
}

bool WaveOutBackend::open(const AudioFormat &format)
{
    close();
    m_format = format;

    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = static_cast<WORD>(format.channels);
    wf.nSamplesPerSec = format.sampleRate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = static_cast<WORD>(format.channels * 2);
    wf.nAvgBytesPerSec = format.sampleRate * format.channels * 2;

    HWAVEOUT hwo = nullptr;
    MMRESULT res = waveOutOpen(&hwo, WAVE_MAPPER, &wf,
        reinterpret_cast<DWORD_PTR>(&waveOutCallbackStatic),
        reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (res != MMSYSERR_NOERROR)
        return false;

    m_hWaveOut = static_cast<void*>(hwo);
    m_totalFramesWritten = 0;
    m_accumFrames = 0;
    m_opened = true;

    fprintf(stderr, "[WAVEOUT] Opened: %dHz, %dch, %dbit, blockAlign=%d, avgBytes=%d\n",
            format.sampleRate, format.channels, 16,
            format.channels * 2, format.sampleRate * format.channels * 2);
    fflush(stderr);

    // Allocate accumulator
    m_accum = new float[BUFFER_FRAMES * format.channels];

    for (int i = 0; i < NUM_BUFFERS; i++) {
        WAVEHDR *hdr = new WAVEHDR();
        memset(hdr, 0, sizeof(WAVEHDR));
        hdr->lpData = reinterpret_cast<LPSTR>(m_buffers[i].data);
        hdr->dwBufferLength = BUFFER_FRAMES * format.channels * 2;
        hdr->dwUser = static_cast<DWORD_PTR>(i);
        m_headers[i] = hdr;
    }

    return true;
}

void WaveOutBackend::close()
{
    if (m_hWaveOut) {
        HWAVEOUT hwo = static_cast<HWAVEOUT>(m_hWaveOut);
        waveOutReset(hwo);
        for (int i = 0; i < NUM_BUFFERS; i++) {
            if (m_headers[i]) {
                WAVEHDR *hdr = static_cast<WAVEHDR*>(m_headers[i]);
                if (hdr->dwFlags & WHDR_PREPARED)
                    waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
                delete hdr;
                m_headers[i] = nullptr;
            }
            m_buffers[i].active = false;
            m_buffers[i].playing = false;
            m_buffers[i].prepared = false;
        }
        waveOutClose(hwo);
        m_hWaveOut = nullptr;
    }
    if (m_accum) {
        delete[] m_accum;
        m_accum = nullptr;
    }
    m_opened = false;
    m_accumFrames = 0;
}

int WaveOutBackend::write(const float *data, int frames)
{
    if (!m_opened) return 0;

    int written = 0;
    while (written < frames) {
        int space = BUFFER_FRAMES - m_accumFrames;
        int copy = (std::min)(space, frames - written);
        memcpy(m_accum + m_accumFrames * m_format.channels,
               data + written * m_format.channels,
               static_cast<size_t>(copy) * m_format.channels * sizeof(float));
        m_accumFrames += copy;
        written += copy;

        if (m_accumFrames >= BUFFER_FRAMES) {
            int idx = -1;
            for (int i = 0; i < NUM_BUFFERS; i++) {
                if (!m_buffers[i].active && !m_buffers[i].playing) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                m_overruns++;
                break;
            }

            short *dst = m_buffers[idx].data;
            const float *src = m_accum;
            for (int s = 0; s < BUFFER_FRAMES * m_format.channels; s++) {
                float v = src[s] * m_volume;
                if (v > 1.0f) v = 1.0f;
                if (v < -1.0f) v = -1.0f;
                dst[s] = static_cast<short>(v * 32767.0f);
            }

            m_buffers[idx].active = true;
            submitBuffer(idx);
            m_accumFrames = 0;
        }
    }
    return written;
}

void WaveOutBackend::submitBuffer(int idx)
{
    WAVEHDR *hdr = static_cast<WAVEHDR*>(m_headers[idx]);
    HWAVEOUT hwo = static_cast<HWAVEOUT>(m_hWaveOut);

    if (m_buffers[idx].prepared)
        waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));

    hdr->dwBufferLength = BUFFER_FRAMES * m_format.channels * 2;
    hdr->dwFlags = 0;

    MMRESULT res = waveOutPrepareHeader(hwo, hdr, sizeof(WAVEHDR));
    if (res != MMSYSERR_NOERROR) {
        m_buffers[idx].prepared = false;
        m_buffers[idx].active = false;
        return;
    }
    m_buffers[idx].prepared = true;

    res = waveOutWrite(hwo, hdr, sizeof(WAVEHDR));
    if (res != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
        m_buffers[idx].prepared = false;
        m_buffers[idx].active = false;
        return;
    }
    m_buffers[idx].playing = true;
    m_totalFramesWritten += BUFFER_FRAMES;
}

double WaveOutBackend::currentPositionSec() const
{
    if (!m_hWaveOut) return 0.0;
    HWAVEOUT hwo = static_cast<HWAVEOUT>(m_hWaveOut);
    MMTIME mmt;
    mmt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(hwo, &mmt, sizeof(MMTIME)) == MMSYSERR_NOERROR) {
        if (mmt.wType == TIME_SAMPLES)
            return static_cast<double>(mmt.u.sample) / m_format.sampleRate;
    }
    return static_cast<double>(m_totalFramesWritten) / m_format.sampleRate;
}

double WaveOutBackend::getLatencySec() const
{
    // waveOut latency estimate: NUM_BUFFERS * BUFFER_FRAMES / sampleRate
    return static_cast<double>(NUM_BUFFERS * BUFFER_FRAMES) / m_format.sampleRate;
}

void WaveOutBackend::setVolume(float vol)
{
    m_volume = (std::max)(0.0f, (std::min)(1.0f, vol));
    if (m_hWaveOut) {
        WORD left = static_cast<WORD>(vol * 0xFFFF);
        WORD right = static_cast<WORD>(vol * 0xFFFF);
        waveOutSetVolume(static_cast<HWAVEOUT>(m_hWaveOut),
                         left | (right << 16));
    }
}

void WaveOutBackend::reset()
{
    if (m_hWaveOut) {
        HWAVEOUT hwo = static_cast<HWAVEOUT>(m_hWaveOut);
        waveOutReset(hwo);
        for (int i = 0; i < NUM_BUFFERS; i++) {
            WAVEHDR *hdr = static_cast<WAVEHDR*>(m_headers[i]);
            if (hdr->dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));
            m_buffers[i].active = false;
            m_buffers[i].playing = false;
            m_buffers[i].prepared = false;
        }
    }
    m_accumFrames = 0;
    m_totalFramesWritten = 0;
}

AudioBackendStats WaveOutBackend::stats() const
{
    AudioBackendStats s;
    s.totalFramesWritten = m_totalFramesWritten;
    s.bufferUnderruns = m_underruns;
    s.bufferOverruns = m_overruns;
    s.averageLatencyMs = getLatencySec() * 1000.0;
    s.totalBufferCount = NUM_BUFFERS;

    int activeCount = 0;
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (m_buffers[i].active || m_buffers[i].playing)
            activeCount++;
    }
    s.activeBufferCount = activeCount;

    return s;
}

void WaveOutBackend::onBufferDone(int idx)
{
    if (idx < 0 || idx >= NUM_BUFFERS) return;
    m_buffers[idx].active = false;
    m_buffers[idx].playing = false;
}
