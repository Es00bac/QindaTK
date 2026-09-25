// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tracking_engine.h"

#include <QColorSpace>
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

} // namespace

QImage proxy(const QImage &image, QString *error)
{
    if (image.isNull() || !image.colorSpace().isValid()
        || qint64(image.width()) * image.height() > kMaxProxyPixels) {
        if (error) *error = QStringLiteral("Tracking frames need a color tag and at most 4 megapixels.");
        return {};
    }
    const QImage srgb = image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
    QImage gray = srgb.convertToFormat(QImage::Format_Grayscale8);
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

End run(const Job &job, const CancellationToken &cancel,
        const std::function<void(const Sample &)> &emitSample)
{
    QPointF last = job.start;
    int lostRun = 0;
    for (qsizetype i = 0; i < job.frames.size(); ++i) {
        if (cancel.isCancelled()) return End::Cancelled;
        // AGENT-GUARD: an occluded feature is searched around its last trusted
        // position with a growing radius; its reported position is held, never
        // extrapolated, and it stays `lost` until the patch matches again.
        const int search = std::min(kMaxSearchRadius, job.searchRadius * (1 + lostRun));
        Sample sample = trackFrame(job.reference, job.seed, job.images.at(i), last,
                                   job.radius, search, job.frames.at(i));
        if (sample.lost) {
            sample.x = last.x();
            sample.y = last.y();
            ++lostRun;
        } else {
            last = QPointF(sample.x, sample.y);
            lostRun = 0;
        }
        emitSample(sample);
        if (lostRun > job.occlusionFrames) return End::Lost;
    }
    return lostRun > 0 ? End::Lost : End::Completed;
}

} // namespace QindaTK::TrackingEngine
