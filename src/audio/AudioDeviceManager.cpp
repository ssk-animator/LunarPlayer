#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "AudioDeviceManager.h"
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

AudioDeviceManager::AudioDeviceManager() {}
AudioDeviceManager::~AudioDeviceManager() {}

AudioDeviceProperties AudioDeviceManager::defaultDevice(bool isInput) {
    auto devices = enumerateDevices(isInput);
    for (auto &d : devices) {
        if (d.isDefault) return d;
    }
    if (!devices.empty()) return devices[0];
    return {};
}

AudioDeviceProperties AudioDeviceManager::deviceProperties(const std::wstring &deviceId) {
    auto devices = enumerateDevices(false);
    for (auto &d : devices) {
        if (d.id == deviceId) return d;
    }
    return {};
}

std::vector<AudioDeviceProperties> AudioDeviceManager::enumerateDevices(bool isInput) {
    std::vector<AudioDeviceProperties> devices;
    IMMDeviceEnumerator *enumerator = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator));
    if (FAILED(hr) || !enumerator) return devices;

    EDataFlow dataFlow = isInput ? eCapture : eRender;
    IMMDeviceCollection *collection = nullptr;
    hr = enumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr) || !collection) { enumerator->Release(); return devices; }

    UINT count = 0;
    collection->GetCount(&count);

    for (UINT i = 0; i < count; i++) {
        IMMDevice *device = nullptr;
        collection->Item(i, &device);
        if (!device) continue;

        AudioDeviceProperties props;
        props.isInput = isInput;

        IPropertyStore *props2 = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props2))) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            if (SUCCEEDED(props2->GetValue(PKEY_Device_FriendlyName, &varName))) {
                props.friendlyName = varName.pwszVal;
                PropVariantClear(&varName);
            }

            PROPVARIANT varId;
            PropVariantInit(&varId);
            if (SUCCEEDED(props2->GetValue(PKEY_Device_Address, &varId))) {
                props.devicePath = varId.pwszVal;
                PropVariantClear(&varId);
            }

            PROPVARIANT varMfr;
            PropVariantInit(&varMfr);
            if (SUCCEEDED(props2->GetValue(PKEY_Device_Manufacturer, &varMfr))) {
                props.manufacturer = varMfr.pwszVal;
                PropVariantClear(&varMfr);
            }

            props2->Release();
        }

        LPWSTR deviceId = nullptr;
        if (SUCCEEDED(device->GetId(&deviceId))) {
            props.id = deviceId;
            CoTaskMemFree(deviceId);
        }

        std::wstring defaultId;
        IMMDevice *defaultDev = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(dataFlow, eConsole, &defaultDev))) {
            LPWSTR defId = nullptr;
            if (SUCCEEDED(defaultDev->GetId(&defId))) {
                defaultId = defId;
                CoTaskMemFree(defId);
            }
            defaultDev->Release();
        }
        props.isDefault = (props.id == defaultId);

        probeDevice(device, props);
        testPassthrough(device, props);

        devices.push_back(props);
        device->Release();
    }

    collection->Release();
    enumerator->Release();
    return devices;
}

bool AudioDeviceManager::probeDevice(IMMDevice *device, AudioDeviceProperties &props) {
    IAudioClient *client = nullptr;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void **>(&client));
    if (FAILED(hr) || !client) return false;

    WAVEFORMATEX *mixFormat = nullptr;
    hr = client->GetMixFormat(&mixFormat);
    if (SUCCEEDED(hr) && mixFormat) {
        props.defaultSampleRate = mixFormat->nSamplesPerSec;
        props.defaultChannels = mixFormat->nChannels;

        if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mixFormat->cbSize >= 22) {
            const WAVEFORMATEXTENSIBLE *wfxt = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(mixFormat);
            if (wfxt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
                props.defaultFormatName = L"PCM ";
                props.defaultFormatName += std::to_wstring(mixFormat->wBitsPerSample) + L"-bit";
            } else if (wfxt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
                props.defaultFormatName = L"Float " + std::to_wstring(mixFormat->wBitsPerSample) + L"-bit";
            } else {
                props.defaultFormatName = L"Unknown";
            }
        } else {
            props.defaultFormatName = L"PCM " + std::to_wstring(mixFormat->wBitsPerSample) + L"-bit";
        }
        CoTaskMemFree(mixFormat);
    }

    UINT32 bufFrames = 0;
    client->GetBufferSize(&bufFrames);
    props.sharedLatencyMs = static_cast<double>(bufFrames) / 48000.0 * 1000.0;

    client->Release();

    props.supportsExclusive = false;
    props.exclusiveLatencyMs = 0.0;
    IAudioClient *exClient = nullptr;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
        nullptr, reinterpret_cast<void **>(&exClient));
    if (SUCCEEDED(hr) && exClient) {
        WAVEFORMATEX *exMix = nullptr;
        hr = exClient->GetMixFormat(&exMix);
        if (SUCCEEDED(hr) && exMix) {
            WAVEFORMATEX testFmt = {};
            testFmt.wFormatTag = WAVE_FORMAT_PCM;
            testFmt.nChannels = 2;
            testFmt.nSamplesPerSec = 48000;
            testFmt.wBitsPerSample = 16;
            testFmt.nBlockAlign = 4;
            testFmt.nAvgBytesPerSec = 192000;
            testFmt.cbSize = 0;
            CoTaskMemFree(exMix);

            WAVEFORMATEX *closest = nullptr;
            hr = exClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &testFmt, &closest);
            props.supportsExclusive = (hr == S_OK);
            if (closest) CoTaskMemFree(closest);
        }
        exClient->Release();
    }

    int rates[] = { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
    props.minSampleRate = 0;
    props.maxSampleRate = 0;
    for (int rate : rates) {
        IAudioClient *testClient = nullptr;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
            nullptr, reinterpret_cast<void **>(&testClient));
        if (SUCCEEDED(hr) && testClient) {
            WAVEFORMATEX fmt = {};
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.nChannels = 2;
            fmt.nSamplesPerSec = rate;
            fmt.wBitsPerSample = 16;
            fmt.nBlockAlign = 4;
            fmt.nAvgBytesPerSec = rate * 4;
            fmt.cbSize = 0;

            WAVEFORMATEX *closest = nullptr;
            hr = testClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &fmt, &closest);
            if (hr == S_OK) {
                props.supportedSampleRates.push_back(rate);
                if (props.minSampleRate == 0 || rate < props.minSampleRate)
                    props.minSampleRate = rate;
                if (rate > props.maxSampleRate)
                    props.maxSampleRate = rate;
            }
            if (closest) CoTaskMemFree(closest);
            testClient->Release();
        }
    }

    props.maxChannels = 0;
    for (int ch = 1; ch <= 32; ch++) {
        IAudioClient *testClient = nullptr;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
            nullptr, reinterpret_cast<void **>(&testClient));
        if (SUCCEEDED(hr) && testClient) {
            WAVEFORMATEX fmt = {};
            fmt.wFormatTag = WAVE_FORMAT_PCM;
            fmt.nChannels = static_cast<WORD>(ch);
            fmt.nSamplesPerSec = 48000;
            fmt.wBitsPerSample = 16;
            fmt.nBlockAlign = ch * 2;
            fmt.nAvgBytesPerSec = 48000 * ch * 2;
            fmt.cbSize = 0;

            WAVEFORMATEX *closest = nullptr;
            hr = testClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &fmt, &closest);
            if (hr == S_OK) {
                props.maxChannels = ch;
            }
            if (closest) CoTaskMemFree(closest);
            testClient->Release();
        }
    }

    return true;
}

void AudioDeviceManager::testPassthrough(IMMDevice *device, AudioDeviceProperties &props) {
    auto testCodec = [&](BitstreamCodec codec) -> bool {
        IAudioClient *client = nullptr;
        HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
            nullptr, reinterpret_cast<void **>(&client));
        if (FAILED(hr) || !client) return false;

        GUID subType;
        switch (codec) {
        case BitstreamCodec::AC3:  subType = KSDATAFORMAT_SUBTYPE_AC3_AUDIO; break;
        case BitstreamCodec::EAC3: subType = KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS; break;
        case BitstreamCodec::DTS:  subType = KSDATAFORMAT_SUBTYPE_DTS_AUDIO; break;
        default: client->Release(); return false;
        }

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
        hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,
            reinterpret_cast<WAVEFORMATEX *>(&wfxt), &closest);
        if (closest) CoTaskMemFree(closest);
        client->Release();
        return hr == S_OK;
    };

    props.supportsPassthroughAC3 = testCodec(BitstreamCodec::AC3);
    props.supportsPassthroughEAC3 = testCodec(BitstreamCodec::EAC3);
    props.supportsPassthroughDTS = testCodec(BitstreamCodec::DTS);
}

std::wstring AudioDeviceManager::deviceStateName(DWORD state) {
    switch (state) {
    case DEVICE_STATE_ACTIVE:    return L"Active";
    case DEVICE_STATE_DISABLED:  return L"Disabled";
    case DEVICE_STATE_NOTPRESENT: return L"Not Present";
    case DEVICE_STATE_UNPLUGGED: return L"Unplugged";
    default: return L"Unknown";
    }
}
