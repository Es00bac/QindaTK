// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scene_model_3d.h"
#include "document_manager.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>
#include <QTemporaryDir>
using namespace QindaTK;
class SceneModel3DTest : public QObject {
    Q_OBJECT
private slots:
    void managedSessionSaveReopenAndRecover() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("scene.qtkscene");
        const QString recovery = dir.filePath("recovery");
        QString documentId;
        QString cube;
        {
            DocumentManager manager;
            manager.recoveryManager()->setDirectory(recovery);
            auto *session = manager.createDocument("qindatk.scene3d.v1");
            QVERIFY2(session, qPrintable(manager.lastError()));
            session->setAutosaveDelayMs(600000);
            auto *scene = qobject_cast<SceneModel3D *>(session->modelObject());
            QVERIFY(scene);
            QCOMPARE(scene->undoService(), manager.undoService());
            cube = scene->addEntity("Cube", "cube", {0, 0, 0}, QColor("#35bbc4"));
            QVERIFY(!cube.isEmpty());
            QVERIFY(scene->setMaterial(cube, QColor("#35bbc4"), .22f, .75f, QColor("#080d10")));
            QVERIFY(scene->canUndo());
            QVERIFY(manager.undoService()->undo());
            QCOMPARE(scene->material(cube).value("metalness").toFloat(), 0.f);
            QVERIFY(manager.undoService()->redo());
            QCOMPARE(scene->material(cube).value("metalness").toFloat(), .75f);
            QSignalSpy finished(session, &DocumentSession::saveFinished);
            QVERIFY(session->requestSaveAs(path));
            QVERIFY(finished.wait(10000));
            QVERIFY(finished.first().first().toBool());
            QVERIFY(!session->isDirty());
            QVERIFY(scene->beginTransform(cube));
            QVERIFY(scene->previewWorldTranslation({20, 0, 0}));
            QVERIFY(!session->isDirty()); // gesture previews never enter the saved document
            QVERIFY(scene->cancelTransform());
            documentId = session->documentId();
            QVERIFY(scene->setAppearance(cube, QColor("#ea8844"), true));
            QVERIFY(session->isDirty());
            QVERIFY(session->autosaveNow());
            QCOMPARE(manager.recoveryManager()->inspectId(documentId).value("schemaId").toString(),
                     QStringLiteral("qindatk.scene3d.v1"));
        }
        DocumentManager restarted;
        restarted.recoveryManager()->setDirectory(recovery);
        auto *restored = restarted.restoreRecovered(documentId, path);
        QVERIFY2(restored, qPrintable(restarted.lastError()));
        auto *scene = qobject_cast<SceneModel3D *>(restored->modelObject());
        QVERIFY(scene);
        QCOMPARE(scene->material(cube).value("baseColor").value<QColor>(), QColor("#ea8844"));
        QVERIFY(restored->recovered() && restored->isDirty());
        QSignalSpy saved(restored, &DocumentSession::saveFinished);
        QVERIFY(restored->requestSave());
        QVERIFY(saved.wait(10000));
        QVERIFY(saved.first().first().toBool());
        QVERIFY(!restored->isDirty());
        QVERIFY(restarted.closeDocument(documentId, DocumentManager::ClosePolicy::Discard));
        auto *reopened = restarted.openDocument(path);
        QVERIFY2(reopened, qPrintable(restarted.lastError()));
        QCOMPARE(reopened->documentId(), documentId);
        auto *openedScene = qobject_cast<SceneModel3D *>(reopened->modelObject());
        QVERIFY(openedScene);
        QCOMPARE(openedScene->material(cube).value("baseColor").value<QColor>(), QColor("#ea8844"));
        QVERIFY(openedScene->canUndo() == false);

        // Existing direct native files still open through the same codec.
        SceneModel3D legacy;
        const QString legacyId = legacy.addEntity("Old", "sphere", {0, 0, 0}, QColor("#4488cc"));
        const QString legacyPath = dir.filePath("old-scene.json");
        QVERIFY(legacy.saveToFile(legacyPath));
        auto *legacySession = restarted.openDocument(legacyPath);
        QVERIFY2(legacySession, qPrintable(restarted.lastError()));
        auto *oldScene = qobject_cast<SceneModel3D *>(legacySession->modelObject());
        QVERIFY(oldScene);
        QCOMPARE(oldScene->material(legacyId).value("baseColor").value<QColor>(), QColor("#4488cc"));
    }
    void materialEditsShadeSnapshotUndoAndRoundTrip() {
        SceneModel3D scene;
        const auto id = scene.addEntity("Sample", "sphere", {0, 0, 0}, QColor("#6699cc"));
        QVERIFY(!id.isEmpty());
        const QString baseline = scene.toJson();
        QVERIFY(scene.setMaterial(id, QColor("#d76a42"), 0.2f, 0.85f, QColor("#102030")));
        const auto material = scene.material(id);
        QCOMPARE(material.value("baseColor").value<QColor>(), QColor("#d76a42"));
        QCOMPARE(material.value("roughness").toFloat(), 0.2f);
        QCOMPARE(material.value("metalness").toFloat(), 0.85f);
        QCOMPARE(scene.entities().first().toMap().value("emissiveColor").value<QColor>(), QColor("#102030"));
        const QString saved = scene.toJson();
        QVERIFY(scene.undo()); QCOMPARE(scene.toJson(), baseline);
        QVERIFY(scene.redo()); QCOMPARE(scene.toJson(), saved);
        SceneModel3D reopened;
        QVERIFY(reopened.fromJson(saved));
        QCOMPARE(reopened.material(id), material);
        QVERIFY(!reopened.setMaterial(id, QColor("#ffffff"), 1.1f, 0, QColor("#000000")));
        QCOMPARE(reopened.toJson(), saved);

        auto legacy = QJsonDocument::fromJson(saved.toUtf8()).object();
        legacy["version"] = 1;
        auto entities = legacy["entities"].toArray();
        auto entity = entities.first().toObject();
        entity.remove("material"); entities[0] = entity; legacy["entities"] = entities;
        SceneModel3D oldFile;
        QVERIFY(oldFile.fromJson(QString::fromUtf8(QJsonDocument(legacy).toJson())));
        QCOMPARE(oldFile.material(id).value("roughness").toFloat(), 0.6f);
        QCOMPARE(oldFile.material(id).value("metalness").toFloat(), 0.f);
    }
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
