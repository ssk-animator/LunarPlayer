#include "AudioBackendFactory.h"
#include "WaveOutBackend.h"

#ifdef _WIN32
#include "WasapiSharedBackend.h"
#include "WasapiExclusiveBackend.h"
#endif

std::unique_ptr<IAudioBackend> AudioBackendFactory::create(AudioBackendType type) {
    switch (type) {
#ifdef _WIN32
    case AudioBackendType::WasapiShared:
        return std::make_unique<WasapiSharedBackend>();
    case AudioBackendType::WasapiExclusive:
        return std::make_unique<WasapiExclusiveBackend>();
#endif
    case AudioBackendType::WaveOut:
        return std::make_unique<WaveOutBackend>();
    default:
        return std::make_unique<WaveOutBackend>();
    }
}

AudioBackendType AudioBackendFactory::defaultBackend() {
#ifdef _WIN32
    return AudioBackendType::WasapiShared;
#else
    return AudioBackendType::WaveOut;
#endif
}

AudioBackendType AudioBackendFactory::fallbackBackend() {
    return AudioBackendType::WaveOut;
}
