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
    addRow(table, "Codec", info.codecName);
    addRow(table, "Codec (long)", info.codecLongName);
    addRow(table, "Profile", info.profile);
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
    addRow(table, "Color Space", info.colorSpace);
    addRow(table, "HDR Type", info.hdrType);
    if (info.bitrate > 0)
        addRow(table, "Bitrate", QLocale().toString(info.bitrate / 1000) + " kb/s");
    addRow(table, "Decoder", info.decoder);
    addRow(table, "Renderer", info.renderer);
    addRow(table, "Audio Codec", info.audioFormat);
    if (info.audioSampleRate > 0)
        addRow(table, "Audio Sample Rate", QLocale().toString(info.audioSampleRate) + " Hz");
    addRow(table, "Audio Channels", QString::number(info.audioChannels));
    addRow(table, "Video Streams", QString::number(info.videoStreams));
    addRow(table, "Audio Streams", QString::number(info.audioStreams));
    addRow(table, "Subtitle Streams", QString::number(info.subtitleStreams));
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
