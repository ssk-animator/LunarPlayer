#include "MediaInfoDialog.h"
#include <QVBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QFont>
#include <QLocale>

MediaInfoDialog::MediaInfoDialog(const MediaInfo &info, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Media Information");
    setMinimumSize(520, 420);
    resize(560, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Property", "Value"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);

    QFont headerFont = table->horizontalHeader()->font();
    headerFont.setBold(true);
    table->horizontalHeader()->setFont(headerFont);

    addRow(table, "File Name", info.fileName);
    addRow(table, "Container", info.containerFormat);
    if (!info.containerLongName.isEmpty() && info.containerLongName != info.containerFormat)
        addRow(table, "Container (long)", info.containerLongName);
    addRow(table, "Codec", info.codecName);
    addRow(table, "Codec (long)", info.codecLongName);
    addRow(table, "Profile", info.profile);
    if (!info.level.isEmpty())
        addRow(table, "Level", info.level);
    addRow(table, "Resolution", QString("%1 x %2").arg(info.width).arg(info.height));
    addRow(table, "Frame Rate", QString("%1 fps").arg(info.fps, 0, 'f', 3));
    {
        int totalMs = static_cast<int>(info.durationSec * 1000.0);
        int h = totalMs / 3600000, m = (totalMs % 3600000) / 60000, s = (totalMs % 60000) / 1000;
        QString dur;
        if (h > 0) dur = QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
        else dur = QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
        addRow(table, "Duration", dur);
    }
    addRow(table, "Pixel Format", info.pixelFormat);
    addRow(table, "Bit Depth", info.bitDepth > 0 ? QString::number(info.bitDepth) : "N/A");
    addRow(table, "Chroma Subsampling", info.chromaSubsampling.isEmpty() ? "N/A" : info.chromaSubsampling);
    addRow(table, "Color Space", info.colorSpace);
    addRow(table, "HDR Type", info.hdrType);
    if (info.bitrate > 0)
        addRow(table, "Bitrate", QLocale().toString(info.bitrate / 1000) + " kb/s");
    addRow(table, "Decoder", info.decoder);
    if (!info.decoderPath.isEmpty())
        addRow(table, "Decoder Path", info.decoderPath);
    addRow(table, "Renderer", info.renderer);
    addRow(table, "Audio Codec", info.audioFormat);
    if (!info.audioCodecLongName.isEmpty() && info.audioCodecLongName != info.audioFormat)
        addRow(table, "Audio Codec (long)", info.audioCodecLongName);
    if (!info.audioProfile.isEmpty())
        addRow(table, "Audio Profile", info.audioProfile);
    if (!info.audioSampleFormat.isEmpty())
        addRow(table, "Audio Sample Format", info.audioSampleFormat);
    if (info.audioBitDepth > 0)
        addRow(table, "Audio Bit Depth", QString("%1-bit").arg(info.audioBitDepth));
    if (info.audioSampleRate > 0)
        addRow(table, "Audio Sample Rate", QLocale().toString(info.audioSampleRate) + " Hz");
    addRow(table, "Audio Channels", QString::number(info.audioChannels));
    if (!info.audioChannelLayout.isEmpty())
        addRow(table, "Audio Channel Layout", info.audioChannelLayout);
    if (info.audioBitrate > 0)
        addRow(table, "Audio Bitrate", QLocale().toString(info.audioBitrate / 1000) + " kb/s");
    if (info.audioFrameSize > 0)
        addRow(table, "Audio Frame Size", QString::number(info.audioFrameSize) + " samples");
    if (info.audioBlockAlign > 0)
        addRow(table, "Audio Block Align", QString::number(info.audioBlockAlign) + " bytes");
    if (!info.audioBackend.isEmpty()) {
        addRow(table, "--- Audio Output ---", "");
        addRow(table, "Audio Backend", info.audioBackend);
        if (info.audioOutputSampleRate > 0)
            addRow(table, "Output Sample Rate", QLocale().toString(info.audioOutputSampleRate) + " Hz");
        if (info.audioOutputChannels > 0)
            addRow(table, "Output Channels", QString::number(info.audioOutputChannels));
        if (info.audioOutputLatencyMs > 0)
            addRow(table, "Output Latency", QString::number(info.audioOutputLatencyMs, 'f', 1) + " ms");
    }
    if (info.audioPacketsDecoded > 0) {
        addRow(table, "--- Audio Statistics ---", "");
        addRow(table, "Packets Decoded", QLocale().toString(info.audioPacketsDecoded));
        addRow(table, "Frames Written", QLocale().toString(info.audioFramesWritten));
        addRow(table, "Audio Underruns", QString::number(info.audioUnderruns));
        addRow(table, "Audio Overruns", QString::number(info.audioOverruns));
        if (info.audioDecodeMs > 0)
            addRow(table, "Avg Decode Time", QString::number(info.audioDecodeMs * 1000.0, 'f', 2) + " ms");
        if (info.audioResampleMs > 0)
            addRow(table, "Avg Resample Time", QString::number(info.audioResampleMs * 1000.0, 'f', 2) + " ms");
    }
    if (!info.deviceName.isEmpty()) {
        addRow(table, "--- Audio Device ---", "");
        addRow(table, "Device", info.deviceName);
        if (!info.deviceManufacturer.isEmpty())
            addRow(table, "Manufacturer", info.deviceManufacturer);
        if (info.deviceMaxChannels > 0)
            addRow(table, "Max Channels", QString::number(info.deviceMaxChannels));
        if (info.deviceMinSampleRate > 0 && info.deviceMaxSampleRate > 0)
            addRow(table, "Sample Rates", QString("%1 - %2 Hz").arg(info.deviceMinSampleRate).arg(info.deviceMaxSampleRate));
        addRow(table, "Exclusive Mode", info.deviceSupportsExclusive ? "Supported" : "Not supported");
        addRow(table, "Passthrough (AC3)", info.deviceSupportsPassthroughAC3 ? "Supported" : "Not supported");
        addRow(table, "Passthrough (EAC3)", info.deviceSupportsPassthroughEAC3 ? "Supported" : "Not supported");
        addRow(table, "Passthrough (DTS)", info.deviceSupportsPassthroughDTS ? "Supported" : "Not supported");
        if (!info.channelOperation.isEmpty())
            addRow(table, "Channel Routing", info.channelOperation);
    }
    QString streams;
    for (int i = 0; i < info.videoStreams; ++i) streams += streams.isEmpty() ? "Video" : ", Video";
    for (int i = 0; i < info.audioStreams; ++i) streams += streams.isEmpty() ? "Audio" : ", Audio";
    for (int i = 0; i < info.subtitleStreams; ++i) streams += streams.isEmpty() ? "Subtitle" : ", Subtitle";
    for (int i = 0; i < info.otherStreams; ++i) streams += streams.isEmpty() ? "Other" : ", Other";
    addRow(table, "Streams", QString("%1 total").arg(info.videoStreams + info.audioStreams + info.subtitleStreams + info.otherStreams));
    addRow(table, "Video Streams", QString::number(info.videoStreams));
    addRow(table, "Audio Streams", QString::number(info.audioStreams));
    addRow(table, "Subtitle Streams", QString::number(info.subtitleStreams));
    if (info.otherStreams > 0)
        addRow(table, "Other Streams", QString::number(info.otherStreams));
    if (!info.creationTime.isEmpty())
        addRow(table, "Creation Time", info.creationTime);
    if (info.isImageSequence) {
        addRow(table, "Image Sequence", "Yes");
        addRow(table, "Sequence Pattern", info.sequencePattern);
        addRow(table, "Sequence Frames", QString::number(info.sequenceFrameCount));
    }

    layout->addWidget(table);

    auto *closeBtn = new QPushButton("Close", this);
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    layout->addLayout(btnLayout);
}

void MediaInfoDialog::addRow(QTableWidget *table, const QString &label, const QString &value)
{
    int row = table->rowCount();
    table->insertRow(row);
    auto *labelItem = new QTableWidgetItem(label);
    labelItem->setFont(QFont(labelItem->font().family(), -1, QFont::Bold));
    table->setItem(row, 0, labelItem);
    table->setItem(row, 1, new QTableWidgetItem(value));
}
