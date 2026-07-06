#ifndef AUDIOCHANNELROUTER_H
#define AUDIOCHANNELROUTER_H

#include "IAudioBackend.h"
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
}

enum class ChannelOperation {
    Native,       // Input layout matches output — no conversion
    Downmix,      // More channels → fewer channels
    Upmix,        // Fewer channels → more channels
    Remap,        // Same count, different speaker positions
    Passthrough   // Bitstream passthrough — no decoding
};

struct RoutingResult {
    AudioFormat outputFormat;
    ChannelOperation operation;
    std::string description;
    int inputChannels;
    int outputChannels;
};

class AudioChannelRouter {
public:
    static RoutingResult negotiate(const AudioFormat &input, int deviceMaxChannels);

    static const char *operationName(ChannelOperation op) {
        switch (op) {
        case ChannelOperation::Native:       return "Native";
        case ChannelOperation::Downmix:      return "Downmix";
        case ChannelOperation::Upmix:        return "Upmix";
        case ChannelOperation::Remap:        return "Remap";
        case ChannelOperation::Passthrough:  return "Passthrough";
        }
        return "Unknown";
    }

    static uint64_t channelMaskForCount(int channels) {
        switch (channels) {
        case 1:  return AV_CH_LAYOUT_MONO;
        case 2:  return AV_CH_LAYOUT_STEREO;
        case 3:  return AV_CH_LAYOUT_SURROUND;
        case 4:  return AV_CH_LAYOUT_QUAD;
        case 5:  return AV_CH_LAYOUT_5POINT0;
        case 6:  return AV_CH_LAYOUT_5POINT1;
        case 7:  return AV_CH_LAYOUT_6POINT1;
        case 8:  return AV_CH_LAYOUT_7POINT1;
        default: return AV_CH_LAYOUT_STEREO;
        }
    }

    static std::string layoutName(uint64_t mask) {
        char buf[128] = {};
        AVChannelLayout layout = {};
        layout.u.mask = mask;
        layout.order = AV_CHANNEL_ORDER_NATIVE;
        av_channel_layout_describe(&layout, buf, sizeof(buf));
        return std::string(buf);
    }
};

#endif
