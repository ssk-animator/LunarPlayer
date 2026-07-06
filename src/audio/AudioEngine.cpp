#include "AudioEngine.h"
#include "AudioChannelRouter.h"
#include "WasapiSharedBackend.h"
#include "WasapiExclusiveBackend.h"
#include <QElapsedTimer>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <avrt.h>
#pragma comment(lib, "avrt.lib")
#endif

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine() { close(); }

bool AudioEngine::openWithFallback(AVFormatContext *fmtCtx, int streamIdx, AudioBackendType preferred) {
    m_fmtCtx = fmtCtx;

    if (!m_decoder.open(fmtCtx, streamIdx))
        return false;

    AVCodecContext *cc = m_decoder.codecContext();

    m_inputFormat.sampleRate = cc->sample_rate;
    m_inputFormat.channels = cc->ch_layout.nb_channels;
    m_inputFormat.channelLayout = cc->ch_layout.u.mask;

    switch (cc->sample_fmt) {
    case AV_SAMPLE_FMT_S16:  m_inputFormat.sampleFormat = AudioSampleFormat::Int16; break;
    case AV_SAMPLE_FMT_S16P: m_inputFormat.sampleFormat = AudioSampleFormat::Int16Planar; break;
    case AV_SAMPLE_FMT_S32:  m_inputFormat.sampleFormat = AudioSampleFormat::Int32; break;
    case AV_SAMPLE_FMT_S32P: m_inputFormat.sampleFormat = AudioSampleFormat::Int32Planar; break;
    case AV_SAMPLE_FMT_FLT:  m_inputFormat.sampleFormat = AudioSampleFormat::Float32; break;
    case AV_SAMPLE_FMT_FLTP: m_inputFormat.sampleFormat = AudioSampleFormat::Float32Planar; break;
    case AV_SAMPLE_FMT_DBL:  m_inputFormat.sampleFormat = AudioSampleFormat::Float64; break;
    case AV_SAMPLE_FMT_DBLP: m_inputFormat.sampleFormat = AudioSampleFormat::Float64; break;
    default: m_inputFormat.sampleFormat = AudioSampleFormat::Float32; break;
    }

    m_outputFormat.sampleRate = m_inputFormat.sampleRate;
    m_outputFormat.sampleFormat = AudioSampleFormat::Float32;

    char inLayoutBuf[128] = {};
    av_channel_layout_describe(&cc->ch_layout, inLayoutBuf, sizeof(inLayoutBuf));
    fprintf(stderr, "[AUDIO-ENGINE] Decoder: %s %s %dHz %dch %s\n",
            avcodec_get_name(cc->codec_id),
            av_get_sample_fmt_name(cc->sample_fmt),
            cc->sample_rate, cc->ch_layout.nb_channels, inLayoutBuf);
    fflush(stderr);

    BitstreamCodec detectedCodec = BitstreamCodec::None;
    switch (cc->codec_id) {
    case AV_CODEC_ID_AC3:  detectedCodec = BitstreamCodec::AC3; break;
    case AV_CODEC_ID_EAC3: detectedCodec = BitstreamCodec::EAC3; break;
    case AV_CODEC_ID_DTS:  detectedCodec = BitstreamCodec::DTS; break;
    default: break;
    }

    AudioBackendType backendType = preferred;
    m_backend = AudioBackendFactory::create(backendType);
    if (!m_backend || !m_backend->open(m_outputFormat)) {
        fprintf(stderr, "[AUDIO-ENGINE] Backend %d FAILED, trying fallback\n", (int)backendType);
        fflush(stderr);
        backendType = AudioBackendFactory::fallbackBackend();
        m_backend = AudioBackendFactory::create(backendType);
        if (!m_backend || !m_backend->open(m_outputFormat)) {
            fprintf(stderr, "[AUDIO-ENGINE] Fallback backend FAILED\n");
            fflush(stderr);
            return false;
        }
    }

    if (detectedCodec != BitstreamCodec::None) {
        bool canPassthrough = false;
        {
            IMMDeviceEnumerator *enum2 = nullptr;
            IMMDevice *dev2 = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                    CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enum2))) &&
                SUCCEEDED(enum2->GetDefaultAudioEndpoint(eRender, eConsole, &dev2))) {
                IAudioClient *testClient = nullptr;
                if (SUCCEEDED(dev2->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                        nullptr, reinterpret_cast<void**>(&testClient)))) {
                    GUID subType;
                    switch (detectedCodec) {
                    case BitstreamCodec::AC3:  subType = KSDATAFORMAT_SUBTYPE_AC3_AUDIO; break;
                    case BitstreamCodec::EAC3: subType = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS; break;
                    case BitstreamCodec::DTS:  subType = KSDATAFORMAT_SUBTYPE_DTS_AUDIO; break;
                    default: subType = GUID_NULL; break;
                    }
                    if (subType != GUID_NULL) {
                        WAVEFORMATEXTENSIBLE wfxt = {};
                        wfxt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                        wfxt.Format.nChannels = 2;
                        wfxt.Format.nSamplesPerSec = cc->sample_rate > 0 ? cc->sample_rate : 48000;
                        wfxt.Format.wBitsPerSample = 16;
                        wfxt.Format.nBlockAlign = 4;
                        wfxt.Format.nAvgBytesPerSec = cc->bit_rate > 0 ? cc->bit_rate / 8 : 192000;
                        wfxt.Format.cbSize = 22;
                        wfxt.SubFormat = subType;
                        wfxt.Samples.wValidBitsPerSample = 16;
                        wfxt.dwChannelMask = KSAUDIO_SPEAKER_STEREO;

                        WAVEFORMATEX *closest = nullptr;
                        HRESULT hr = testClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,
                            reinterpret_cast<WAVEFORMATEX *>(&wfxt), &closest);
                        canPassthrough = (hr == S_OK);
                        if (closest) CoTaskMemFree(closest);
                    }
                    testClient->Release();
                }
                dev2->Release();
            }
            if (enum2) enum2->Release();
        }

        if (canPassthrough) {
            auto passthroughBackend = std::make_unique<WasapiExclusiveBackend>();
            if (passthroughBackend->openPassthrough(detectedCodec, cc->sample_rate, cc->bit_rate)) {
                m_backend.reset();
                m_backend = std::move(passthroughBackend);
                m_passthroughCodec = detectedCodec;
                m_isOpen = true;
                fprintf(stderr, "[AUDIO-ENGINE] Bitstream passthrough ACTIVE: %s %dHz %dbps\n",
                        avcodec_get_name(cc->codec_id), cc->sample_rate, cc->bit_rate);
                fflush(stderr);
                return true;
            }
        }
        fprintf(stderr, "[AUDIO-ENGINE] No passthrough for %s, using PCM decode\n",
                avcodec_get_name(cc->codec_id));
        fflush(stderr);
    }

    int deviceMaxCh = m_backend->deviceMaxChannels();
    int deviceSampleRate = m_backend->deviceSampleRate();
    RoutingResult routing = AudioChannelRouter::negotiate(m_inputFormat, deviceMaxCh);
    m_outputFormat.channels = routing.outputFormat.channels;
    m_outputFormat.channelLayout = routing.outputFormat.channelLayout;
    m_outputFormat.sampleRate = deviceSampleRate;
    m_channelOperation = routing.operation;

    if (!m_resampler.open(m_inputFormat, m_outputFormat)) {
        fprintf(stderr, "[AUDIO-ENGINE] Resampler init FAILED\n");
        fflush(stderr);
        return false;
    }

    m_ringBuffer = std::make_unique<AudioRingBuffer>(
        m_outputFormat.sampleRate * 2, m_outputFormat.channels);

    m_convertBufferCapacity = 48000;
    m_convertBuffer = new float[m_convertBufferCapacity * m_outputFormat.channels];

    if (m_backend->type() == AudioBackendType::WasapiShared) {
        auto *wasapi = static_cast<WasapiSharedBackend *>(m_backend.get());
        wasapi->setRingBuffer(m_ringBuffer.get());
    }

    fprintf(stderr, "[AUDIO-ENGINE] Pipeline: %s %dHz %dch -> %s %dHz %dch [%s] [dedicated thread]\n",
            avcodec_get_name(cc->codec_id),
            m_inputFormat.sampleRate, m_inputFormat.channels,
            m_backend->name().c_str(),
            m_outputFormat.sampleRate, m_outputFormat.channels,
            AudioChannelRouter::operationName(routing.operation));
    fflush(stderr);

    m_dspChain.addModule(std::make_shared<AudioGain>());
    m_dspChain.addModule(std::make_shared<AudioPeakLimiter>());
    m_dspChain.addModule(std::make_shared<AudioSimpleEQ>());
    m_dspChain.addModule(std::make_shared<AudioCompressor>());

    m_isOpen = true;
    return true;
}

bool AudioEngine::open(AVFormatContext *fmtCtx, int audioStreamIdx, AudioBackendType backendType) {
    close();
    return openWithFallback(fmtCtx, audioStreamIdx, backendType);
}

void AudioEngine::close() {
    stop();

    m_decoder.close();
    m_resampler.close();
    m_ringBuffer.reset();
    m_backend.reset();

    if (m_convertBuffer) { delete[] m_convertBuffer; m_convertBuffer = nullptr; }
    m_convertBufferCapacity = 0;
    m_fmtCtx = nullptr;
    m_passthroughCodec = BitstreamCodec::None;
    m_isOpen = false;
}

void AudioEngine::start() {
    if (!m_isOpen) return;
    m_isPlaying = true;
    m_isPaused = false;

    if (m_threadRunning) {
        fprintf(stderr, "[AUDIO-ENGINE] start: already running, skip\n");
        fflush(stderr);
        return;
    }

    fprintf(stderr, "[AUDIO-ENGINE] start: pre-fill begin ring=%d\n",
            m_ringBuffer ? m_ringBuffer->availableRead() : 0);
    fflush(stderr);
    int prefillCount = 0;
    for (int i = 0; i < 20 && m_ringBuffer && m_ringBuffer->availableRead() < m_outputFormat.sampleRate / 4; i++) {
        decodeAndProcess();
        prefillCount++;
    }
    fprintf(stderr, "[AUDIO-ENGINE] start: pre-fill done iterations=%d ring=%d\n",
            prefillCount, m_ringBuffer ? m_ringBuffer->availableRead() : 0);
    fflush(stderr);

    if (m_backend) m_backend->startPlayback();

    m_threadRunning = true;
    m_audioThread = std::thread(&AudioEngine::audioFeedLoop, this);
}

void AudioEngine::stop() {
    m_isPlaying = false;
    m_isPaused = false;

    if (m_threadRunning) {
        m_threadRunning = false;
        if (m_audioThread.joinable())
            m_audioThread.join();
    }

    if (m_backend) m_backend->reset();
}

void AudioEngine::pause() { m_isPaused = true; }
void AudioEngine::resume() { m_isPaused = false; }

void AudioEngine::reset() {
    stop();
    if (m_backend) m_backend->reset();
    m_decoder.flush();
    if (m_ringBuffer) m_ringBuffer->reset();
    m_clock.reset();
    m_totalPacketsDecoded = 0;
    m_totalFramesDecoded = 0;
    m_totalDecodeErrors = 0;
    m_totalFramesResampled = 0;
    m_totalFramesWritten = 0;
}

void AudioEngine::flush() {
    fprintf(stderr, "[AUDIO-ENGINE] flush: decoder_packets=%llu ring=%d\n",
            (unsigned long long)m_totalPacketsDecoded,
            m_ringBuffer ? m_ringBuffer->availableRead() : 0);
    fflush(stderr);
    m_decoder.flush();
    if (m_ringBuffer) m_ringBuffer->reset();
}

double AudioEngine::clockPositionSec() const {
    if (!m_backend) return 0.0;
    return m_clock.position(m_backend->currentPositionSec());
}

void AudioEngine::syncToPts(double ptsSec) {
    fprintf(stderr, "[AUDIO-ENGINE] syncToPts: %.3f\n", ptsSec);
    fflush(stderr);
    m_clock.setBase(ptsSec, m_backend ? m_backend->currentPositionSec() : 0.0);
}

void AudioEngine::setVolume(float vol) { if (m_backend) m_backend->setVolume(vol); }
float AudioEngine::volume() const { return m_backend ? m_backend->volume() : 0.0f; }

AudioPipelineStats AudioEngine::stats() const {
    AudioPipelineStats s;
    s.packetsDecoded = m_totalPacketsDecoded;
    s.framesDecoded = m_totalFramesDecoded;
    s.decodeErrors = m_totalDecodeErrors;
    s.framesResampled = m_totalFramesResampled;
    s.framesWritten = m_totalFramesWritten;

    if (m_ringBuffer) {
        s.underruns = m_ringBuffer->underruns();
        s.overruns = m_ringBuffer->overruns();
    }

    s.clockPositionSec = clockPositionSec();
    s.devicePositionSec = m_backend ? m_backend->currentPositionSec() : 0.0;
    s.latencyMs = m_backend ? m_backend->getLatencySec() * 1000.0 : 0.0;
    s.backendName = m_backend ? m_backend->name() : "none";
    s.outputChannels = m_outputFormat.channels;
    s.sampleRate = m_outputFormat.sampleRate;

    AVCodecContext *cc = m_decoder.codecContext();
    if (cc) {
        s.codecName = avcodec_get_name(cc->codec_id);
        s.sampleFormat = av_get_sample_fmt_name(cc->sample_fmt);
        char buf[128] = {};
        av_channel_layout_describe(&cc->ch_layout, buf, sizeof(buf));
        s.channelLayout = buf;
    }

    return s;
}

void AudioEngine::audioFeedLoop() {
#ifdef _WIN32
    DWORD mmcssTaskIndex = 0;
    HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Audio", &mmcssTaskIndex);
    if (mmcssHandle) {
        AvSetMmThreadPriority(mmcssHandle, AVRT_PRIORITY_HIGH);
        fprintf(stderr, "[AUDIO-THREAD] MMCSS registered: priority=HIGH\n");
    } else {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        fprintf(stderr, "[AUDIO-THREAD] MMCSS failed, using THREAD_PRIORITY_HIGHEST\n");
    }
    fflush(stderr);
#endif

    auto lastWake = std::chrono::steady_clock::now();
    int logCounter = 0;

    while (m_threadRunning) {
        auto now = std::chrono::steady_clock::now();
        double wakeJitterMs = std::chrono::duration<double, std::milli>(now - lastWake).count();
        m_lastWakeJitterMs.store(wakeJitterMs);
        lastWake = now;

        if (!m_isPlaying || m_isPaused || !m_backend) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Decode + resample
        auto t0 = std::chrono::steady_clock::now();
        decodeAndProcess();
        auto t1 = std::chrono::steady_clock::now();
        m_lastDecodeMs.store(std::chrono::duration<double, std::milli>(t1 - t0).count());

        // Write to WaveOut
        bool hasRenderThread = (m_backend->type() == AudioBackendType::WasapiShared);

        if (!hasRenderThread) {
            int avail = m_ringBuffer ? m_ringBuffer->availableRead() : 0;
            int cap = m_ringBuffer ? m_ringBuffer->capacity() : 1;
            m_lastRingFillPct.store(100.0 * avail / cap);

            if (avail > 0 && m_convertBufferCapacity >= avail) {
                auto tw0 = std::chrono::steady_clock::now();
                int got = m_ringBuffer->read(m_convertBuffer, avail);
                if (got > 0) {
                    m_backend->write(m_convertBuffer, got);
                    m_totalFramesWritten += got;
                }
                auto tw1 = std::chrono::steady_clock::now();
                m_lastWriteMs.store(std::chrono::duration<double, std::milli>(tw1 - tw0).count());
            }
        }

        double devicePos = m_backend->currentPositionSec();
        emit clockUpdated(m_clock.position(devicePos));

        // Log every ~500ms
        logCounter++;
        if (logCounter >= 167) {
            logCounter = 0;
            int underruns = m_ringBuffer ? m_ringBuffer->underruns() : 0;
            int overruns = m_ringBuffer ? m_ringBuffer->overruns() : 0;
            int64_t framesWritten = m_totalFramesWritten;
            if (m_backend && m_backend->type() == AudioBackendType::WasapiShared) {
                framesWritten = m_backend->stats().totalFramesWritten;
            }
            fprintf(stderr, "[AUDIO-PERF] wake=%.1fms decode=%.2fms write=%.2fms ringFill=%.0f%% "
                    "underruns=%d overruns=%d packets=%" PRId64 " frames=%" PRId64 "\n",
                    m_lastWakeJitterMs.load(), m_lastDecodeMs.load(), m_lastWriteMs.load(),
                    m_lastRingFillPct.load(), underruns, overruns,
                    m_totalPacketsDecoded, framesWritten);
            fflush(stderr);
        }

        // Target ~3ms between iterations
        std::this_thread::sleep_for(std::chrono::microseconds(FEED_INTERVAL_US));
    }

    // Thread priority restored automatically when thread exits
}

void AudioEngine::decodeAndProcess() {
    if (m_passthroughCodec != BitstreamCodec::None) {
        if (m_packetSource) {
            AVPacket *pkt = nullptr;
            while (m_packetSource(&pkt)) {
                if (m_passthroughCodec != BitstreamCodec::None && m_backend) {
                    int written = m_backend->writePassthrough(pkt->data, pkt->size);
                    if (written > 0) {
                        m_totalFramesWritten += pkt->size;
                    }
                }
                av_packet_free(&pkt);
                m_totalPacketsDecoded++;
            }
        }
        return;
    }

    if (m_packetSource) {
        AVPacket *pkt = nullptr;
        int pulled = 0;
        while (pulled < 32 && m_packetSource(&pkt)) {
            m_decoder.sendPacket(pkt);
            av_packet_free(&pkt);

            DecodedAudioFrame df;
            while (m_decoder.receiveFrame(df)) {
                processDecodedFrame(df);
            }
            pulled++;
            m_totalPacketsDecoded++;
        }
        if (pulled > 0) {
            fprintf(stderr, "[AUDIO-ENGINE] decodeAndProcess: pulled=%d totalPackets=%llu ring=%d\n",
                    pulled, (unsigned long long)m_totalPacketsDecoded,
                    m_ringBuffer ? m_ringBuffer->availableRead() : 0);
            fflush(stderr);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_packetMutex);
        while (!m_packetQueue.empty()) {
            AVPacket *pkt = m_packetQueue.front();
            m_packetQueue.pop();
            m_decoder.sendPacket(pkt);
            av_packet_free(&pkt);

            DecodedAudioFrame df;
            while (m_decoder.receiveFrame(df)) {
                processDecodedFrame(df);
            }
            m_totalPacketsDecoded++;
        }
    }
}

void AudioEngine::processDecodedFrame(DecodedAudioFrame &df) {
    if (!df.valid || !df.frame) return;

    int outCapacity = m_convertBufferCapacity;
    int converted = m_resampler.convert(df.frame, &m_convertBuffer, outCapacity);

    if (converted > 0) {
        if (m_dspChain.modules().size() > 0) {
            m_dspChain.process(m_convertBuffer, converted, m_outputFormat.channels);
        }
        if (m_ringBuffer) {
            m_ringBuffer->write(m_convertBuffer, converted);
            m_totalFramesDecoded += converted;
            m_totalFramesResampled += converted;
        }
    }
}
