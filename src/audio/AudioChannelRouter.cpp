#include "AudioChannelRouter.h"
#include <cstdio>
#include <cstring>

RoutingResult AudioChannelRouter::negotiate(const AudioFormat &input, int deviceMaxChannels) {
    RoutingResult result;
    result.inputChannels = input.channels;
    result.outputFormat = input;
    result.operation = ChannelOperation::Native;

    int sourceChannels = input.channels;

    if (deviceMaxChannels <= 0) deviceMaxChannels = 2;

    if (sourceChannels <= deviceMaxChannels) {
        result.outputFormat.channels = sourceChannels;
        result.outputFormat.channelLayout = input.channelLayout;
        result.operation = ChannelOperation::Native;
        result.outputChannels = sourceChannels;
    } else {
        result.outputFormat.channels = deviceMaxChannels;
        result.outputFormat.channelLayout = channelMaskForCount(deviceMaxChannels);
        result.operation = ChannelOperation::Downmix;
        result.outputChannels = deviceMaxChannels;
    }

    result.outputFormat.sampleRate = input.sampleRate;
    result.outputFormat.sampleFormat = AudioSampleFormat::Float32;

    char inBuf[128] = {}, outBuf[128] = {};
    AVChannelLayout inLayout = {}, outLayout = {};
    inLayout.u.mask = input.channelLayout;
    inLayout.order = AV_CHANNEL_ORDER_NATIVE;
    inLayout.nb_channels = input.channels;
    outLayout.u.mask = result.outputFormat.channelLayout;
    outLayout.order = AV_CHANNEL_ORDER_NATIVE;
    outLayout.nb_channels = result.outputFormat.channels;
    av_channel_layout_describe(&inLayout, inBuf, sizeof(inBuf));
    av_channel_layout_describe(&outLayout, outBuf, sizeof(outBuf));

    char desc[256];
    snprintf(desc, sizeof(desc), "%s %dch(%s) -> %dch(%s)",
             operationName(result.operation),
             input.channels, inBuf,
             result.outputFormat.channels, outBuf);
    result.description = desc;

    fprintf(stderr, "[CHANNEL-ROUTER] %s\n", desc);
    fflush(stderr);

    return result;
}
