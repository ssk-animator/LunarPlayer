#ifndef AUDIOBACKENDFACTORY_H
#define AUDIOBACKENDFACTORY_H

#include "IAudioBackend.h"
#include <memory>
#include <vector>
#include <string>

class AudioBackendFactory {
public:
    static std::vector<std::string> availableBackends() {
        std::vector<std::string> list;
        list.push_back("WASAPI Shared");
        list.push_back("WASAPI Exclusive");
        list.push_back("WaveOut (Legacy)");
        return list;
    }

    static std::unique_ptr<IAudioBackend> create(AudioBackendType type);
    static AudioBackendType defaultBackend();
    static AudioBackendType fallbackBackend();
};

#endif
