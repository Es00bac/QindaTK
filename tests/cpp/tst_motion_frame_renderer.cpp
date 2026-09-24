// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/motion_composition.h"
#include "../../src/core/motion_frame_renderer.h"
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
};

QTEST_GUILESS_MAIN(MotionFrameRendererTest)
#include "tst_motion_frame_renderer.moc"
