// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scene_model_3d.h"
#include <QtTest>
#include <QTemporaryDir>
using namespace QindaTK;
class SceneModel3DTest : public QObject {
    Q_OBJECT
private slots:
    void hierarchyManipulationAndPersistence() {
        SceneModel3D scene;
        const auto parent = scene.addEntity("Parent", "cube", {100, 0, 0}, QColor("#00aabb"));
        const auto child = scene.addEntity("Child", "sphere", {120, 20, 0}, QColor("#ff8822"));
        QVERIFY(!parent.isEmpty()); QVERIFY(!child.isEmpty());
        QVERIFY(scene.setTransform(parent, {100, 0, 0}, {0, 0, 90}, 2));
        const auto before = scene.worldTransform(child);
        QVERIFY(scene.reparent(child, parent, true));
        auto worldPosition = [&] { return scene.worldTransform(child)["position"].value<QVector3D>(); };
        QVERIFY((worldPosition() - before["position"].value<QVector3D>()).length() < .001f);
        QVERIFY(!scene.reparent(parent, child, false));
        const auto saved = scene.toJson();
        QVERIFY(scene.beginTransform(child));
        QVERIFY(scene.previewWorldTranslation({13, 9, 0}, 10));
        QVERIFY((worldPosition() - QVector3D(130, 30, 0)).length() < .001f);
        QCOMPARE(scene.toJson(), saved); // previews must not leak into persistence
        QVERIFY(scene.cancelTransform()); QCOMPARE(scene.toJson(), saved);
        QVERIFY(scene.beginTransform(child));
        QVERIFY(scene.previewWorldTranslation({20, 0, 0}));
        QVERIFY(scene.commitTransform());
        QVERIFY((worldPosition() - QVector3D(140, 20, 0)).length() < .001f);
        QVERIFY(scene.undo()); QCOMPARE(scene.toJson(), saved);
        QVERIFY(scene.redo());
        QTemporaryDir dir;
        QVERIFY(scene.saveToFile(dir.filePath("scene.json")));
        SceneModel3D reopened;
        QVERIFY(reopened.loadFromFile(dir.filePath("scene.json")));
        QCOMPARE(reopened.toJson(), scene.toJson());
        const auto stable = reopened.toJson();
        QVERIFY(!reopened.fromJson("{}")); QCOMPARE(reopened.toJson(), stable);
        QVERIFY(scene.reparent(child, "", true));
        QVERIFY((worldPosition() - QVector3D(140, 20, 0)).length() < .001f);
        QVERIFY(scene.undo());
        QVERIFY(scene.setAppearance(parent, QColor("#00aabb"), false));
        QVERIFY(!scene.entities()[1].toMap()["visible"].toBool());
    }
};
QTEST_GUILESS_MAIN(SceneModel3DTest)
#include "tst_scene_model_3d.moc"
