// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/automation_model.h"
#include "../../src/core/motion_composition.h"
#include "../../src/core/motion_frame_renderer.h"
#include "../../src/core/motion_onion_skin_item.h"
#include <QPainter>
#include <QtTest>

using namespace QindaTK;

class MotionOverlaysTest final : public QObject {
    Q_OBJECT
private slots:
    void onionSkinUsesAdjacentRenderedFramesAndRestoresCurrent()
    {
        AutomationModel model;
        QVERIFY(model.addParameterMap({{QStringLiteral("id"), QStringLiteral("x")},
            {QStringLiteral("name"), QStringLiteral("X")}, {QStringLiteral("dimension"), QStringLiteral("length")},
            {QStringLiteral("unitId"), QStringLiteral("px")}, {QStringLiteral("minimum"), 0.0},
            {QStringLiteral("maximum"), 7.0}, {QStringLiteral("default"), 1.0},
            {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool());
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")}, {QStringLiteral("parameterId"), QStringLiteral("x")}},
            {{QStringLiteral("ticks"), QStringLiteral("0")}, {QStringLiteral("value"), 1.0}, {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool());
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")}, {QStringLiteral("parameterId"), QStringLiteral("x")}},
            {{QStringLiteral("ticks"), QStringLiteral("10")}, {QStringLiteral("value"), 5.0}, {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool());
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")}, {QStringLiteral("parameterId"), QStringLiteral("x")}},
            {{QStringLiteral("ticks"), QStringLiteral("20")}, {QStringLiteral("value"), 3.0}, {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool());
        MotionComposition composition;
        composition.setAnimationModel(&model);
        const QVariantMap layer{{QStringLiteral("layerId"), QStringLiteral("shape")},
            {QStringLiteral("sourceId"), QStringLiteral("pixel")}, {QStringLiteral("zIndex"), 0},
            {QStringLiteral("transform"), QVariantMap{{QStringLiteral("y"), 1.0}}},
            {QStringLiteral("animatedProperties"), QVariantMap{{QStringLiteral("x"), QStringLiteral("x")}}}};
        composition.setLayers({layer});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        QImage source(1, 1, QImage::Format_ARGB32_Premultiplied);
        source.fill(Qt::white);
        QVERIFY(renderer.setSourceImage(QStringLiteral("pixel"), source));

        MotionOnionSkinItem item;
        item.setRenderer(&renderer);
        item.setPreviousTicks(QStringLiteral("0"));
        item.setCurrentTicks(QStringLiteral("20"));
        item.setNextTicks(QStringLiteral("10"));
        item.setFrameWidth(8);
        item.setFrameHeight(4);
        item.setPreviousTint(QColor(255, 0, 0));
        item.setNextTint(QColor(0, 0, 255));
        item.setNeighborOpacity(0.5);
        QVERIFY(item.refresh());
        QCOMPARE(renderer.frame().pixelColor(3, 1), QColor(Qt::white));
        QImage output(8, 4, QImage::Format_ARGB32_Premultiplied);
        output.fill(Qt::transparent);
        QPainter painter(&output);
        item.setWidth(8); item.setHeight(4);
        item.paint(&painter);
        painter.end();
        const QColor previous = output.pixelColor(1, 1);
        QVERIFY(previous.red() > 200 && previous.blue() < 80);
        const QColor next = output.pixelColor(5, 1);
        QVERIFY(next.blue() > 200 && next.red() < 80);
    }
};

QTEST_MAIN(MotionOverlaysTest)
#include "tst_motion_overlays.moc"
