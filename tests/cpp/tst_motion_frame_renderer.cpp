// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/motion_composition.h"
#include "../../src/core/motion_frame_renderer.h"
#include <QColorSpace>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class MotionFrameRendererTest final : public QObject {
    Q_OBJECT
private slots:
    void compositesLayerPixelsAndExportsSameFrame()
    {
        MotionComposition composition;
        const auto layer = [](const QString &id, const QString &source, int z, int x, double opacity) {
            return QVariantMap{{QStringLiteral("layerId"), id},
                               {QStringLiteral("sourceId"), source},
                               {QStringLiteral("zIndex"), z},
                               {QStringLiteral("visible"), true},
                               {QStringLiteral("transform"), QVariantMap{
                                    {QStringLiteral("x"), x}, {QStringLiteral("y"), 1},
                                    {QStringLiteral("opacity"), opacity}}}};
        };
        composition.setLayers({layer(QStringLiteral("red"), QStringLiteral("red-source"), 0, 1, 1.0),
                               layer(QStringLiteral("blue"), QStringLiteral("blue-source"), 1, 2, 0.5)});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        QImage red(2, 2, QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::red);
        QImage blue(2, 2, QImage::Format_ARGB32_Premultiplied);
        blue.fill(Qt::blue);
        QVERIFY(renderer.setSourceImage(QStringLiteral("red-source"), red));
        QVERIFY(renderer.setSourceImage(QStringLiteral("blue-source"), blue));
        const QVariantMap result = renderer.render(QStringLiteral("0"), 8, 8);
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("drawnLayers")).toInt(), 2);
        const QImage frame = renderer.frame();
        QCOMPARE(frame.pixelColor(1, 1), QColor(Qt::red));
        const QColor overlap = frame.pixelColor(2, 1);
        QVERIFY(qAbs(overlap.red() - 127) <= 1);
        QCOMPARE(overlap.green(), 0);
        QVERIFY(qAbs(overlap.blue() - 128) <= 1);
        QCOMPARE(overlap.alpha(), 255);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("frame.png"));
        QVERIFY(renderer.exportPng(QUrl::fromLocalFile(path)));
        const QImage reopened(path);
        QVERIFY(!reopened.isNull());
        QCOMPARE(reopened.pixelColor(2, 1), overlap);
    }

    void orderedEffectsSkipDisabledRejectUnsupportedAndUseHostRegistry()
    {
        MotionComposition composition;
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        QImage red(1, 1, QImage::Format_RGBA8888);
        red.setColorSpace(QColorSpace(QColorSpace::SRgb));
        red.fill(QColor(Qt::red));
        QVERIFY(renderer.setSourceImage(QStringLiteral("pixel"), red));
        const auto layer = [](const QVariantList &effects) {
            return QVariantMap{{QStringLiteral("layerId"), QStringLiteral("layer")},
                               {QStringLiteral("sourceId"), QStringLiteral("pixel")},
                               {QStringLiteral("zIndex"), 0}, {QStringLiteral("visible"), true},
                               {QStringLiteral("effects"), effects},
                               {QStringLiteral("transform"), QVariantMap{}}};
        };
        const QVariantMap bright{{QStringLiteral("effectId"), QStringLiteral("brightness")},
                                 {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("amount"), 0.5}}}};
        const QVariantMap gray{{QStringLiteral("effectId"), QStringLiteral("grayscale")}};
        composition.setLayers({layer({bright, gray})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        const QColor brightThenGray = renderer.frame().pixelColor(0, 0);
        QVERIFY(brightThenGray.red() > 150);
        QCOMPARE(brightThenGray.red(), brightThenGray.green());
        QCOMPARE(brightThenGray.green(), brightThenGray.blue());

        composition.setLayers({layer({gray, bright})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        const QColor grayThenBright = renderer.frame().pixelColor(0, 0);
        QVERIFY(grayThenBright.red() > brightThenGray.red() + 15);
        QCOMPARE(grayThenBright.red(), grayThenBright.green());
        QCOMPARE(grayThenBright.green(), grayThenBright.blue());

        composition.setLayers({layer({QVariantMap{{QStringLiteral("effectId"), QStringLiteral("missing")},
                                                    {QStringLiteral("enabled"), false}}})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(0, 0), QColor(Qt::red));

        const QImage previous = renderer.frame();
        composition.setLayers({layer({QVariantMap{{QStringLiteral("effectId"), QStringLiteral("missing")}}})});
        const QVariantMap rejected = renderer.render(QStringLiteral("0"), 1, 1);
        QVERIFY(!rejected.value(QStringLiteral("ok")).toBool());
        QVERIFY(renderer.lastError().contains(QStringLiteral("Unsupported enabled motion effect")));
        QCOMPARE(renderer.frame(), previous);

        QVERIFY(renderer.registerEffectProcessor(QStringLiteral("invert"),
            [](QImage &image, const QVariantMap &parameters, QString *error) {
                if (!parameters.isEmpty()) { *error = QStringLiteral("invert takes no parameters"); return false; }
                for (int y = 0; y < image.height(); ++y) {
                    uchar *row = image.scanLine(y);
                    for (int x = 0; x < image.width(); ++x)
                        for (int c = 0; c < 3; ++c) row[x * 4 + c] = uchar(255 - row[x * 4 + c]);
                }
                return true;
            }));
        composition.setLayers({layer({QVariantMap{{QStringLiteral("effectId"), QStringLiteral("invert")}}})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(0, 0), QColor(0, 255, 255));
    }
};

QTEST_GUILESS_MAIN(MotionFrameRendererTest)
#include "tst_motion_frame_renderer.moc"
