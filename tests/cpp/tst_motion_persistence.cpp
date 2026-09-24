// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/automation_model.h"
#include "../../src/core/motion_composition.h"
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class MotionPersistenceTest final : public QObject {
    Q_OBJECT
private:
    static bool addParameter(AutomationModel &model, const QString &id, double minimum,
                             double maximum, double initial, const QVariantList &points)
    {
        const auto added = model.addParameterMap({{QStringLiteral("id"), id},
            {QStringLiteral("name"), id}, {QStringLiteral("dimension"), QStringLiteral("length")},
            {QStringLiteral("unitId"), QStringLiteral("px")}, {QStringLiteral("minimum"), minimum},
            {QStringLiteral("maximum"), maximum}, {QStringLiteral("default"), initial},
            {QStringLiteral("interpolation"), QStringLiteral("linear")}});
        if (!added.value(QStringLiteral("ok")).toBool()) return false;
        for (const QVariant &entry : points) {
            const QVariantMap point = entry.toMap();
            const QVariantMap result = model.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                {QStringLiteral("parameterId"), id}}, point);
            if (!result.value(QStringLiteral("ok")).toBool()) return false;
        }
        return true;
    }
private slots:
    void roundTripsExactKeysLayerReferencesAndEffectRecords()
    {
        const QString start = QStringLiteral("9007199254740990");
        const QString middle = QStringLiteral("9007199254741090");
        const QString end = QStringLiteral("9007199254741190");
        AutomationModel sourceAnimation;
        QVERIFY(addParameter(sourceAnimation, QStringLiteral("layer/box/x"), 0, 100, 8,
            {QVariantMap{{QStringLiteral("ticks"), start}, {QStringLiteral("value"), 8.0},
                {QStringLiteral("interpolation"), QStringLiteral("linear")}}}));
        QVERIFY(sourceAnimation.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")}, {QStringLiteral("parameterId"), QStringLiteral("layer/box/x")}},
            {{QStringLiteral("ticks"), end}, {QStringLiteral("value"), 32.0}, {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool());
        QVERIFY(addParameter(sourceAnimation, QStringLiteral("layer/box/opacity"), 0, 1, 1,
            {QVariantMap{{QStringLiteral("ticks"), start}, {QStringLiteral("value"), 1.0},
                {QStringLiteral("interpolation"), QStringLiteral("linear")}}}));
        QVERIFY(sourceAnimation.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")}, {QStringLiteral("parameterId"), QStringLiteral("layer/box/opacity")}},
            {{QStringLiteral("ticks"), end}, {QStringLiteral("value"), 0.5}, {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool());

        MotionComposition source;
        source.setAnimationModel(&sourceAnimation);
        const QVariantMap parent{{QStringLiteral("layerId"), QStringLiteral("scene-root")},
            {QStringLiteral("sourceId"), QString()}, {QStringLiteral("zIndex"), 0}, {QStringLiteral("visible"), true},
            {QStringLiteral("transform"), QVariantMap{{QStringLiteral("x"), 40.0}, {QStringLiteral("y"), 20.0}, {QStringLiteral("opacity"), 0.8}}}};
        const QVariantMap child{{QStringLiteral("layerId"), QStringLiteral("layer/box")},
            {QStringLiteral("parentLayerId"), QStringLiteral("scene-root")}, {QStringLiteral("sourceId"), QStringLiteral("asset:box-art")},
            {QStringLiteral("zIndex"), 4}, {QStringLiteral("visible"), true},
            {QStringLiteral("transform"), QVariantMap{{QStringLiteral("x"), 2.0}, {QStringLiteral("y"), 3.0}, {QStringLiteral("opacity"), 1.0}}},
            {QStringLiteral("animatedProperties"), QVariantMap{{QStringLiteral("x"), QStringLiteral("layer/box/x")},
                {QStringLiteral("opacity"), QStringLiteral("layer/box/opacity")}}},
            {QStringLiteral("effects"), QVariantList{QVariantMap{{QStringLiteral("effectId"), QStringLiteral("blur-soft")},
                {QStringLiteral("enabled"), false}, {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("radius"), 3.5}}}}}}};
        source.setLayers({parent, child});
        const QVariantMap initial = source.evaluate(middle);
        QVERIFY(initial.value(QStringLiteral("ok")).toBool());
        const QVariantMap before = initial.value(QStringLiteral("layers")).toList().at(1).toMap();
        QCOMPARE(before.value(QStringLiteral("matrix")).toList().at(4).toDouble(), 60.0);
        QCOMPARE(before.value(QStringLiteral("opacity")).toDouble(), 0.6);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl url = QUrl::fromLocalFile(directory.filePath(QStringLiteral("motion.qtmotion")));
        QVERIFY(source.saveToFile(url));
        const QString json = source.toJson();
        QVERIFY(json.contains(QStringLiteral("qindatk.motion-composition")));
        QVERIFY(json.contains(end));
        QVERIFY(json.contains(QStringLiteral("asset:box-art")));
        QVERIFY(!json.contains(QStringLiteral("imageBytes")));

        AutomationModel restoredAnimation;
        MotionComposition restored;
        restored.setAnimationModel(&restoredAnimation);
        const QVariantMap loaded = restored.loadFromFile(url);
        QVERIFY2(loaded.value(QStringLiteral("ok")).toBool(), qPrintable(loaded.value(QStringLiteral("error")).toString()));
        QCOMPARE(loaded.value(QStringLiteral("layerCount")).toInt(), 2);
        QCOMPARE(restored.layers().at(1).toMap().value(QStringLiteral("sourceId")).toString(), QStringLiteral("asset:box-art"));
        QCOMPARE(restored.layers().at(1).toMap().value(QStringLiteral("effects")).toList().at(0).toMap()
            .value(QStringLiteral("effectId")).toString(), QStringLiteral("blur-soft"));
        QCOMPARE(restoredAnimation.points(QStringLiteral("layer/box/x")).at(1).toMap()
            .value(QStringLiteral("ticks")).toString(), end);
        const QVariantMap after = restored.evaluate(middle);
        QVERIFY(after.value(QStringLiteral("ok")).toBool());
        const QVariantMap restoredLayer = after.value(QStringLiteral("layers")).toList().at(1).toMap();
        QCOMPARE(restoredLayer.value(QStringLiteral("matrix")).toList().at(4).toDouble(), 60.0);
        QCOMPARE(restoredLayer.value(QStringLiteral("opacity")).toDouble(), 0.6);

        const auto bad = restored.loadJson(QStringLiteral("{\"schema\":\"qindatk.motion-composition\",\"version\":99}"));
        QVERIFY(!bad.value(QStringLiteral("ok")).toBool());
        QCOMPARE(restored.layers().at(1).toMap().value(QStringLiteral("sourceId")).toString(), QStringLiteral("asset:box-art"));
    }
};

QTEST_GUILESS_MAIN(MotionPersistenceTest)
#include "tst_motion_persistence.moc"
