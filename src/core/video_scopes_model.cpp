// SPDX-License-Identifier: LGPL-3.0-or-later
#include "video_scopes_model.h"

#include <QColorSpace>
#include <QVector>
#include <QtMath>

namespace {
constexpr int kMaxPixels = 16 * 1024 * 1024;
constexpr int kHistogramBins = 128;
constexpr int kWaveColumns = 128;
constexpr int kWaveRows = 64;
constexpr int kVectorSide = 64;
}

VideoScopesModel::VideoScopesModel(QObject *parent) : QObject(parent) {}

QVariantMap VideoScopesModel::failed(const QString &message)
{
    m_error = message;
    m_snapshot = {{QStringLiteral("valid"), false}, {QStringLiteral("error"), message}};
    emit changed();
    return m_snapshot;
}

void VideoScopesModel::clear()
{
    m_error.clear();
    m_snapshot.clear();
    emit changed();
}

QVariantMap VideoScopesModel::analyzeColorBarsFixture(const QString &frameId)
{
    // Eight equal-width SMPTE-like encoded sRGB bars exercise primary and
    // secondary chroma without implying a broadcast legal-range signal.
    const QRgb bars[] = {qRgb(255, 255, 255), qRgb(255, 255, 0), qRgb(0, 255, 255),
                         qRgb(0, 255, 0), qRgb(255, 0, 255), qRgb(255, 0, 0),
                         qRgb(0, 0, 255), qRgb(0, 0, 0)};
    QImage image(256, 64, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = bars[qMin(7, x * 8 / image.width())];
    }
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return analyzeFrame(image, frameId);
}

QVariantMap VideoScopesModel::analyzeFrame(const QImage &frame, const QString &frameId)
{
    if (frame.isNull())
        return failed(QStringLiteral("No decoded frame was supplied."));
    if (frameId.isEmpty())
        return failed(QStringLiteral("A stable frame ID is required."));
    if (qint64(frame.width()) * frame.height() > kMaxPixels)
        return failed(QStringLiteral("Frame exceeds the 16-megapixel analysis bound."));

    const bool hadProfile = frame.colorSpace().isValid();
    QImage srgb = frame;
    if (hadProfile) {
        srgb = frame.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888);
        if (srgb.isNull())
            return failed(QStringLiteral("The frame color profile could not be converted to sRGB."));
    } else {
        srgb = frame.convertToFormat(QImage::Format_RGBA8888);
        srgb.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }

    QVector<int> histogram(kHistogramBins, 0);
    QVector<int> waveform(kWaveColumns * kWaveRows, 0);
    QVector<int> vectorscope(kVectorSide * kVectorSide, 0);
    int samples = 0;
    int transparent = 0;
    int maxColumnCount = 0;
    for (int y = 0; y < srgb.height(); ++y) {
        const uchar *line = srgb.constScanLine(y);
        for (int x = 0; x < srgb.width(); ++x) {
            const uchar *p = line + x * 4;
            if (p[3] == 0) { ++transparent; continue; }
            const double r = p[0] / 255.0;
            const double g = p[1] / 255.0;
            const double b = p[2] / 255.0;
            const double luma = qBound(0.0, 0.2126 * r + 0.7152 * g + 0.0722 * b, 1.0);
            const int h = qMin(kHistogramBins - 1, int(luma * kHistogramBins));
            const int column = qMin(kWaveColumns - 1, x * kWaveColumns / srgb.width());
            const int row = qMin(kWaveRows - 1, int((1.0 - luma) * kWaveRows));
            const double cb = (b - luma) / (2.0 * (1.0 - 0.0722)) + 0.5;
            const double cr = (r - luma) / (2.0 * (1.0 - 0.2126)) + 0.5;
            const int vx = qBound(0, qRound(cb * (kVectorSide - 1)), kVectorSide - 1);
            const int vy = qBound(0, qRound((1.0 - cr) * (kVectorSide - 1)), kVectorSide - 1);
            ++histogram[h];
            ++waveform[column * kWaveRows + row];
            ++vectorscope[vy * kVectorSide + vx];
            ++samples;
        }
    }
    for (int c = 0; c < kWaveColumns; ++c) {
        int count = 0;
        for (int row = 0; row < kWaveRows; ++row)
            count += waveform[c * kWaveRows + row];
        maxColumnCount = qMax(maxColumnCount, count);
    }

    QVariantList hist, wave, vectors;
    hist.reserve(histogram.size());
    wave.reserve(waveform.size());
    vectors.reserve(vectorscope.size());
    for (int value : histogram) hist.append(value);
    for (int value : waveform) wave.append(value);
    for (int value : vectorscope) vectors.append(value);
    m_error.clear();
    m_snapshot = {
        {QStringLiteral("valid"), true}, {QStringLiteral("frameId"), frameId},
        {QStringLiteral("width"), srgb.width()}, {QStringLiteral("height"), srgb.height()},
        {QStringLiteral("sampleCount"), samples}, {QStringLiteral("transparentPixelCount"), transparent},
        {QStringLiteral("profileConvertedToSrgb"), hadProfile},
        {QStringLiteral("colorSpaceAssumption"), hadProfile ? QStringLiteral("input profile converted to sRGB") : QStringLiteral("untagged input assumed encoded sRGB")},
        {QStringLiteral("rangeConvention"), QStringLiteral("full-range normalized encoded RGB [0,1]")},
        {QStringLiteral("lumaConvention"), QStringLiteral("Rec.709 Y' from encoded sRGB; no legal-range scaling")},
        {QStringLiteral("vectorscopeConvention"), QStringLiteral("Rec.709 full-range Cb/Cr; Cb horizontal, Cr vertical")},
        {QStringLiteral("histogramBins"), kHistogramBins}, {QStringLiteral("histogram"), hist},
        {QStringLiteral("waveformColumns"), kWaveColumns}, {QStringLiteral("waveformRows"), kWaveRows},
        {QStringLiteral("waveform"), wave}, {QStringLiteral("waveformPeakColumnSamples"), maxColumnCount},
        {QStringLiteral("vectorscopeSide"), kVectorSide}, {QStringLiteral("vectorscope"), vectors}
    };
    emit changed();
    return m_snapshot;
}
