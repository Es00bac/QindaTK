// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>
#include <functional>

namespace QindaTK::TrackingEngine {

// Tracking proxies: encoded sRGB Grayscale8, at most this many pixels.
inline constexpr qint64 kMaxProxyPixels = 4 * 1024 * 1024;
// Aggregate proxy storage per TrackingRotoModel.
inline constexpr qsizetype kMaxProxyBytes = 256 * 1024 * 1024;
// Mean absolute patch error above which a frame reports `lost`.
inline constexpr double kLostError = 16.0;
// Search radius cap while a feature is occluded (radius grows per lost frame).
inline constexpr int kMaxSearchRadius = 64;
// Minimum patch standard deviation accepted as a trackable reference.
inline constexpr double kMinTexture = 6.0;

struct Sample {
    int frame = 0;
    double x = 0; // feature position, source pixels
    double y = 0;
    double confidence = 0;
    bool lost = false;
};

enum class End { Completed, Lost, Cancelled };

// AGENT-CONTRACT: a Job owns implicitly shared copies of its proxies, so the
// worker thread never reads TrackingRotoModel state; results return through
// the sample callback only (TrackingRotoModel posts them to its thread).
struct Job {
    QImage reference;           // proxy holding the reference patch
    QPointF seed;               // feature point in the reference proxy
    QPointF start;              // last trusted feature point before frames[0]
    int radius = 5;             // patch half size
    int searchRadius = 16;      // per-frame search half size
    int occlusionFrames = 0;    // consecutive lost frames searched before stopping
    QVector<int> frames;        // frame IDs in analysis order
    QVector<QImage> images;     // proxies matching `frames`
};

// Tagged image -> tracking proxy, or a null image with `error` set.
QImage proxy(const QImage &image, QString *error);
// Standard deviation of the (2r+1)^2 patch centred on (x, y).
double patchVariation(const QImage &image, int x, int y, int radius);
// True when the reference patch fits the proxy and has enough texture.
bool trackableReference(const QImage &reference, QPointF seed, int radius);
// One frame: integer search around `prior` (feature coordinates) followed by
// subpixel refinement; `lost` when the best error exceeds kLostError.
Sample trackFrame(const QImage &reference, QPointF seed, const QImage &target,
                  QPointF prior, int radius, int searchRadius, int frame);
// Runs the whole job; `emitSample` receives every frame in order.
End run(const Job &job, const CancellationToken &cancel,
        const std::function<void(const Sample &)> &emitSample);

} // namespace QindaTK::TrackingEngine
