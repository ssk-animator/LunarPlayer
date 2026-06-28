#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include "AudioOutput.h"
#include <cstring>
#include <algorithm>
#include <cstdio>

#pragma comment(lib, "winmm.lib")

static void CALLBACK waveOutCallbackStatic(HWAVEOUT hwo, UINT uMsg,
                                            DWORD_PTR dwInstance, DWORD_PTR dwParam1,
                                            DWORD_PTR /*dwParam2*/)
{
    if (uMsg != WOM_DONE) return;
    AudioOutput *self = reinterpret_cast<AudioOutput*>(dwInstance);
    WAVEHDR *hdr = reinterpret_cast<WAVEHDR*>(dwParam1);
    int idx = static_cast<int>(hdr->dwUser);
    self->onBufferDone(idx);
}

AudioOutput::AudioOutput()
{
    for (int i = 0; i < NUM_BUFFERS; i++) {
        m_buffers[i].data = new short[BUFFER_FRAMES * 2];
        m_headers[i] = nullptr;
    }
}

AudioOutput::~AudioOutput()
{
    close();
    for (int i = 0; i < NUM_BUFFERS; i++)
        delete[] m_buffers[i].data;
}

bool AudioOutput::open(int sampleRate, int channels)
{
    close();
    m_sampleRate = sampleRate;
    m_channels = channels;

    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = static_cast<WORD>(channels);
    wf.nSamplesPerSec = sampleRate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = static_cast<WORD>(channels * 2);
    wf.nAvgBytesPerSec = sampleRate * channels * 2;

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

    for (int i = 0; i < NUM_BUFFERS; i++) {
        WAVEHDR *hdr = new WAVEHDR();
        memset(hdr, 0, sizeof(WAVEHDR));
        hdr->lpData = reinterpret_cast<LPSTR>(m_buffers[i].data);
        hdr->dwBufferLength = BUFFER_FRAMES * channels * 2;
        hdr->dwUser = static_cast<DWORD_PTR>(i);
        m_headers[i] = hdr;
    }

    return true;
}

void AudioOutput::close()
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
    m_opened = false;
    m_accumFrames = 0;
}

bool AudioOutput::isOpen() const
{
    return m_opened;
}

int AudioOutput::write(const float *data, int frames)
{
    if (!m_opened) return 0;

    int written = 0;
    while (written < frames) {
        int space = BUFFER_FRAMES - m_accumFrames;
        int copy = (std::min)(space, frames - written);
        memcpy(m_accum + m_accumFrames * m_channels,
               data + written * m_channels,
               static_cast<size_t>(copy) * m_channels * sizeof(float));
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
            if (idx < 0)
                break;

            short *dst = m_buffers[idx].data;
            const float *src = m_accum;
            for (int s = 0; s < BUFFER_FRAMES * m_channels; s++) {
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

void AudioOutput::submitBuffer(int idx)
{
    WAVEHDR *hdr = static_cast<WAVEHDR*>(m_headers[idx]);
    HWAVEOUT hwo = static_cast<HWAVEOUT>(m_hWaveOut);

    // If the header was previously prepared (callback didn't unprepare it), do it now
    if (m_buffers[idx].prepared)
        waveOutUnprepareHeader(hwo, hdr, sizeof(WAVEHDR));

    hdr->dwBufferLength = BUFFER_FRAMES * m_channels * 2;
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

double AudioOutput::currentPositionSec() const
{
    if (!m_hWaveOut) return 0.0;
    HWAVEOUT hwo = static_cast<HWAVEOUT>(m_hWaveOut);
    MMTIME mmt;
    mmt.wType = TIME_SAMPLES;
    if (waveOutGetPosition(hwo, &mmt, sizeof(MMTIME)) == MMSYSERR_NOERROR) {
        if (mmt.wType == TIME_SAMPLES)
            return static_cast<double>(mmt.u.sample) / m_sampleRate;
    }
    return static_cast<double>(m_totalFramesWritten) / m_sampleRate;
}

void AudioOutput::setVolume(float vol)
{
    m_volume = (std::max)(0.0f, (std::min)(1.0f, vol));
    if (m_hWaveOut) {
        WORD left = static_cast<WORD>(vol * 0xFFFF);
        WORD right = static_cast<WORD>(vol * 0xFFFF);
        waveOutSetVolume(static_cast<HWAVEOUT>(m_hWaveOut),
                         left | (right << 16));
    }
}

float AudioOutput::volume() const
{
    return m_volume;
}

void AudioOutput::reset()
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

void AudioOutput::onBufferDone(int idx)
{
    if (idx < 0 || idx >= NUM_BUFFERS) return;
    // NOTE: Do NOT call waveOutUnprepareHeader here — doing so from the
    // waveOut service thread can deadlock with waveOutReset on the main
    // thread.  Unpreparing is handled by submitBuffer() before the next
    // prepare, and by reset()/close() after waveOutReset.
    m_buffers[idx].active = false;
    m_buffers[idx].playing = false;
}
