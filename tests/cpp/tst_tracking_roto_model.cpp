// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/tracking_roto_model.h"
#include "../../src/core/motion_composition.h"
#include "../../src/core/motion_frame_renderer.h"

#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>

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
};

QTEST_GUILESS_MAIN(TrackingRotoModelTest)
#include "tst_tracking_roto_model.moc"
