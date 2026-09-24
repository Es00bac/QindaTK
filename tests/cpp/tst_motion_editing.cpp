// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-073: layer time offsets/remaps, path and tangent edits in transformed
// parent space, effect insertion history and atomic multi-curve batches.
#include "../../src/core/automation_model.h"
#include "../../src/core/motion_composition.h"
#include <QPointF>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantMap key(const QString &ticks, double value)
{
    return {{QStringLiteral("ticks"), ticks}, {QStringLiteral("value"), value},
            {QStringLiteral("interpolation"), QStringLiteral("linear")}};
}

bool addCurve(AutomationModel &model, const QString &id, const QVariantList &keys)
{
    const QVariantMap added = model.addParameterMap({{QStringLiteral("id"), id}, {QStringLiteral("name"), id},
        {QStringLiteral("dimension"), QStringLiteral("length")}, {QStringLiteral("unitId"), QStringLiteral("px")},
        {QStringLiteral("minimum"), -1000.0}, {QStringLiteral("maximum"), 1000.0},
        {QStringLiteral("default"), 0.0}, {QStringLiteral("interpolation"), QStringLiteral("linear")}});
    if (!added.value(QStringLiteral("ok")).toBool()) return false;
    for (const QVariant &entry : keys) {
        const QVariantMap result = model.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                                                    {QStringLiteral("parameterId"), id}}, entry.toMap());
        if (!result.value(QStringLiteral("ok")).toBool()) return false;
    }
    return true;
}

QVariantMap layerIn(const QVariantMap &evaluated, const QString &id)
{
    for (const QVariant &row : evaluated.value(QStringLiteral("layers")).toList())
        if (row.toMap().value(QStringLiteral("layerId")).toString() == id) return row.toMap();
    return {};
}

QPointF origin(MotionComposition &composition, const QString &ticks, const QString &id)
{
    const QVariantList m = layerIn(composition.evaluate(ticks), id).value(QStringLiteral("matrix")).toList();
    return m.size() == 6 ? QPointF(m.at(4).toDouble(), m.at(5).toDouble()) : QPointF(qQNaN(), qQNaN());
}

QPointF point(const QVariant &value)
{
    const QVariantMap map = value.toMap();
    return {map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble()};
}

bool near(const QPointF &a, const QPointF &b, double tolerance = 1e-9)
{
    return qAbs(a.x() - b.x()) <= tolerance && qAbs(a.y() - b.y()) <= tolerance;
}

double valueAtKey(AutomationModel &model, const QString &curve, int index)
{
    return model.points(curve).at(index).toMap().value(QStringLiteral("value")).toDouble();
}

// Parent: rotation 90 degrees, scale 2, translation (300, 200), so a child at
// parent-space (x, y) sits at world (300 - 2y, 200 + 2x). The child starts
// 1000 ticks late; keys at layer ticks 0/100/200 are x 0/50/100, y 0/20/0.
struct PathScene {
    AutomationModel model;
    MotionComposition composition;
    bool ready = false;
    PathScene()
    {
        ready = addCurve(model, QStringLiteral("child/x"), {key(QStringLiteral("0"), 0), key(QStringLiteral("100"), 50),
                                                          key(QStringLiteral("200"), 100)})
            && addCurve(model, QStringLiteral("child/y"), {key(QStringLiteral("0"), 0), key(QStringLiteral("100"), 20),
                                                         key(QStringLiteral("200"), 0)});
        model.clearHistory();
        composition.setAnimationModel(&model);
        composition.setLayers({
            QVariantMap{{QStringLiteral("layerId"), QStringLiteral("parent")},
                {QStringLiteral("transform"), QVariantMap{{QStringLiteral("x"), 300.0}, {QStringLiteral("y"), 200.0},
                    {QStringLiteral("rotation"), 90.0}, {QStringLiteral("scaleX"), 2.0}, {QStringLiteral("scaleY"), 2.0}}}},
            QVariantMap{{QStringLiteral("layerId"), QStringLiteral("child")},
                {QStringLiteral("parentLayerId"), QStringLiteral("parent")}, {QStringLiteral("zIndex"), 1},
                {QStringLiteral("timeOffsetTicks"), QStringLiteral("1000")},
                {QStringLiteral("animatedProperties"), QVariantMap{{QStringLiteral("x"), QStringLiteral("child/x")},
                                                                   {QStringLiteral("y"), QStringLiteral("child/y")}}}}});
    }
};
} // namespace

class MotionEditingTest final : public QObject {
    Q_OBJECT
private slots:
    void layerOffsetsAndRemapsSampleExactLayerTime()
    {
        AutomationModel model;
        QVERIFY(addCurve(model, QStringLiteral("x"), {key(QStringLiteral("0"), 0), key(QStringLiteral("1000"), 1000)}));
        MotionComposition composition;
        composition.setAnimationModel(&model);
        const auto layer = [](const QString &id, const QVariantMap &timing) {
            QVariantMap record{{QStringLiteral("layerId"), id},
                {QStringLiteral("animatedProperties"), QVariantMap{{QStringLiteral("x"), QStringLiteral("x")}}}};
            record.insert(timing);
            return record;
        };
        const auto pair = [](const char *comp, const char *local) {
            return QVariantMap{{QStringLiteral("compTicks"), QString::fromLatin1(comp)},
                               {QStringLiteral("layerTicks"), QString::fromLatin1(local)}};
        };
        composition.setLayers({
            layer(QStringLiteral("offset"), {{QStringLiteral("timeOffsetTicks"), QStringLiteral("250")}}),
            // 2^53 + 1 is not a double: only exact integer time lands on layer tick 500.
            layer(QStringLiteral("late"), {{QStringLiteral("timeOffsetTicks"), QStringLiteral("9007199254740993")}}),
            layer(QStringLiteral("remap"), {{QStringLiteral("timeRemap"), QVariantList{pair("0", "0"), pair("100", "500"),
                                                                                     pair("200", "500"), pair("300", "0")}}}),
            layer(QStringLiteral("floor"), {{QStringLiteral("timeRemap"), QVariantList{pair("0", "1"), pair("3", "0")}}})});
        const auto check = [&](const char *ticks, const QString &id, const char *local, double x) {
            const QVariantMap row = layerIn(composition.evaluate(QString::fromLatin1(ticks)), id);
            return row.value(QStringLiteral("localTicks")).toString() == QString::fromLatin1(local)
                && qAbs(row.value(QStringLiteral("matrix")).toList().value(4).toDouble() - x) < 1e-9;
        };
        QVERIFY(check("600", QStringLiteral("offset"), "350", 350));
        QVERIFY(check("0", QStringLiteral("offset"), "-250", 0)); // held before the first key
        QVERIFY(check("9007199254741493", QStringLiteral("late"), "500", 500));
        QVERIFY(check("50", QStringLiteral("remap"), "250", 250));
        QVERIFY(check("150", QStringLiteral("remap"), "500", 500));  // freeze
        QVERIFY(check("201", QStringLiteral("remap"), "495", 495));  // reverse
        QVERIFY(check("-5", QStringLiteral("remap"), "0", 0));       // held before the first pair
        QVERIFY(check("900", QStringLiteral("remap"), "0", 0));      // held after the last pair
        QVERIFY(check("1", QStringLiteral("floor"), "0", 0));        // 1 - 1/3 floors to 0

        composition.setLayers({layer(QStringLiteral("bad"), {{QStringLiteral("timeRemap"), QVariantList{
            pair("10", "0"), pair("10", "5")}}})});
        QVERIFY(!composition.evaluate(QStringLiteral("0")).value(QStringLiteral("ok")).toBool());
        QVERIFY(composition.lastError().contains(QStringLiteral("strictly increasing")));
        composition.setLayers({layer(QStringLiteral("bad"), {{QStringLiteral("timeOffsetTicks"), 5}})});
        QVERIFY(!composition.evaluate(QStringLiteral("0")).value(QStringLiteral("ok")).toBool());
    }

    void pathKeyEditsLandInWorldSpaceAsOneUndoStep()
    {
        PathScene scene;
        QVERIFY(scene.ready);
        QVERIFY(near(origin(scene.composition, QStringLiteral("1100"), QStringLiteral("child")), {260, 300}));
        // World (100, 260) is parent-space (30, 100) at layer tick 100.
        const QVariantMap moved = scene.composition.movePathKey(QStringLiteral("child"), QStringLiteral("1100"), 100, 260);
        QVERIFY2(moved.value(QStringLiteral("ok")).toBool(), qPrintable(moved.value(QStringLiteral("error")).toString()));
        QCOMPARE(moved.value(QStringLiteral("localTicks")).toString(), QStringLiteral("100"));
        QCOMPARE(scene.model.historyCount(), 1);
        QVERIFY(near(origin(scene.composition, QStringLiteral("1100"), QStringLiteral("child")), {100, 260}));
        QVERIFY(qAbs(valueAtKey(scene.model, QStringLiteral("child/x"), 1) - 30) < 1e-9);
        QVERIFY(qAbs(valueAtKey(scene.model, QStringLiteral("child/y"), 1) - 100) < 1e-9);

        QVERIFY(scene.model.undo());
        QCOMPARE(valueAtKey(scene.model, QStringLiteral("child/x"), 1), 50.0);
        QCOMPARE(valueAtKey(scene.model, QStringLiteral("child/y"), 1), 20.0);
        QVERIFY(near(origin(scene.composition, QStringLiteral("1100"), QStringLiteral("child")), {260, 300}));
        QVERIFY(scene.model.redo());
        QVERIFY(near(origin(scene.composition, QStringLiteral("1100"), QStringLiteral("child")), {100, 260}));

        // No key at layer tick 50: both curves gain one, still one step.
        const QVariantMap added = scene.composition.movePathKey(QStringLiteral("child"), QStringLiteral("1050"), 280, 240);
        QVERIFY(added.value(QStringLiteral("ok")).toBool());
        QCOMPARE(added.value(QStringLiteral("createdIds")).toStringList().size(), 2);
        QCOMPARE(scene.model.historyCount(), 2);
        QVERIFY(near(origin(scene.composition, QStringLiteral("1050"), QStringLiteral("child")), {280, 240}));
        QVERIFY(scene.model.undo());
        QCOMPARE(scene.model.points(QStringLiteral("child/x")).size(), 3);
        QCOMPARE(scene.model.points(QStringLiteral("child/y")).size(), 3);

        QVERIFY(!scene.composition.movePathKey(QStringLiteral("parent"), QStringLiteral("1100"), 0, 0)
                     .value(QStringLiteral("ok")).toBool());
        QVERIFY(!scene.composition.movePathKey(QStringLiteral("child"), QStringLiteral("1100"), qInf(), 0)
                     .value(QStringLiteral("ok")).toBool());
    }

    void pathTangentsShapeSampledSegmentsAndUndo()
    {
        PathScene scene;
        QVERIFY(scene.ready);
        // Unset handles sit 1/3 and 2/3 along each segment.
        const QVariantMap handles = scene.composition.pathHandles(QStringLiteral("child"), QStringLiteral("1100"));
        QVERIFY(handles.value(QStringLiteral("ok")).toBool());
        QVERIFY(near(point(handles.value(QStringLiteral("position"))), {260, 300}));
        QVERIFY(near(point(handles.value(QStringLiteral("outHandle"))), {300 - 2 * 40.0 / 3, 200 + 2 * 200.0 / 3}));
        QVERIFY(near(point(handles.value(QStringLiteral("inHandle"))), {300 - 2 * 40.0 / 3, 200 + 2 * 100.0 / 3}));
        QVERIFY(near(origin(scene.composition, QStringLiteral("1150"), QStringLiteral("child")), {280, 350}));

        // Out control at parent-space (90, 40) is world (220, 380).
        const QVariantMap edited = scene.composition.setPathTangent(QStringLiteral("child"), QStringLiteral("1100"),
                                                                    QStringLiteral("out"), 220, 380);
        QVERIFY2(edited.value(QStringLiteral("ok")).toBool(), qPrintable(edited.value(QStringLiteral("error")).toString()));
        QCOMPARE(scene.model.historyCount(), 1);
        QVERIFY(near(point(scene.composition.pathHandles(QStringLiteral("child"), QStringLiteral("1100"))
                               .value(QStringLiteral("outHandle"))), {220, 380}, 1e-6));
        // Segment midpoint = (P0 + 3 C1 + 3 C2 + P3) / 8 per axis:
        // x (50 + 270 + 250 + 100) / 8 = 83.75, y (20 + 120 + 20 + 0) / 8 = 20.
        QVERIFY(near(origin(scene.composition, QStringLiteral("1150"), QStringLiteral("child")),
                     {300 - 2 * 20.0, 200 + 2 * 83.75}, 1e-6));
        QVERIFY(scene.model.undo());
        QVERIFY(near(origin(scene.composition, QStringLiteral("1150"), QStringLiteral("child")), {280, 350}));

        const int steps = scene.model.historyCount();
        QVERIFY(!scene.composition.setPathTangent(QStringLiteral("child"), QStringLiteral("1200"), QStringLiteral("out"),
                                                  0, 0).value(QStringLiteral("ok")).toBool());
        QVERIFY(!scene.composition.setPathTangent(QStringLiteral("child"), QStringLiteral("1100"), QStringLiteral("up"),
                                                  0, 0).value(QStringLiteral("ok")).toBool());
        QCOMPARE(scene.model.historyCount(), steps);
    }

    void effectInsertionIsOneUndoableLayerTransaction()
    {
        MotionComposition composition;
        composition.setLayers({QVariantMap{{QStringLiteral("layerId"), QStringLiteral("art")},
                                           {QStringLiteral("sourceId"), QStringLiteral("art-source")}}});
        const auto effectIds = [&] {
            QStringList ids;
            for (const QVariant &effect : composition.layers().at(0).toMap().value(QStringLiteral("effects")).toList())
                ids.push_back(effect.toMap().value(QStringLiteral("effectId")).toString());
            return ids;
        };
        const QString start = composition.revisionText();
        QVERIFY(composition.insertEffect(QStringLiteral("art"), {{QStringLiteral("effectId"), QStringLiteral("grayscale")}},
                                         -1, start).value(QStringLiteral("ok")).toBool());
        QVERIFY(composition.canUndo());
        QVERIFY(!composition.insertEffect(QStringLiteral("art"), {{QStringLiteral("effectId"), QStringLiteral("brightness")}},
                                          -1, start).value(QStringLiteral("ok")).toBool());
        QVERIFY(composition.lastError().contains(QStringLiteral("changed")));
        QVERIFY(!composition.insertEffect(QStringLiteral("art"), {{QStringLiteral("effectId"), QStringLiteral("grayscale")}})
                     .value(QStringLiteral("ok")).toBool());
        QVERIFY(!composition.insertEffect(QStringLiteral("missing"), {{QStringLiteral("effectId"), QStringLiteral("x")}})
                     .value(QStringLiteral("ok")).toBool());
        QVERIFY(composition.insertEffect(QStringLiteral("art"), {{QStringLiteral("effectId"), QStringLiteral("brightness")},
            {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("amount"), 0.2}}}}, 0).value(QStringLiteral("ok")).toBool());
        QCOMPARE(effectIds(), QStringList({QStringLiteral("brightness"), QStringLiteral("grayscale")}));
        QVERIFY(composition.undo());
        QCOMPARE(effectIds(), QStringList({QStringLiteral("grayscale")}));
        QVERIFY(composition.undo());
        QVERIFY(effectIds().isEmpty());
        QVERIFY(!composition.canUndo());
        QVERIFY(composition.redo());
        QCOMPARE(effectIds(), QStringList({QStringLiteral("grayscale")}));
        composition.setLayers({});
        QVERIFY(!composition.canUndo());
        QVERIFY(!composition.canRedo());
    }

    void batchesAreAtomicAndEditEachCurveOnce()
    {
        PathScene scene;
        QVERIFY(scene.ready);
        const auto setValue = [&](const QString &curve, double value) {
            AutomationBatchEdit edit;
            edit.request.kind = AutomationEditKind::SetPoint;
            edit.request.parameterId = curve;
            edit.request.anchorPointId = QUuid(scene.model.points(curve).at(0).toMap().value(QStringLiteral("id")).toString());
            edit.input.value = value;
            return edit;
        };
        QVERIFY(scene.model.applyBatch({setValue(QStringLiteral("child/x"), 5), setValue(QStringLiteral("child/y"), 5000)},
                                       QStringLiteral("Out of range")));
        QCOMPARE(valueAtKey(scene.model, QStringLiteral("child/x"), 0), 0.0);
        QVERIFY(scene.model.applyBatch({setValue(QStringLiteral("child/x"), 5), setValue(QStringLiteral("child/x"), 6)},
                                       QStringLiteral("Twice")));
        QCOMPARE(scene.model.historyCount(), 0);
        QString transaction;
        QVERIFY(!scene.model.applyBatch({setValue(QStringLiteral("child/x"), 5), setValue(QStringLiteral("child/y"), 6)},
                                        QStringLiteral("Both"), &transaction));
        QVERIFY(!transaction.isEmpty());
        QCOMPARE(scene.model.historyCount(), 1);
        QCOMPARE(scene.model.undoText(), QStringLiteral("Both"));
    }
};

QTEST_GUILESS_MAIN(MotionEditingTest)
#include "tst_motion_editing.moc"
