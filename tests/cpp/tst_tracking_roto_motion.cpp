// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-075 known transforms beyond translation: rotation and scale
// (similarity tracks), perspective (four-track corner pin) and footage
// larger than the 4 MP tracking proxy (UHD frames tracked on a downscale).
#include "../../src/core/tracking_roto_model.h"

#include <QColorSpace>
#include <QPolygonF>
#include <QtMath>
#include <QtTest>
#include <algorithm>
#include <array>
#include <cmath>

using namespace QindaTK;

namespace {
struct Blob { double ox, oy, sigma, amplitude; };

// Smooth anisotropic texture at centre c, rotated by `degrees` (clockwise on
// screen) and scaled by `scale`: image point = c + scale * R(degrees) * u.
double texture(const Blob *blobs, int count, double u, double v)
{
    double value = 40.0;
    for (int i = 0; i < count; ++i) {
        const double ex = u - blobs[i].ox, ey = v - blobs[i].oy;
        value += blobs[i].amplitude * std::exp(-(ex * ex + ey * ey) / (2 * blobs[i].sigma * blobs[i].sigma));
    }
    return value;
}

QImage posed(double cx, double cy, double degrees, double scale)
{
    const Blob blobs[] = {{0, 0, 4.8, 120}, {4.8, -3.2, 3.2, -60}, {-4.8, 3.2, 4.0, 70}, {3.2, 4.8, 2.6, 50}};
    const double a = qDegreesToRadians(degrees), c = std::cos(a), s = std::sin(a);
    QImage image(96, 96, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 96; ++x) {
            const double dx = x - cx, dy = y - cy;
            const double v = texture(blobs, 4, (c * dx + s * dy) / scale, (-s * dx + c * dy) / scale);
            const int g = std::clamp(int(std::floor(v + 0.5)), 0, 255);
            image.setPixel(x, y, qRgb(g, g, g));
        }
    return image;
}

// Same 11x11 texture at each corner of a 128x128 frame.
QImage corners(const QVector<QPoint> &at)
{
    QImage image(128, 128, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.fill(QColor(16, 16, 16));
    for (const QPoint &centre : at)
        for (int y = -5; y <= 5; ++y)
            for (int x = -5; x <= 5; ++x) {
                const int v = 55 + ((x + 5) * 17 + (y + 5) * 29) % 175;
                image.setPixel(centre.x() + x, centre.y() + y, qRgb(v, v, v));
            }
    return image;
}

// 3840x2160 frame (8.3 MP) with a smooth feature at (cx, cy).
QImage uhd(double cx, double cy)
{
    const Blob blobs[] = {{0, 0, 9, 120}, {9, -6, 6, -60}, {-9, 6, 7.5, 70}, {6, 9, 4.8, 50}};
    QImage image(3840, 2160, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.fill(qRgb(40, 40, 40));
    for (int y = int(cy) - 48; y <= int(cy) + 48; ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = int(cx) - 48; x <= int(cx) + 48; ++x) {
            const int g = std::clamp(int(std::floor(texture(blobs, 4, x - cx, y - cy) + 0.5)), 0, 255);
            row[x] = qRgb(g, g, g);
        }
    }
    return image;
}

QPointF diagonalsMeet(const QPolygonF &q)
{
    // q0 + t (q2 - q0) = q1 + u (q3 - q1)
    const QPointF d1 = q[2] - q[0], d2 = q[3] - q[1], r = q[1] - q[0];
    const double det = d1.x() * -d2.y() - d1.y() * -d2.x();
    const double t = (r.x() * -d2.y() - r.y() * -d2.x()) / det;
    return q[0] + t * d1;
}

QVariantMap point(const QString &id, double x, double y, double w, double h)
{
    return {{QStringLiteral("pointId"), id}, {QStringLiteral("x"), x / w}, {QStringLiteral("y"), y / h}};
}

QVariantMap keyPoint(const TrackingRotoModel &model, int frame, int index)
{
    for (const QVariant &key : model.masks().first().toMap().value(QStringLiteral("keyframes")).toList())
        if (key.toMap().value(QStringLiteral("frame")).toInt() == frame)
            return key.toMap().value(QStringLiteral("points")).toList().at(index).toMap();
    return {};
}
} // namespace

class TrackingRotoMotionTest final : public QObject {
    Q_OBJECT
private slots:
    void rotationAndScaleAreMeasured()
    {
        TrackingRotoModel model;
        const auto truth = [](int k) {
            return std::array<double, 4>{40 + 1.5 * k, 40 + 0.8 * k, 3.0 * k, std::pow(1.025, k)};
        };
        for (int k = 0; k <= 6; ++k) {
            const auto t = truth(k);
            QVERIFY(model.setFrame(k, posed(t[0], t[1], t[2], t[3])));
        }
        const QString id = model.addTrack(QStringLiteral("turning"), 0, 40, 40, 8, 6);
        QVERIFY(!model.setTrackOptions(id, 8, 6, QStringLiteral("affine")));
        QVERIFY(model.setTrackOptions(id, 8, 6, QStringLiteral("similarity")));
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("motion")).toString(),
                 QStringLiteral("similarity"));
        QVERIFY(model.startAnalysis(id, 1, 6));
        QTRY_VERIFY(!model.analysisRunning());
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        const QVariantList preview = model.tracks().first().toMap().value(QStringLiteral("previewPoints")).toList();
        QCOMPARE(preview.size(), 6);
        double worstXY = 0, worstAngle = 0, worstScale = 0;
        for (int k = 1; k <= 6; ++k) {
            const QVariantMap p = preview.at(k - 1).toMap();
            const auto t = truth(k);
            QCOMPARE(p.value(QStringLiteral("state")).toString(), QStringLiteral("tracked"));
            worstXY = std::max({worstXY, std::abs(p.value(QStringLiteral("x")).toDouble() - t[0]),
                                std::abs(p.value(QStringLiteral("y")).toDouble() - t[1])});
            worstAngle = std::max(worstAngle, std::abs(p.value(QStringLiteral("rotation")).toDouble() - t[2]));
            worstScale = std::max(worstScale, std::abs(p.value(QStringLiteral("scale")).toDouble() - t[3]));
        }
        qInfo("Similarity error: position %.3f px, rotation %.3f deg, scale %.4f",
              worstXY, worstAngle, worstScale);
        QVERIFY2(worstXY < 0.25, qPrintable(QString::number(worstXY)));
        QVERIFY2(worstAngle < 0.5, qPrintable(QString::number(worstAngle)));
        QVERIFY2(worstScale < 0.01, qPrintable(QString::number(worstScale)));
        QVERIFY(model.acceptAnalysis(id));

        // The mask turns and scales about the tracked point.
        const QString maskId = model.addMask(QStringLiteral("turning matte"));
        QVERIFY(model.setMaskKeyframe(maskId, 0, {point(QStringLiteral("a"), 52, 40, 96, 96),
                                                   point(QStringLiteral("b"), 40, 28, 96, 96),
                                                   point(QStringLiteral("c"), 30, 46, 96, 96)}));
        QVERIFY(model.applyPropagation(maskId, 0, {id}, 0, 6).value(QStringLiteral("ok")).toBool());
        const auto t = truth(6);
        const double a = qDegreesToRadians(t[2]);
        // (52, 40) is 12 px right of the feature at frame 0.
        const QPointF expected(t[0] + t[3] * 12 * std::cos(a), t[1] + t[3] * 12 * std::sin(a));
        const QVariantMap mapped = keyPoint(model, 6, 0);
        QVERIFY(std::abs(mapped.value(QStringLiteral("x")).toDouble() * 96 - expected.x()) < 0.5);
        QVERIFY(std::abs(mapped.value(QStringLiteral("y")).toDouble() * 96 - expected.y()) < 0.5);
    }

    void fourTracksCornerPinWithPerspective()
    {
        const QVector<QVector<QPoint>> quads = {
            {{24, 24}, {100, 26}, {104, 100}, {22, 96}},
            {{25, 24}, {101, 27}, {107, 103}, {22, 97}},
            {{26, 25}, {102, 28}, {110, 106}, {23, 98}}};
        TrackingRotoModel model;
        for (int k = 0; k < 3; ++k) QVERIFY(model.setFrame(k, corners(quads[k])));
        QStringList ids;
        for (int i = 0; i < 4; ++i) {
            const QString id = model.addTrack(QStringLiteral("corner %1").arg(i), 0,
                                              quads[0][i].x(), quads[0][i].y(), 5, 4);
            QVERIFY(model.startAnalysis(id, 1, 2));
            QTRY_VERIFY(!model.analysisRunning());
            QVERIFY(model.acceptAnalysis(id));
            ids.append(id);
        }
        const QPolygonF q0(QList<QPointF>(quads[0].cbegin(), quads[0].cend()));
        const QPolygonF q2(QList<QPointF>(quads[2].cbegin(), quads[2].cend()));
        const QPointF d0 = diagonalsMeet(q0), d2 = diagonalsMeet(q2);
        const QString maskId = model.addMask(QStringLiteral("screen"));
        QVERIFY(model.setMaskKeyframe(maskId, 0, {point(QStringLiteral("d"), d0.x(), d0.y(), 128, 128),
                                                   point(QStringLiteral("e"), 40, 40, 128, 128),
                                                   point(QStringLiteral("f"), 80, 44, 128, 128)}));
        QVERIFY(!model.previewPropagation(maskId, 0, ids.mid(0, 3), 0, 2).value(QStringLiteral("ok")).toBool());
        const QVariantMap applied = model.applyPropagation(maskId, 0, ids, 0, 2);
        QVERIFY2(applied.value(QStringLiteral("ok")).toBool(), qPrintable(model.lastError()));
        // A projective map keeps the diagonals' crossing on the diagonals; the
        // affine map through three corners would miss it by 0.67 px.
        const QVariantMap mapped = keyPoint(model, 2, 0);
        QVERIFY(std::abs(mapped.value(QStringLiteral("x")).toDouble() * 128 - d2.x()) < 1e-6);
        QVERIFY(std::abs(mapped.value(QStringLiteral("y")).toDouble() * 128 - d2.y()) < 1e-6);
    }

    void uhdFootageTracksOnADownscaledProxy()
    {
        TrackingRotoModel model;
        const auto truth = [](int k) { return QPointF(1900 + 7.5 * k, 1000 + 3.3 * k); };
        for (int k = 0; k <= 3; ++k) QVERIFY2(model.setFrame(k, uhd(truth(k).x(), truth(k).y())),
                                              qPrintable(model.lastError()));
        const QString id = model.addTrack(QStringLiteral("uhd"), 0, 1900, 1000, 8, 16);
        QVERIFY2(model.startAnalysis(id, 1, 3), qPrintable(model.lastError()));
        QTRY_VERIFY(!model.analysisRunning());
        const QVariantList preview = model.tracks().first().toMap().value(QStringLiteral("previewPoints")).toList();
        QCOMPARE(preview.size(), 3);
        double worst = 0;
        for (int k = 1; k <= 3; ++k) {
            const QVariantMap p = preview.at(k - 1).toMap();
            QCOMPARE(p.value(QStringLiteral("state")).toString(), QStringLiteral("tracked"));
            worst = std::max({worst, std::abs(p.value(QStringLiteral("x")).toDouble() - truth(k).x()),
                              std::abs(p.value(QStringLiteral("y")).toDouble() - truth(k).y())});
        }
        qInfo("UHD tracking error through the proxy: max %.3f source px", worst);
        QVERIFY2(worst < 0.5, qPrintable(QString::number(worst)));
        QVERIFY(model.acceptAnalysis(id));
        // Mask motion is normalized by the source size, not the proxy's.
        const QString maskId = model.addMask(QStringLiteral("uhd matte"));
        QVERIFY(model.setMaskKeyframe(maskId, 0, {point(QStringLiteral("a"), 1880, 980, 3840, 2160),
                                                   point(QStringLiteral("b"), 1920, 980, 3840, 2160),
                                                   point(QStringLiteral("c"), 1900, 1020, 3840, 2160)}));
        const QVariantMap moved = model.previewTrackedMask(maskId, 0, 3, id);
        QVERIFY(moved.value(QStringLiteral("ok")).toBool());
        const double x = moved.value(QStringLiteral("points")).toList().first().toMap()
                             .value(QStringLiteral("x")).toDouble() * 3840;
        QVERIFY2(std::abs(x - (1880 + 22.5)) < 0.5, qPrintable(QString::number(x)));
    }
};

QTEST_GUILESS_MAIN(TrackingRotoMotionTest)
#include "tst_tracking_roto_motion.moc"
