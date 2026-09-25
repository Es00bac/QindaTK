// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>
#include <functional>

namespace QindaTK::TrackingEngine {

// Tracking proxies: encoded sRGB Grayscale8, at most this many pixels. A
// larger source frame is downscaled to fit (proxy()), and TrackingRotoModel
// maps positions back to source pixels.
inline constexpr qint64 kMaxProxyPixels = 4 * 1024 * 1024;
// Largest source frame accepted for proxying (8K UHD is 33 MP).
inline constexpr qint64 kMaxSourcePixels = 64 * 1024 * 1024;
// Aggregate proxy storage per TrackingRotoModel.
inline constexpr qsizetype kMaxProxyBytes = 256 * 1024 * 1024;
// Mean absolute patch error above which a frame reports `lost`.
inline constexpr double kLostError = 16.0;
// Search radius cap while a feature is occluded (radius grows per lost frame).
inline constexpr int kMaxSearchRadius = 64;
// Minimum patch standard deviation accepted as a trackable reference.
inline constexpr double kMinTexture = 6.0;

// Similarity search bounds: per-frame change and absolute range.
inline constexpr double kMaxRotationStep = 20.0; // degrees per frame
inline constexpr double kMaxScaleStep = 1.25;    // ratio per frame (and its inverse)
inline constexpr double kMinScale = 0.25;
inline constexpr double kMaxScale = 4.0;

// Translation matches an axis-aligned patch; Similarity also solves the
// patch's rotation and uniform scale relative to the reference (D-143).
enum class Motion { Translation, Similarity };

struct Sample {
    int frame = 0;
    double x = 0; // feature position, proxy pixels
    double y = 0;
    double rotation = 0; // degrees, clockwise on screen (image y points down)
    double scale = 1;
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
    double startRotation = 0;   // last trusted rotation/scale (Similarity)
    double startScale = 1;
    Motion motion = Motion::Translation;
    int radius = 5;             // patch half size
    int searchRadius = 16;      // per-frame search half size
    int occlusionFrames = 0;    // consecutive lost frames searched before stopping
    QVector<int> frames;        // frame IDs in analysis order
    QVector<QImage> images;     // proxies matching `frames`
};

// Tagged image -> tracking proxy, or a null image with `error` set. Frames
// above kMaxProxyPixels are downscaled to fit, keeping the aspect ratio.
QImage proxy(const QImage &image, QString *error);
// Standard deviation of the (2r+1)^2 patch centred on (x, y).
double patchVariation(const QImage &image, int x, int y, int radius);
// True when the reference patch fits the proxy and has enough texture.
bool trackableReference(const QImage &reference, QPointF seed, int radius);
// One frame: integer search around `prior` (feature coordinates) followed by
// subpixel refinement; `lost` when the best error exceeds kLostError.
Sample trackFrame(const QImage &reference, QPointF seed, const QImage &target,
                  QPointF prior, int radius, int searchRadius, int frame);
// Similarity variant: integer search under the prior rotation/scale, then a
// pattern search over position, rotation and scale on bilinear samples.
Sample trackSimilarity(const QImage &reference, QPointF seed, const QImage &target,
                       QPointF prior, double priorRotation, double priorScale,
                       int radius, int searchRadius, int frame);
// Runs the whole job; `emitSample` receives every frame in order.
End run(const Job &job, const CancellationToken &cancel,
        const std::function<void(const Sample &)> &emitSample);

} // namespace QindaTK::TrackingEngine
