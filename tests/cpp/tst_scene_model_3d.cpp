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
    void constrainedTransformGesture() {
        SceneModel3D scene;
        const auto parent = scene.addEntity("Parent", "cube", {0, 0, 0}, QColor("#00aabb"));
        const auto child = scene.addEntity("Child", "sphere", {0, 0, 0}, QColor("#ff8822"));
        QVERIFY(scene.setTransform(parent, {0, 0, 0}, {0, 0, 90}, 2));
        QVERIFY(scene.reparent(child, parent, false));
        QVERIFY(scene.setTransform(child, {10, 0, 0}, {0, 0, 0}, 1));
        const auto original = scene.toJson();
        auto position = [&] { return scene.worldTransform(child)["position"].value<QVector3D>(); };
        auto rotation = [&] { return scene.worldTransform(child)["rotation"].value<QQuaternion>(); };
        QVERIFY((position() - QVector3D(0, 20, 0)).length() < .001f);
        QVERIFY(scene.beginTransform(child));
        QVERIFY(scene.previewConstrainedTranslation({7, 11, 3}, "x", "world", 5));
        QVERIFY((position() - QVector3D(5, 20, 0)).length() < .001f);
        QVERIFY(scene.previewConstrainedTranslation({7, 11, 3}, "x", "local", 5));
        QVERIFY((position() - QVector3D(0, 30, 0)).length() < .001f);
        QVERIFY(scene.previewAxisTranslation(7, "z", "local", 5));
        QVERIFY((position() - QVector3D(0, 20, 5)).length() < .001f);
        QVERIFY(scene.previewRotation(47, "z", "world", 15));
        QVERIFY((position() - QVector3D(0, 20, 0)).length() < .001f);
        QVERIFY((rotation().rotatedVector({1, 0, 0}) - QVector3D(-.7071068f, .7071068f, 0)).length() < .001f);
        QVERIFY(scene.previewRotation(90, "x", "local"));
        QVERIFY((rotation().rotatedVector({0, 1, 0}) - QVector3D(0, 0, 1)).length() < .001f);
        QVERIFY(scene.previewUniformScale(1.5f));
        QCOMPARE(scene.worldTransform(child)["scale"].toFloat(), 3.f);
        QCOMPARE(scene.toJson(), original); // no preview leaks to persistent state
        QVERIFY(scene.cancelTransform()); QCOMPARE(scene.toJson(), original);
        QVERIFY(scene.beginTransform(child));
        QVERIFY(scene.previewUniformScale(1.5f));
        QVERIFY(scene.commitTransform());
        QCOMPARE(scene.worldTransform(child)["scale"].toFloat(), 3.f);
        QVERIFY(scene.undo()); QCOMPARE(scene.toJson(), original);
        QVERIFY(scene.redo()); QCOMPARE(scene.worldTransform(child)["scale"].toFloat(), 3.f);
    }
};
QTEST_GUILESS_MAIN(SceneModel3DTest)
#include "tst_scene_model_3d.moc"
