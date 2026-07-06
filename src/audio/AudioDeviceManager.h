#ifndef AUDIODEVICEMANAGER_H
#define AUDIODEVICEMANAGER_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "IAudioBackend.h"
#include <vector>
#include <string>

struct AudioDeviceProperties {
    std::wstring id;
    std::wstring name;
    std::wstring friendlyName;
    std::wstring devicePath;
    bool isDefault = false;
    bool isInput = false;
    int maxChannels = 0;
    int minSampleRate = 0;
    int maxSampleRate = 0;
    std::vector<int> supportedSampleRates;
    int defaultSampleRate = 0;
    int defaultChannels = 0;
    std::wstring defaultFormatName;
    double exclusiveLatencyMs = 0.0;
    double sharedLatencyMs = 0.0;
    bool supportsExclusive = false;
    bool supportsPassthroughAC3 = false;
    bool supportsPassthroughEAC3 = false;
    bool supportsPassthroughDTS = false;
    std::wstring manufacturer;
    std::wstring driverName;
};

class AudioDeviceManager {
public:
    AudioDeviceManager();
    ~AudioDeviceManager();

    std::vector<AudioDeviceProperties> enumerateDevices(bool isInput = false);
    AudioDeviceProperties deviceProperties(const std::wstring &deviceId);
    AudioDeviceProperties defaultDevice(bool isInput = false);

    static std::wstring deviceStateName(DWORD state);

private:
    bool probeDevice(IMMDevice *device, AudioDeviceProperties &props);
    bool testFormat(IMMDevice *device, const WAVEFORMATEX &fmt, bool exclusive);
    void testPassthrough(IMMDevice *device, AudioDeviceProperties &props);
};

#endif
