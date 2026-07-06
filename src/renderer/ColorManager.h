#pragma once
#include <QObject>
#include <QString>
#include <QImage>
#include <cstdint>

struct AVFrame;

enum class HDRFormat {
    None,
    HDR10,
    HDR10Plus,
    HLG,
    DolbyVision,
    SDR
};

enum class ColorPrimaries {
    BT709 = 1,
    BT2020 = 9,
    DCI_P3 = 12,
    Unknown = 0
};

enum class TransferCharacteristics {
    BT709 = 1,
    BT2020_10 = 14,
    BT2020_12 = 15,
    PQ = 16,       // SMPTE ST 2084
    HLG = 18,       // ARIB STD-B67
    Unknown = 0
};

struct HDRMetadata {
    HDRFormat format = HDRFormat::None;
    ColorPrimaries primaries = ColorPrimaries::Unknown;
    TransferCharacteristics transfer = TransferCharacteristics::Unknown;

    // Mastering display metadata (from AV_FRAME_DATA_MASTERING_DISPLAY_METADATA)
    double displayPrimaryX[3]{};
    double displayPrimaryY[3]{};
    double whitePointX = 0.3127, whitePointY = 0.3290;
    double maxLuminance = 1000.0;
    double minLuminance = 0.001;

    // Content light level (from AV_FRAME_DATA_CONTENT_LIGHT_LEVEL)
    double maxCLL = 0.0;     // Max content light level (nits)
    double maxFALL = 0.0;    // Max frame-average light level (nits)

    bool valid = false;
};

class ColorManager : public QObject
{
    Q_OBJECT
public:
    explicit ColorManager(QObject *parent = nullptr);

    // Parse HDR metadata from AVFrame side data
    static HDRMetadata parseFrameMetadata(const AVFrame *frame);

    // Detect HDR format from codec parameters
    static HDRFormat detectFormat(const AVFrame *frame);

    // Color space conversion: returns appropriate sws flags
    static int swsColorFlags(const HDRMetadata &src, const HDRMetadata &dst);

    // Check if tone mapping is needed
    static bool needsToneMapping(const HDRMetadata &metadata);

    // Human-readable description
    static QString formatName(HDRFormat fmt);
    static QString description(const HDRMetadata &md);

    // Apply tone mapping to QImage (CPU path, HDR→SDR)
    QImage applyToneMap(const QImage &frame, const HDRMetadata &metadata);

    // Configure display
    void setDisplayMaxLuminance(double nits) { m_displayMaxLuminance = nits; }
    double displayMaxLuminance() const { return m_displayMaxLuminance; }

    // Tone mapping parameters
    void setToneMapExposure(double ev) { m_exposure = ev; }
    double toneMapExposure() const { return m_exposure; }

signals:
    void hdrFormatChanged(HDRFormat format);
    void toneMappingChanged();

private:
    double m_displayMaxLuminance = 300.0; // Typical SDR display
    double m_exposure = 0.0;
};
