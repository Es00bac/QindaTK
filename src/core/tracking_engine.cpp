// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tracking_engine.h"

#include <QColorSpace>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK::TrackingEngine {
namespace {

bool patchFits(const QImage &image, int x, int y, int radius)
{
    return x - radius >= 0 && y - radius >= 0 && x + radius < image.width() && y + radius < image.height();
}

double patchDifference(const QImage &reference, int rx, int ry,
                       const QImage &target, int tx, int ty, int radius)
{
    qint64 sum = 0;
    const int width = radius * 2 + 1;
    for (int y = -radius; y <= radius; ++y) {
        const auto *a = reference.constScanLine(ry + y) + rx - radius;
        const auto *b = target.constScanLine(ty + y) + tx - radius;
        for (int x = 0; x < width; ++x) sum += std::abs(int(a[x]) - int(b[x]));
    }
    return double(sum) / double(width * width);
}

double patchSquares(const QImage &reference, int rx, int ry,
                    const QImage &target, int tx, int ty, int radius)
{
    qint64 sum = 0;
    const int width = radius * 2 + 1;
    for (int y = -radius; y <= radius; ++y) {
        const auto *a = reference.constScanLine(ry + y) + rx - radius;
        const auto *b = target.constScanLine(ty + y) + tx - radius;
        for (int x = 0; x < width; ++x) {
            const int d = int(a[x]) - int(b[x]);
            sum += qint64(d) * d;
        }
    }
    return double(sum);
}

// Vertex of the parabola through (-1, minus), (0, centre), (1, plus).
double parabolaOffset(double minus, double centre, double plus)
{
    const double curvature = minus - 2.0 * centre + plus;
    if (!(curvature > 0.0)) return 0.0;
    return std::clamp((minus - plus) / (2.0 * curvature), -0.5, 0.5);
}

// Bilinear sample of a Grayscale8 image; false outside its pixel centres.
bool bilinear(const QImage &image, double x, double y, double *value)
{
    const int w = image.width(), h = image.height();
    if (!(x >= 0.0 && y >= 0.0 && x <= w - 1 && y <= h - 1) || w < 2 || h < 2) return false;
    const int x0 = std::min(int(x), w - 2), y0 = std::min(int(y), h - 2);
    const double fx = x - x0, fy = y - y0;
    const auto *r0 = image.constScanLine(y0) + x0;
    const auto *r1 = image.constScanLine(y0 + 1) + x0;
    const double top = r0[0] + fx * (r0[1] - r0[0]);
    const double bottom = r1[0] + fx * (r1[1] - r1[0]);
    *value = top + fy * (bottom - top);
    return true;
}

struct Pose { double x = 0, y = 0, rotation = 0, scale = 1; };

// Reference patch (centred on integer rx, ry; feature at rx + sub) against the
// target sampled under `pose`: mean absolute error, or the sum of squares.
// Infinite when a sample leaves the target.
double warpedError(const QImage &reference, int rx, int ry, QPointF sub,
                   const QImage &target, const Pose &pose, int radius, bool squares)
{
    const double angle = qDegreesToRadians(pose.rotation);
    const double c = std::cos(angle) * pose.scale, s = std::sin(angle) * pose.scale;
    double sum = 0.0;
    for (int v = -radius; v <= radius; ++v) {
        const auto *row = reference.constScanLine(ry + v) + rx;
        const double oy = v - sub.y();
        for (int u = -radius; u <= radius; ++u) {
            const double ox = u - sub.x();
            double t = 0.0;
            if (!bilinear(target, pose.x + c * ox - s * oy, pose.y + s * ox + c * oy, &t))
                return std::numeric_limits<double>::infinity();
            const double d = double(row[u]) - t;
            sum += squares ? d * d : std::abs(d);
        }
    }
    const int width = radius * 2 + 1;
    return squares ? sum : sum / double(width * width);
}

} // namespace

QImage proxy(const QImage &image, QString *error)
{
    if (image.isNull() || !image.colorSpace().isValid()
        || qint64(image.width()) * image.height() > kMaxSourcePixels) {
        if (error) *error = QStringLiteral("Tracking frames need a color tag and at most 64 megapixels.");
        return {};
    }
    const QImage srgb = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    QImage gray = srgb.convertToFormat(QImage::Format_Grayscale8);
    const qint64 pixels = qint64(image.width()) * image.height();
    // AGENT-NOTE: an oversized frame (e.g. 3840x2160) is tracked on a smooth
    // downscale; TrackingRotoModel keeps each frame's source size and maps
    // proxy positions back to source pixels (docs/tracking-roto.md).
    if (!gray.isNull() && pixels > kMaxProxyPixels) {
        const double factor = std::sqrt(double(kMaxProxyPixels) / double(pixels));
        const QSize size(std::max(1, int(image.width() * factor)), std::max(1, int(image.height() * factor)));
        gray = gray.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_Grayscale8);
    }
    if (gray.isNull() && error) *error = QStringLiteral("Tracking frame conversion failed.");
    return gray;
}

double patchVariation(const QImage &image, int x, int y, int radius)
{
    qint64 sum = 0;
    qint64 sumSquares = 0;
    const int width = radius * 2 + 1;
    for (int yy = -radius; yy <= radius; ++yy) {
        const auto *row = image.constScanLine(y + yy) + x - radius;
        for (int xx = 0; xx < width; ++xx) {
            const int v = row[xx];
            sum += v;
            sumSquares += v * v;
        }
    }
    const double n = double(width * width);
    const double mean = double(sum) / n;
    return std::sqrt(std::max(0.0, double(sumSquares) / n - mean * mean));
}

bool trackableReference(const QImage &reference, QPointF seed, int radius)
{
    const int rx = qRound(seed.x()), ry = qRound(seed.y());
    return !reference.isNull() && patchFits(reference, rx, ry, radius)
        && patchVariation(reference, rx, ry, radius) >= kMinTexture;
}

Sample trackFrame(const QImage &reference, QPointF seed, const QImage &target,
                  QPointF prior, int radius, int searchRadius, int frame)
{
    // AGENT-NOTE: the reference patch is centred on the rounded seed; `sub`
    // carries the seed's fractional part so a corrected subpixel seed stays
    // exact. Patch centres are integer; feature points are centre + sub.
    const int rx = qRound(seed.x()), ry = qRound(seed.y());
    const QPointF sub(seed.x() - rx, seed.y() - ry);
    const int cx = qRound(prior.x() - sub.x()), cy = qRound(prior.y() - sub.y());
    double best = std::numeric_limits<double>::infinity();
    int bestX = cx, bestY = cy;
    for (int y = cy - searchRadius; y <= cy + searchRadius; ++y) {
        for (int x = cx - searchRadius; x <= cx + searchRadius; ++x) {
            if (!patchFits(target, x, y, radius)) continue;
            const double difference = patchDifference(reference, rx, ry, target, x, y, radius);
            if (difference < best) { best = difference; bestX = x; bestY = y; }
        }
    }
    Sample sample;
    sample.frame = frame;
    sample.confidence = std::isfinite(best) ? std::max(0.0, 1.0 - best / 64.0) : 0.0;
    sample.lost = !(best <= kLostError);
    double fx = bestX, fy = bestY;
    // AGENT-NOTE: a zero-residual integer match is exact, so refinement is
    // skipped; otherwise a parabola through the squared-error neighbours
    // gives the subpixel offset (measured < 0.1 px on smooth texture,
    // tests/cpp/tst_tracking_roto_model.cpp subpixelTranslation).
    if (!sample.lost && best > 0.0) {
        const double centre = patchSquares(reference, rx, ry, target, bestX, bestY, radius);
        if (patchFits(target, bestX - 1, bestY, radius) && patchFits(target, bestX + 1, bestY, radius))
            fx += parabolaOffset(patchSquares(reference, rx, ry, target, bestX - 1, bestY, radius), centre,
                                 patchSquares(reference, rx, ry, target, bestX + 1, bestY, radius));
        if (patchFits(target, bestX, bestY - 1, radius) && patchFits(target, bestX, bestY + 1, radius))
            fy += parabolaOffset(patchSquares(reference, rx, ry, target, bestX, bestY - 1, radius), centre,
                                 patchSquares(reference, rx, ry, target, bestX, bestY + 1, radius));
    }
    sample.x = fx + sub.x();
    sample.y = fy + sub.y();
    return sample;
}

Sample trackSimilarity(const QImage &reference, QPointF seed, const QImage &target,
                       QPointF prior, double priorRotation, double priorScale,
                       int radius, int searchRadius, int frame)
{
    const int rx = qRound(seed.x()), ry = qRound(seed.y());
    const QPointF sub(seed.x() - rx, seed.y() - ry);
    Sample sample;
    sample.frame = frame;
    Pose best{prior.x(), prior.y(), priorRotation, priorScale};
    double error = std::numeric_limits<double>::infinity();
    for (int j = -searchRadius; j <= searchRadius; ++j)
        for (int i = -searchRadius; i <= searchRadius; ++i) {
            const Pose pose{prior.x() + i, prior.y() + j, priorRotation, priorScale};
            const double e = warpedError(reference, rx, ry, sub, target, pose, radius, false);
            if (e < error) { error = e; best = pose; }
        }
    if (std::isfinite(error)) {
        // AGENT-NOTE: compass (pattern) search on the squared error over
        // x, y, rotation and scale, halving the steps when no move improves.
        // Bounds keep a per-frame change plausible (kMaxRotationStep,
        // kMaxScaleStep) so a flat error surface cannot drift the pose.
        double current = warpedError(reference, rx, ry, sub, target, best, radius, true);
        double stepXY = 0.5, stepRotation = 4.0, stepScale = 0.04;
        for (int iteration = 0; iteration < 400 && stepXY > 0.004; ++iteration) {
            bool improved = false;
            const Pose moves[] = {{stepXY, 0, 0, 1}, {-stepXY, 0, 0, 1}, {0, stepXY, 0, 1},
                                  {0, -stepXY, 0, 1}, {0, 0, stepRotation, 1}, {0, 0, -stepRotation, 1},
                                  {0, 0, 0, 1 + stepScale}, {0, 0, 0, 1 / (1 + stepScale)}};
            for (const Pose &move : moves) {
                const Pose pose{best.x + move.x, best.y + move.y, best.rotation + move.rotation,
                                best.scale * move.scale};
                const double ratio = pose.scale / priorScale;
                if (std::abs(pose.rotation - priorRotation) > kMaxRotationStep
                    || ratio > kMaxScaleStep || ratio < 1.0 / kMaxScaleStep
                    || pose.scale < kMinScale || pose.scale > kMaxScale) continue;
                const double e = warpedError(reference, rx, ry, sub, target, pose, radius, true);
                if (e < current) { current = e; best = pose; improved = true; }
            }
            if (!improved) { stepXY /= 2; stepRotation /= 2; stepScale /= 2; }
        }
        error = warpedError(reference, rx, ry, sub, target, best, radius, false);
    }
    sample.confidence = std::isfinite(error) ? std::max(0.0, 1.0 - error / 64.0) : 0.0;
    sample.lost = !(error <= kLostError);
    sample.x = best.x;
    sample.y = best.y;
    sample.rotation = best.rotation;
    sample.scale = best.scale;
    return sample;
}

End run(const Job &job, const CancellationToken &cancel,
        const std::function<void(const Sample &)> &emitSample)
{
    QPointF last = job.start;
    double lastRotation = job.startRotation, lastScale = job.startScale;
    int lostRun = 0;
    for (qsizetype i = 0; i < job.frames.size(); ++i) {
        if (cancel.isCancelled()) return End::Cancelled;
        // AGENT-GUARD: an occluded feature is searched around its last trusted
        // position with a growing radius; its reported position is held, never
        // extrapolated, and it stays `lost` until the patch matches again.
        const int search = std::min(kMaxSearchRadius, job.searchRadius * (1 + lostRun));
        Sample sample = job.motion == Motion::Similarity
            ? trackSimilarity(job.reference, job.seed, job.images.at(i), last, lastRotation,
                              lastScale, job.radius, search, job.frames.at(i))
            : trackFrame(job.reference, job.seed, job.images.at(i), last,
                         job.radius, search, job.frames.at(i));
        if (sample.lost) {
            sample.x = last.x();
            sample.y = last.y();
            sample.rotation = lastRotation;
            sample.scale = lastScale;
            ++lostRun;
        } else {
            last = QPointF(sample.x, sample.y);
            lastRotation = sample.rotation;
            lastScale = sample.scale;
            lostRun = 0;
        }
        emitSample(sample);
        if (lostRun > job.occlusionFrames) return End::Lost;
    }
    return lostRun > 0 ? End::Lost : End::Completed;
}

} // namespace QindaTK::TrackingEngine
