// SPDX-License-Identifier: LGPL-3.0-or-later
#include "video_scope_analysis.h"

#include <QColorSpace>
#include <QVector>
#include <QtMath>
#include <algorithm>
#include <array>

namespace {
QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("valid"), false}, {QStringLiteral("error"), message}};
}

QVariantList toList(const QVector<int> &values)
{
    QVariantList list;
    list.reserve(values.size());
    for (int value : values) list.append(value);
    return list;
}

QString transferName(const QColorSpace &space)
{
    switch (space.transferFunction()) {
    case QColorSpace::TransferFunction::Linear: return QStringLiteral("linear");
    case QColorSpace::TransferFunction::Gamma: return QStringLiteral("gamma %1").arg(space.gamma());
    case QColorSpace::TransferFunction::SRgb: return QStringLiteral("srgb");
    case QColorSpace::TransferFunction::ProPhotoRgb: return QStringLiteral("prophoto");
    case QColorSpace::TransferFunction::St2084: return QStringLiteral("pq");
    case QColorSpace::TransferFunction::Hlg: return QStringLiteral("hlg");
    default: return QStringLiteral("custom");
    }
}

QString matrixLabel(VideoScopeConvention::Matrix matrix)
{
    switch (matrix) {
    case VideoScopeConvention::Matrix::Bt601: return QStringLiteral("Rec.601");
    case VideoScopeConvention::Matrix::Bt2020: return QStringLiteral("Rec.2020");
    case VideoScopeConvention::Matrix::Bt709: break;
    }
    return QStringLiteral("Rec.709");
}

struct Extent {
    int minimum = 256;
    int maximum = -1;
    void add(int code) { minimum = std::min(minimum, code); maximum = std::max(maximum, code); }
    int low() const { return maximum < 0 ? -1 : minimum; }
};
} // namespace

std::optional<VideoScopeConvention::Matrix> VideoScopeConvention::matrixFrom(const QString &name)
{
    if (name == QLatin1String("bt709")) return Matrix::Bt709;
    if (name == QLatin1String("bt601")) return Matrix::Bt601;
    if (name == QLatin1String("bt2020")) return Matrix::Bt2020;
    return std::nullopt;
}

std::optional<VideoScopeConvention::Range> VideoScopeConvention::rangeFrom(const QString &name)
{
    if (name == QLatin1String("full")) return Range::Full;
    if (name == QLatin1String("limited")) return Range::Limited;
    return std::nullopt;
}

std::optional<VideoScopeConvention::Space> VideoScopeConvention::spaceFrom(const QString &name)
{
    if (name == QLatin1String("srgb")) return Space::Srgb;
    if (name == QLatin1String("signal")) return Space::Signal;
    return std::nullopt;
}

QString VideoScopeConvention::matrixName(Matrix matrix)
{
    return matrix == Matrix::Bt601 ? QStringLiteral("bt601")
        : matrix == Matrix::Bt2020 ? QStringLiteral("bt2020") : QStringLiteral("bt709");
}

QString VideoScopeConvention::rangeName(Range range)
{
    return range == Range::Limited ? QStringLiteral("limited") : QStringLiteral("full");
}

QString VideoScopeConvention::spaceName(Space space)
{
    return space == Space::Signal ? QStringLiteral("signal") : QStringLiteral("srgb");
}

double VideoScopeConvention::kr() const
{
    return matrix == Matrix::Bt601 ? 0.299 : matrix == Matrix::Bt2020 ? 0.2627 : 0.2126;
}

double VideoScopeConvention::kb() const
{
    return matrix == Matrix::Bt601 ? 0.114 : matrix == Matrix::Bt2020 ? 0.0593 : 0.0722;
}

int VideoScopeConvention::lumaCode(double value) const
{
    const double v = std::clamp(value, 0.0, 1.0);
    return range == Range::Limited ? qRound(16.0 + 219.0 * v) : qRound(255.0 * v);
}

int VideoScopeConvention::chromaCode(double value) const
{
    const double c = std::clamp(value, -0.5, 0.5);
    return range == Range::Limited ? qRound(128.0 + 224.0 * c) : std::clamp(qRound(128.0 + 255.0 * c), 0, 255);
}

QVariantList VideoScopeAnalysis::vectorTargets(const VideoScopeConvention &convention)
{
    const double kr = convention.kr();
    const double kb = convention.kb();
    const double kg = 1.0 - kr - kb;
    struct Color { const char *id; double r, g, b; };
    const std::array<Color, 6> colors{{{"R", 1, 0, 0}, {"Yl", 1, 1, 0}, {"G", 0, 1, 0},
                                       {"Cy", 0, 1, 1}, {"B", 0, 0, 1}, {"Mg", 1, 0, 1}}};
    QVariantList targets;
    for (const int level : {75, 100}) {
        const double scale = level / 100.0;
        for (const Color &color : colors) {
            const double r = color.r * scale, g = color.g * scale, b = color.b * scale;
            const double y = kr * r + kg * g + kb * b;
            const double cb = (b - y) / (2.0 * (1.0 - kb));
            const double cr = (r - y) / (2.0 * (1.0 - kr));
            targets.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("%1%2").arg(QLatin1String(color.id)).arg(level)},
                {QStringLiteral("label"), QLatin1String(color.id)}, {QStringLiteral("level"), level},
                {QStringLiteral("cb"), convention.chromaCode(cb)}, {QStringLiteral("cr"), convention.chromaCode(cr)},
                {QStringLiteral("x"), cb + 0.5}, {QStringLiteral("y"), 0.5 - cr}});
        }
    }
    return targets;
}

QVariantMap VideoScopeAnalysis::analyze(const QImage &frame, const QString &frameId,
                                        const VideoScopeConvention &convention)
{
    if (frame.isNull()) return failure(QStringLiteral("No decoded frame was supplied."));
    if (frameId.isEmpty()) return failure(QStringLiteral("A stable frame ID is required."));
    if (qint64(frame.width()) * frame.height() > kMaxPixels)
        return failure(QStringLiteral("Frame exceeds the 16-megapixel analysis bound."));

    const QColorSpace tag = frame.colorSpace();
    const bool tagged = tag.isValid();
    const bool convert = tagged && convention.space == VideoScopeConvention::Space::Srgb;
    QImage rgba = convert ? frame.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888)
                          : frame.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull())
        return failure(QStringLiteral("The frame color profile could not be converted to sRGB."));
    const QString transfer = convention.space == VideoScopeConvention::Space::Srgb ? QStringLiteral("srgb")
        : tagged ? transferName(tag) : QStringLiteral("untagged");

    const double kr = convention.kr();
    const double kb = convention.kb();
    const double kg = 1.0 - kr - kb;
    QVector<int> histogram(kHistogramBins, 0), histogramRgb(3 * kHistogramBins, 0);
    QVector<int> waveform(kWaveColumns * kWaveRows, 0), parade(3 * kWaveColumns * kWaveRows, 0);
    QVector<int> vectorscope(kVectorSide * kVectorSide, 0);
    QVector<Extent> waveExtent(kWaveColumns), paradeExtent(3 * kWaveColumns);
    Extent levels[6]; // Y', Cb, Cr, R', G', B'
    int samples = 0, transparent = 0;
    const auto row = [](int code) { return (255 - code) * kWaveRows / 256; };
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar *line = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            const uchar *p = line + x * 4;
            if (p[3] == 0) { ++transparent; continue; }
            const double rgb[3] = {p[0] / 255.0, p[1] / 255.0, p[2] / 255.0};
            const double luma = std::clamp(kr * rgb[0] + kg * rgb[1] + kb * rgb[2], 0.0, 1.0);
            const double cb = (rgb[2] - luma) / (2.0 * (1.0 - kb));
            const double cr = (rgb[0] - luma) / (2.0 * (1.0 - kr));
            const int column = std::min(kWaveColumns - 1, x * kWaveColumns / rgba.width());
            const int lumaCode = convention.lumaCode(luma);
            ++histogram[lumaCode * kHistogramBins / 256];
            ++waveform[column * kWaveRows + row(lumaCode)];
            waveExtent[column].add(lumaCode);
            levels[0].add(lumaCode);
            levels[1].add(convention.chromaCode(cb));
            levels[2].add(convention.chromaCode(cr));
            for (int channel = 0; channel < 3; ++channel) {
                const int code = convention.lumaCode(rgb[channel]);
                ++histogramRgb[channel * kHistogramBins + code * kHistogramBins / 256];
                ++parade[(channel * kWaveColumns + column) * kWaveRows + row(code)];
                paradeExtent[channel * kWaveColumns + column].add(code);
                levels[3 + channel].add(code);
            }
            const int vx = std::clamp(qRound((cb + 0.5) * (kVectorSide - 1)), 0, kVectorSide - 1);
            const int vy = std::clamp(qRound((0.5 - cr) * (kVectorSide - 1)), 0, kVectorSide - 1);
            ++vectorscope[vy * kVectorSide + vx];
            ++samples;
        }
    }
    int peakColumn = 0;
    for (int c = 0; c < kWaveColumns; ++c) {
        int count = 0;
        for (int r = 0; r < kWaveRows; ++r) count += waveform[c * kWaveRows + r];
        peakColumn = std::max(peakColumn, count);
    }
    QVector<int> waveMin, waveMax, paradeMin, paradeMax;
    for (const Extent &extent : std::as_const(waveExtent)) { waveMin << extent.low(); waveMax << extent.maximum; }
    for (const Extent &extent : std::as_const(paradeExtent)) { paradeMin << extent.low(); paradeMax << extent.maximum; }
    const char *levelNames[6] = {"y", "cb", "cr", "r", "g", "b"};
    QVariantMap levelMap;
    for (int i = 0; i < 6; ++i) {
        levelMap.insert(QStringLiteral("%1Min").arg(QLatin1String(levelNames[i])), levels[i].low());
        levelMap.insert(QStringLiteral("%1Max").arg(QLatin1String(levelNames[i])), levels[i].maximum);
    }

    const bool limited = convention.range == VideoScopeConvention::Range::Limited;
    const bool srgbSpace = convention.space == VideoScopeConvention::Space::Srgb;
    const QString assumption = srgbSpace
        ? (tagged ? QStringLiteral("input profile converted to sRGB") : QStringLiteral("untagged input assumed encoded sRGB"))
        : QStringLiteral("delivered encoded values measured unchanged (transfer %1)").arg(transfer);
    return {
        {QStringLiteral("valid"), true}, {QStringLiteral("frameId"), frameId},
        {QStringLiteral("width"), rgba.width()}, {QStringLiteral("height"), rgba.height()},
        {QStringLiteral("sampleCount"), samples}, {QStringLiteral("transparentPixelCount"), transparent},
        {QStringLiteral("profileConvertedToSrgb"), convert},
        {QStringLiteral("colorSpaceAssumption"), assumption},
        {QStringLiteral("matrix"), VideoScopeConvention::matrixName(convention.matrix)},
        {QStringLiteral("signalRange"), VideoScopeConvention::rangeName(convention.range)},
        {QStringLiteral("analysisSpace"), VideoScopeConvention::spaceName(convention.space)},
        {QStringLiteral("transfer"), transfer},
        {QStringLiteral("rangeConvention"), limited
            ? QStringLiteral("limited-range 8-bit codes: R'G'B' and Y' 16–235, Cb/Cr 16–240")
            : QStringLiteral("full-range normalized encoded RGB [0,1]")},
        {QStringLiteral("lumaConvention"), QStringLiteral("%1 Y' from encoded %2; %3")
            .arg(matrixLabel(convention.matrix), srgbSpace ? QStringLiteral("sRGB") : QStringLiteral("signal"),
                 limited ? QStringLiteral("legal-range codes 16–235") : QStringLiteral("no legal-range scaling"))},
        {QStringLiteral("vectorscopeConvention"), QStringLiteral("%1 %2 Cb/Cr; Cb horizontal, Cr vertical")
            .arg(matrixLabel(convention.matrix), limited ? QStringLiteral("limited-range") : QStringLiteral("full-range"))},
        {QStringLiteral("codeScale"), QVariantMap{{QStringLiteral("black"), limited ? 16 : 0},
            {QStringLiteral("white"), limited ? 235 : 255}, {QStringLiteral("chromaZero"), 128},
            {QStringLiteral("chromaMin"), limited ? 16 : 0}, {QStringLiteral("chromaMax"), limited ? 240 : 255}}},
        {QStringLiteral("levels"), levelMap},
        {QStringLiteral("histogramBins"), kHistogramBins}, {QStringLiteral("histogram"), toList(histogram)},
        {QStringLiteral("histogramRgb"), toList(histogramRgb)},
        {QStringLiteral("waveformColumns"), kWaveColumns}, {QStringLiteral("waveformRows"), kWaveRows},
        {QStringLiteral("waveform"), toList(waveform)}, {QStringLiteral("waveformPeakColumnSamples"), peakColumn},
        {QStringLiteral("waveformMin"), toList(waveMin)}, {QStringLiteral("waveformMax"), toList(waveMax)},
        {QStringLiteral("parade"), toList(parade)},
        {QStringLiteral("paradeMin"), toList(paradeMin)}, {QStringLiteral("paradeMax"), toList(paradeMax)},
        {QStringLiteral("vectorscopeSide"), kVectorSide}, {QStringLiteral("vectorscope"), toList(vectorscope)},
        {QStringLiteral("vectorTargets"), vectorTargets(convention)}};
}
