// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/tracking_roto_model.h"
#include "../../src/core/motion_composition.h"
#include "../../src/core/motion_frame_renderer.h"

#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>

using namespace QindaTK;

class TrackingRotoModelTest final : public QObject {
    Q_OBJECT
private:
    static QImage patterned(int dx, int dy, bool occluded = false)
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        image.fill(QColor(16, 16, 16));
        if (!occluded) {
            for (int y = -5; y <= 5; ++y)
                for (int x = -5; x <= 5; ++x) {
                    const int value = 55 + ((x + 5) * 17 + (y + 5) * 29) % 175;
                    image.setPixelColor(20 + dx + x, 20 + dy + y,
                                        QColor(value, value, value));
                }
        }
        return image;
    }

    // Smooth, textured feature at (20 + dx, 20 + dy): sums of Gaussians give
    // exact fractional shifts after 8-bit rounding.
    static QImage smooth(double dx, double dy)
    {
        struct Blob { double ox, oy, sigma, amplitude; };
        const Blob blobs[] = {{0, 0, 3.0, 120}, {3, -2, 2.0, -60}, {-3, 2, 2.5, 70}, {2, 3, 1.6, 50}};
        QImage image(64, 64, QImage::Format_ARGB32);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                double v = 40.0;
                for (const Blob &b : blobs) {
                    const double ex = x - 20 - dx - b.ox, ey = y - 20 - dy - b.oy;
                    v += b.amplitude * std::exp(-(ex * ex + ey * ey) / (2 * b.sigma * b.sigma));
                }
                const int g = std::clamp(int(std::floor(v + 0.5)), 0, 255);
                image.setPixel(x, y, qRgb(g, g, g));
            }
        return image;
    }

    // Moving pattern (2 px right, 1 px down per frame); frames 3 and 4 occluded.
    static void loadOccludedRun(TrackingRotoModel &model)
    {
        for (int k = 0; k <= 6; ++k)
            QVERIFY(model.setFrame(k, patterned(2 * k, k, k == 3 || k == 4)));
    }

    static QVariantMap preview(const TrackingRotoModel &model, int track, int index)
    {
        return model.tracks().at(track).toMap().value(QStringLiteral("previewPoints")).toList().at(index).toMap();
    }

    static QVariantList square(double left, double top, double size = 10)
    {
        QVariantList points;
        const double xs[] = {left, left + size, left + size, left};
        const double ys[] = {top, top, top + size, top + size};
        for (int i = 0; i < 4; ++i)
            points.append(QVariantMap{{QStringLiteral("pointId"), QStringLiteral("p%1").arg(i)},
                                      {QStringLiteral("x"), xs[i] / 64.0}, {QStringLiteral("y"), ys[i] / 64.0}});
        return points;
    }

    static QVariantList triangle(int offset = 0)
    {
        return {QVariantMap{{QStringLiteral("pointId"), QStringLiteral("a")},
                             {QStringLiteral("x"), (10 + offset) / 64.0}, {QStringLiteral("y"), 10 / 64.0}},
                QVariantMap{{QStringLiteral("pointId"), QStringLiteral("b")},
                             {QStringLiteral("x"), (30 + offset) / 64.0}, {QStringLiteral("y"), 10 / 64.0}},
                QVariantMap{{QStringLiteral("pointId"), QStringLiteral("c")},
                             {QStringLiteral("x"), (10 + offset) / 64.0}, {QStringLiteral("y"), 30 / 64.0}}};
    }

private slots:
    void shiftedFeatureReportsLossAndAcceptsCorrection()
    {
        TrackingRotoModel model;
        QVERIFY(model.setFrame(0, patterned(0, 0)));
        QVERIFY(model.setFrame(1, patterned(3, 2)));
        QVERIFY(model.setFrame(2, patterned(6, 4)));
        QVERIFY(model.setFrame(3, patterned(0, 0, true)));
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 9);
        QVERIFY(!id.isEmpty());
        QVERIFY(model.startAnalysis(id, 1, 3));
        QTRY_VERIFY(!model.analysisRunning());
        QVariantMap track = model.tracks().first().toMap();
        QCOMPARE(track.value(QStringLiteral("state")).toString(), QStringLiteral("lost"));
        const QVariantList preview = track.value(QStringLiteral("previewPoints")).toList();
        QCOMPARE(preview.size(), 3);
        QCOMPARE(preview.at(0).toMap().value(QStringLiteral("x")).toDouble(), 23.0);
        QCOMPARE(preview.at(1).toMap().value(QStringLiteral("y")).toDouble(), 24.0);
        QCOMPARE(preview.at(2).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("lost"));
        QVERIFY(preview.at(2).toMap().value(QStringLiteral("confidence")).toDouble() < 0.75);
        QCOMPARE(track.value(QStringLiteral("points")).toList().size(), 1);
        QVERIFY(model.acceptAnalysis(id));
        const QString maskId = model.addMask(QStringLiteral("tracked polygon"));
        QVERIFY(model.setMaskKeyframe(maskId, 1, triangle()));
        const QVariantMap propagated = model.previewTrackedMask(maskId, 1, 2, id);
        QVERIFY(propagated.value(QStringLiteral("ok")).toBool());
        QCOMPARE(propagated.value(QStringLiteral("points")).toList().first().toMap()
                     .value(QStringLiteral("x")).toDouble(), 13.0 / 64.0);
        QVERIFY(model.correctPoint(id, 3, 30, 25));
        track = model.tracks().first().toMap();
        QCOMPARE(track.value(QStringLiteral("points")).toList().last().toMap()
                     .value(QStringLiteral("state")).toString(), QStringLiteral("corrected"));
        QVERIFY(model.offsetTrack(id, 1, -1, 2, 3));
        QVERIFY(model.undo());
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("points")).toList().last().toMap()
                     .value(QStringLiteral("x")).toDouble(), 30.0);
        QVERIFY(model.redo());
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("points")).toList().last().toMap()
                     .value(QStringLiteral("x")).toDouble(), 31.0);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl saved = QUrl::fromLocalFile(directory.filePath(QStringLiteral("track.json")));
        QVERIFY(model.saveToFile(saved));
        TrackingRotoModel reopened;
        QVERIFY(reopened.loadFromFile(saved));
        QCOMPARE(reopened.tracks().first().toMap().value(QStringLiteral("points")).toList().last().toMap()
                     .value(QStringLiteral("x")).toDouble(), 31.0);
    }

    void cancellationNeverAppliesPreview()
    {
        TrackingRotoModel model;
        QVERIFY(model.setFrame(0, patterned(0, 0)));
        QVERIFY(model.setFrame(1, patterned(2, 1)));
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20);
        QVERIFY(model.startAnalysis(id, 1, 1));
        model.cancelAnalysis();
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("state")).toString(),
                 QStringLiteral("cancelled"));
        QVERIFY(!model.acceptAnalysis(id));
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("points")).toList().size(), 1);
    }

    void maskInterpolatesRendersAndSurvivesReopen()
    {
        TrackingRotoModel model;
        const QString id = model.addMask(QStringLiteral("person"));
        QVERIFY(model.setMaskKeyframe(id, 0, triangle()));
        QVERIFY(model.setMaskKeyframe(id, 2, triangle(10)));
        const QVariantMap middle = model.evaluateMask(id, 1);
        QCOMPARE(middle.value(QStringLiteral("interpolation")).toString(), QStringLiteral("linear"));
        QCOMPARE(middle.value(QStringLiteral("points")).toList().first().toMap()
                     .value(QStringLiteral("x")).toDouble(), 15.0 / 64.0);
        QVariantList changedTopology = triangle(20);
        changedTopology[0] = QVariantMap{{QStringLiteral("pointId"), QStringLiteral("new")},
                                         {QStringLiteral("x"), 30 / 64.0}, {QStringLiteral("y"), 10 / 64.0}};
        QVERIFY(model.setMaskKeyframe(id, 4, changedTopology));
        QCOMPARE(model.evaluateMask(id, 3).value(QStringLiteral("interpolation")).toString(),
                 QStringLiteral("heldTopology"));
        QImage red(64, 64, QImage::Format_ARGB32_Premultiplied);
        red.setColorSpace(QColorSpace(QColorSpace::SRgb));
        red.fill(Qt::red);
        const QImage masked = model.applyMask(id, 1, red);
        QVERIFY(!masked.isNull());
        QCOMPARE(masked.pixelColor(18, 14).alpha(), 255);
        QCOMPARE(masked.pixelColor(2, 2).alpha(), 0);
        QCOMPARE(masked.pixelColor(12, 14).alpha(), 0);
        QVERIFY(model.setMaskFeather(id, 3));
        const QImage feathered = model.applyMask(id, 1, red);
        QVERIFY(feathered.pixelColor(14, 14).alpha() > 0);
        QVERIFY(feathered.pixelColor(14, 14).alpha() < 255);
        QVERIFY(model.undo());
        QCOMPARE(model.masks().first().toMap().value(QStringLiteral("feather")).toDouble(), 0.0);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl url = QUrl::fromLocalFile(directory.filePath(QStringLiteral("tracking.json")));
        QVERIFY(model.saveToFile(url));
        TrackingRotoModel reopened;
        QVERIFY(reopened.loadFromFile(url));
        QCOMPARE(reopened.evaluateMask(id, 1).value(QStringLiteral("interpolation")).toString(),
                 QStringLiteral("linear"));
        QCOMPARE(reopened.applyMask(id, 1, red).pixelColor(18, 14).alpha(), 255);

        MotionComposition composition;
        composition.setLayers({QVariantMap{{QStringLiteral("layerId"), QStringLiteral("masked")},
            {QStringLiteral("sourceId"), QStringLiteral("red")},
            {QStringLiteral("maskId"), id},
            {QStringLiteral("visible"), true}, {QStringLiteral("transform"), QVariantMap{}}}});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        renderer.setRotoModel(&reopened);
        QVERIFY(renderer.setSourceImage(QStringLiteral("red"), red));
        QVERIFY(renderer.render(QStringLiteral("1"), 64, 64).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(18, 14), QColor(Qt::red));
        QCOMPARE(renderer.frame().pixelColor(2, 2).alpha(), 0);
        const QImage previous = renderer.frame();
        QVERIFY(!renderer.render(QStringLiteral("-1"), 64, 64).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame(), previous);
    }

    void subpixelTranslationIsMeasured()
    {
        TrackingRotoModel model;
        for (int k = 0; k <= 5; ++k) QVERIFY(model.setFrame(k, smooth(1.3 * k, 0.6 * k)));
        const QString id = model.addTrack(QStringLiteral("smooth"), 0, 20, 20, 5, 6);
        QVERIFY(model.startAnalysis(id, 1, 5));
        QTRY_VERIFY(!model.analysisRunning());
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        double sum = 0.0, worst = 0.0;
        for (int k = 1; k <= 5; ++k) {
            const QVariantMap point = preview(model, 0, k - 1);
            QCOMPARE(point.value(QStringLiteral("frame")).toInt(), k);
            QCOMPARE(point.value(QStringLiteral("state")).toString(), QStringLiteral("tracked"));
            const double ex = std::abs(point.value(QStringLiteral("x")).toDouble() - (20 + 1.3 * k));
            const double ey = std::abs(point.value(QStringLiteral("y")).toDouble() - (20 + 0.6 * k));
            sum += ex + ey;
            worst = std::max({worst, ex, ey});
        }
        // Integer matching alone errs by up to 0.5 px here (frame 5 is at 26.5).
        qInfo("Subpixel tracking error: mean %.3f px, max %.3f px", sum / 10.0, worst);
        QVERIFY2(worst < 0.2, qPrintable(QString::number(worst)));
    }

    void occlusionHoldsLostFramesAndReacquires()
    {
        TrackingRotoModel model;
        loadOccludedRun(model);
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 3);
        QVERIFY(!model.startAnalysis(id, 1, 6, 31));
        QVERIFY(model.startAnalysis(id, 1, 6, 2));
        QVERIFY(!model.startAnalysis(id, 1, 6, 2)); // one job at a time
        QTRY_VERIFY(!model.analysisRunning());
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(model.tracks().first().toMap().value(QStringLiteral("previewPoints")).toList().size(), 6);
        const int expectX[] = {22, 24, 24, 24, 30, 32};
        const int expectY[] = {21, 22, 22, 22, 25, 26};
        for (int i = 0; i < 6; ++i) {
            const QVariantMap point = preview(model, 0, i);
            const bool occluded = i == 2 || i == 3;
            QCOMPARE(point.value(QStringLiteral("state")).toString(),
                     occluded ? QStringLiteral("lost") : QStringLiteral("tracked"));
            QCOMPARE(point.value(QStringLiteral("x")).toDouble(), double(expectX[i]));
            QCOMPARE(point.value(QStringLiteral("y")).toDouble(), double(expectY[i]));
            if (occluded) QVERIFY(point.value(QStringLiteral("confidence")).toDouble() < 0.75);
        }

        // With one occlusion frame allowed the second lost frame stops the run.
        const QString strict = model.addTrack(QStringLiteral("strict"), 0, 20, 20, 5, 3);
        QVERIFY(model.startAnalysis(strict, 1, 6, 1));
        QTRY_VERIFY(!model.analysisRunning());
        const QVariantMap track = model.tracks().at(1).toMap();
        QCOMPARE(track.value(QStringLiteral("state")).toString(), QStringLiteral("lost"));
        QCOMPARE(track.value(QStringLiteral("previewPoints")).toList().size(), 4);
        QVERIFY(model.lastError().contains(QStringLiteral("frame 4")));
    }

    void backwardTrackingRebaseAndReplacedFrames()
    {
        TrackingRotoModel model;
        loadOccludedRun(model);
        const QString back = model.addTrack(QStringLiteral("back"), 2, 24, 22, 5, 3);
        QVERIFY(model.startAnalysis(back, 1, 0));
        QTRY_VERIFY(!model.analysisRunning());
        QCOMPARE(preview(model, 0, 0).value(QStringLiteral("frame")).toInt(), 0);
        QCOMPARE(preview(model, 0, 0).value(QStringLiteral("x")).toDouble(), 20.0);
        QCOMPARE(preview(model, 0, 1).value(QStringLiteral("x")).toDouble(), 22.0);
        QVERIFY(model.acceptAnalysis(back));

        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 3);
        QVERIFY(model.startAnalysis(id, 1, 6, 2));
        QTRY_VERIFY(!model.analysisRunning());
        QVERIFY(model.acceptAnalysis(id));
        QVERIFY(!model.rebaseTrack(id, 3)); // lost frames never become references
        QVERIFY(model.rebaseTrack(id, 5));
        QVariantMap track = model.tracks().at(1).toMap();
        QCOMPARE(track.value(QStringLiteral("referenceFrame")).toInt(), 5);
        QCOMPARE(track.value(QStringLiteral("seedX")).toDouble(), 30.0);
        QVERIFY(model.startAnalysis(id, 6, 6));
        QTRY_VERIFY(!model.analysisRunning());
        QCOMPARE(preview(model, 1, 0).value(QStringLiteral("x")).toDouble(), 32.0);
        QVERIFY(model.undo());
        QCOMPARE(model.tracks().at(1).toMap().value(QStringLiteral("referenceFrame")).toInt(), 0);

        // Replacing a frame cancels the worker job; its late results never apply.
        QVERIFY(model.startAnalysis(id, 1, 6, 2));
        QVERIFY(model.setFrame(6, patterned(12, 6)));
        QVERIFY(!model.analysisRunning());
        QCOMPARE(model.tracks().at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("cancelled"));
        QTest::qWait(100);
        QVERIFY(model.tracks().at(1).toMap().value(QStringLiteral("previewPoints")).toList().isEmpty());
        QVERIFY(!model.acceptAnalysis(id));
    }

    void trackedMaskKeysTrustedFramesAndRendersLayerTime()
    {
        TrackingRotoModel model;
        loadOccludedRun(model);
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 3);
        QVERIFY(model.startAnalysis(id, 1, 6, 2));
        QTRY_VERIFY(!model.analysisRunning());
        QVERIFY(model.acceptAnalysis(id));
        const QString maskId = model.addMask(QStringLiteral("follow"));
        QVERIFY(model.setMaskKeyframe(maskId, 0, square(15, 15)));
        QVERIFY(!model.applyTrackToMask(maskId, 3, id, 0, 6).value(QStringLiteral("ok")).toBool());
        const QVariantMap applied = model.applyTrackToMask(maskId, 0, id, 0, 6);
        QVERIFY2(applied.value(QStringLiteral("ok")).toBool(), qPrintable(model.lastError()));
        QCOMPARE(applied.value(QStringLiteral("keyedFrames")).toList(), (QVariantList{1, 2, 5, 6}));
        QCOMPARE(applied.value(QStringLiteral("skippedFrames")).toList(), (QVariantList{3, 4}));
        QCOMPARE(model.evaluateMask(maskId, 5).value(QStringLiteral("points")).toList().first().toMap()
                     .value(QStringLiteral("x")).toDouble(), 25.0 / 64.0);
        QVERIFY(model.undo()); // one undo step for the whole propagation
        QCOMPARE(model.masks().first().toMap().value(QStringLiteral("keyframes")).toList().size(), 1);
        QVERIFY(model.redo());
        QCOMPARE(model.masks().first().toMap().value(QStringLiteral("keyframes")).toList().size(), 5);

        QImage red(64, 64, QImage::Format_ARGB32_Premultiplied);
        red.setColorSpace(QColorSpace(QColorSpace::SRgb));
        red.fill(Qt::red);
        MotionComposition composition;
        composition.setLayers({QVariantMap{{QStringLiteral("layerId"), QStringLiteral("masked")},
            {QStringLiteral("sourceId"), QStringLiteral("red")}, {QStringLiteral("maskId"), maskId},
            {QStringLiteral("timeOffsetTicks"), QStringLiteral("4")},
            {QStringLiteral("visible"), true}, {QStringLiteral("transform"), QVariantMap{}}}});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        renderer.setRotoModel(&model);
        QVERIFY(renderer.setSourceImage(QStringLiteral("red"), red));
        // Composition 9 is layer frame 5: square [25,35]x[20,30]. Frame 9 would
        // hold key 6, [27,37]x[21,31], which excludes pixel (26,22).
        QVERIFY2(renderer.render(QStringLiteral("9"), 64, 64).value(QStringLiteral("ok")).toBool(),
                 qPrintable(renderer.lastError()));
        QCOMPARE(renderer.frame().pixelColor(26, 22).alpha(), 255);
        QCOMPARE(renderer.frame().pixelColor(36, 30).alpha(), 0);
        QVERIFY(!renderer.render(QStringLiteral("3"), 64, 64).value(QStringLiteral("ok")).toBool());

        renderer.setMaskTicksPerFrame(QStringLiteral("10"));
        QCOMPARE(renderer.maskTicksPerFrame(), QStringLiteral("10"));
        QVERIFY(renderer.render(QStringLiteral("54"), 64, 64).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(26, 22).alpha(), 255); // local 50 -> frame 5
        QVERIFY(renderer.render(QStringLiteral("64"), 64, 64).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(26, 22).alpha(), 0);   // local 60 -> frame 6
        QCOMPARE(renderer.frame().pixelColor(36, 30).alpha(), 255);

        // A decoded source frame index wins over layer time.
        QVERIFY(renderer.setSourceImage(QStringLiteral("red"), red, 5));
        QCOMPARE(renderer.sourceFrame(QStringLiteral("red")), QStringLiteral("5"));
        QVERIFY(renderer.render(QStringLiteral("64"), 64, 64).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(26, 22).alpha(), 255);
        QVERIFY(renderer.setSourceImage(QStringLiteral("red"), red));
        QVERIFY(renderer.sourceFrame(QStringLiteral("red")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TrackingRotoModelTest)
#include "tst_tracking_roto_model.moc"
