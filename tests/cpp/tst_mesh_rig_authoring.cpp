// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-077 audit gaps: edit gestures as one undo step, rig animation through
// the WP-073 AutomationModel, rig authoring edits and the mesh DocumentCodec
// (save, autosave, crash recovery and reopen through DocumentManager).
#include "automation_model.h"
#include "document_manager.h"
#include "mesh_rig_document.h"
#include "undo_service.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
bool near(const QVector3D &a, const QVector3D &b, float tolerance = 1e-4f) { return (a - b).length() < tolerance; }
QVariantMap boneMap(const MeshRigDocument &doc, const QString &id) {
    for (const QVariant &row : doc.bones())
        if (row.toMap().value("boneId").toString() == id) return row.toMap();
    return {};
}
QStringList boneOrder(const MeshRigDocument &doc) {
    QStringList ids;
    for (const QVariant &row : doc.bones()) ids.append(row.toMap().value("boneId").toString());
    return ids;
}
}

class MeshRigAuthoringTest : public QObject {
    Q_OBJECT
private slots:
    void sliderGestureIsOneUndoStep();
    void boneAnimationUsesAutomationModel();
    void authoringEditsKeepIdsAndRefuseDanglingReferences();
    void sessionSaveRecoverAndReopen();
};

void MeshRigAuthoringTest::sliderGestureIsOneUndoStep()
{
    MeshRigDocument doc;
    const QString v = doc.addVertex({1, 0, 0});
    const QString bone = doc.addBone("Arm", {}, {0, 0, 0}, {1, 0, 0});
    QVERIFY(doc.setVertexWeights(v, {{bone, 1.0}}));
    // Twenty slider ticks inside one gesture: one step, earlier edits kept.
    QVERIFY(doc.beginEditGesture());
    QVERIFY(doc.editGestureActive());
    for (int degrees = 1; degrees <= 20; ++degrees) QVERIFY(doc.setBonePose(bone, {0, 0, float(degrees)}));
    QVERIFY(doc.endEditGesture());
    QVERIFY(!doc.endEditGesture()); // no gesture is open any more
    QCOMPARE(doc.undoService()->stepCount(&doc), 4);
    QVERIFY(doc.undo());
    QCOMPARE(doc.evaluatedPose(bone), QVector3D(0, 0, 0));
    QVERIFY(doc.undo()); // the weight edit is still there, not evicted
    QVERIFY(doc.vertices()[0].toMap().value("weights").toMap().isEmpty());
    QVERIFY(doc.redo()); QVERIFY(doc.redo());
    QCOMPARE(doc.evaluatedPose(bone), QVector3D(0, 0, 20));
    // Two gestures stay two steps; outside a gesture each edit is its own.
    QVERIFY(doc.beginEditGesture());
    QVERIFY(doc.setBonePose(bone, {0, 0, 21})); QVERIFY(doc.setBonePose(bone, {0, 0, 22}));
    QVERIFY(doc.endEditGesture());
    QVERIFY(doc.setBonePose(bone, {0, 0, 23}));
    QVERIFY(doc.setBonePose(bone, {0, 0, 24}));
    QCOMPARE(doc.undoService()->stepCount(&doc), 7);
    // Without gestures, 20 ticks against the 16-step history evict the start.
    MeshRigDocument ticks;
    const QString first = ticks.addBone("Arm", {}, {0, 0, 0}, {1, 0, 0});
    for (int degrees = 1; degrees <= 20; ++degrees) QVERIFY(ticks.setBonePose(first, {0, 0, float(degrees)}));
    QCOMPARE(ticks.undoService()->stepCount(&ticks), 16);
}

void MeshRigAuthoringTest::boneAnimationUsesAutomationModel()
{
    AutomationModel animation;
    MeshRigDocument doc;
    const QString v = doc.addVertex({1, 0, 0});
    const QString bone = doc.addBone("Arm", {}, {0, 0, 0}, {1, 0, 0});
    QVERIFY(doc.setVertexWeights(v, {{bone, 1.0}}));
    QVERIFY(doc.setPoseKey(bone, 0, {0, 0, 0}));
    QVERIFY(doc.setPoseKey(bone, 10, {0, 0, 40}));
    QVERIFY(!doc.bindBoneAnimation(bone)); // needs a model
    doc.setTicksPerFrame(QStringLiteral("100"));
    doc.setAnimationModel(&animation);
    QVERIFY(doc.bindBoneAnimation(bone));
    QVERIFY(doc.isBoneAnimationBound(bone));
    const QStringList lanes = doc.boneAnimationParameters(bone);
    for (const QString &lane : lanes) QVERIFY(animation.parameterIds().contains(lane));
    // Private keys were copied into exact-tick lanes (frame x ticksPerFrame).
    QCOMPARE(animation.points(lanes[2]).size(), 2);
    QCOMPARE(animation.valueAt(lanes[2], QStringLiteral("1000")), 40.0);
    // Evaluation follows the lanes at the playhead, not the private keys.
    doc.setTimeTicks(QStringLiteral("250"));
    QCOMPARE(doc.currentFrame(), 2);
    QVERIFY(near(doc.evaluatedPose(bone), {0, 0, 10}));
    QVERIFY(boneMap(doc, bone).value("animationBound").toBool());
    // A timeline edit (as KeyframeTimeline/DopeSheet make) moves the rig.
    const QString lastKey = animation.points(lanes[2]).last().toMap().value("id").toString();
    QVERIFY(animation.applyEdit({{"kind", "setPoint"}, {"parameterId", lanes[2]}, {"anchorPointId", lastKey}},
                                {{"value", 80.0}}).value("ok").toBool());
    QVERIFY(near(doc.evaluatedPose(bone), {0, 0, 20}));
    const QVector3D posed = doc.vertices()[0].toMap().value("posedPosition").value<QVector3D>();
    QVERIFY(near(posed, {float(std::cos(qDegreesToRadians(20.0))), float(std::sin(qDegreesToRadians(20.0))), 0}));
    // Posing a bound bone keys the lanes at the playhead: one automation step.
    const int before = animation.historyCount();
    QVERIFY(doc.setBonePose(bone, {0, 0, 30}));
    QCOMPARE(animation.historyCount(), before + 1);
    QCOMPARE(animation.points(lanes[2]).size(), 3);
    QVERIFY(near(doc.evaluatedPose(bone), {0, 0, 30}));
    // A slider gesture previews, then keys once at the end.
    QVERIFY(doc.beginEditGesture());
    for (int degrees = 31; degrees <= 45; ++degrees) QVERIFY(doc.setBonePose(bone, {0, 0, float(degrees)}));
    QVERIFY(near(doc.evaluatedPose(bone), {0, 0, 45}));
    QCOMPARE(animation.historyCount(), before + 1);
    QVERIFY(doc.endEditGesture());
    QCOMPARE(animation.historyCount(), before + 2);
    QCOMPARE(animation.valueAt(lanes[2], QStringLiteral("250")), 45.0);
    QVERIFY(animation.undo());
    QVERIFY(near(doc.evaluatedPose(bone), {0, 0, 30}));
    // Limits never clamp stored data: a limit that excludes a lane key is refused.
    QVERIFY(!doc.setBoneLimits(bone, {-180, -180, -10}, {180, 180, 50}));
    QVERIFY(doc.lastError().contains("timeline"));
    QVERIFY(!doc.setBonePose(bone, {0, 0, 200}));
    // Keys through the frame API reach the lanes too; removal is exact.
    QVERIFY(doc.setPoseKey(bone, 5, {0, 0, 12}));
    QCOMPARE(animation.valueAt(lanes[2], QStringLiteral("500")), 12.0);
    QVERIFY(doc.removePoseKey(bone, 5));
    QVERIFY(!doc.removePoseKey(bone, 5));
    // Unbinding (model gone) falls back to the untouched private keys.
    doc.setAnimationModel(nullptr);
    QVERIFY(!doc.isBoneAnimationBound(bone));
    QVERIFY(near(doc.evaluatedPose(bone), {0, 0, 8})); // private keys at frame 2
}

void MeshRigAuthoringTest::authoringEditsKeepIdsAndRefuseDanglingReferences()
{
    MeshRigDocument doc;
    const QString v = doc.addVertex({2, 0, 0});
    const QString root = doc.addBone("Root", {}, {0, 0, 0}, {1, 0, 0});
    const QString child = doc.addBone("Child", root, {1, 0, 0}, {2, 0, 0});
    const QString other = doc.addBone("Other", {}, {0, 1, 0}, {0, 2, 0});
    QVERIFY(doc.setVertexWeights(v, {{child, 1.0}}));
    // Rename and rest edits keep the stable ID.
    QVERIFY(doc.renameBone(child, "Forearm"));
    QCOMPARE(boneMap(doc, child).value("name").toString(), QStringLiteral("Forearm"));
    QVERIFY(!doc.renameBone(child, "  "));
    QVERIFY(doc.setBoneRest(child, {1, 0, 0}, {3, 0, 0}));
    QCOMPARE(boneMap(doc, child).value("tail").value<QVector3D>(), QVector3D(3, 0, 0));
    QVERIFY(!doc.setBoneRest(child, {1, 0, 0}, {1, 0, 0}));
    // Reparenting refuses cycles and keeps parents before children.
    QVERIFY(!doc.setBoneParent(root, child));
    QVERIFY(doc.lastError().contains("descendant"));
    QVERIFY(doc.setBoneParent(other, child));
    const QStringList order = boneOrder(doc);
    QVERIFY(order.indexOf(child) < order.indexOf(other));
    QVERIFY(doc.setBonePose(root, {0, 0, 90}));
    // other hangs off child, so the root's rotation carries it: (0,1,0) -> (-1,0,0).
    QVERIFY(near(boneMap(doc, other).value("posedHead").value<QVector3D>(), {-1, 0, 0}));
    // Removal is refused while anything refers to the bone.
    QVERIFY(!doc.removeBone(root));    // has a child
    QVERIFY(!doc.removeBone(child));   // weighted and has a child
    QVERIFY(doc.setBoneParent(other, {}));
    QVERIFY(!doc.removeBone(child));   // still weighted
    QVERIFY(doc.lastError().contains("weighted"));
    QVERIFY(!doc.addConstraint(other, "copyRotation", root, 1).isEmpty());
    QVERIFY(!doc.removeBone(root));
    QVERIFY(doc.setPoseKey(other, 3, {0, 0, 5}));
    QVERIFY(!doc.removeBone(other));   // animated
    QVERIFY(doc.removePoseKey(other, 3));
    QVERIFY(doc.removeBone(other));    // its own constraint goes with it
    QCOMPARE(doc.bones().size(), 2);
    QVERIFY(doc.undo());
    QCOMPARE(boneMap(doc, other).value("constraints").toList().size(), 1);
    QVERIFY(doc.redo());
    QVERIFY(boneMap(doc, other).isEmpty());
    // Authored rigs persist and reopen with the same IDs.
    MeshRigDocument reopened;
    QVERIFY(reopened.fromJson(doc.toJson()));
    QCOMPARE(boneMap(reopened, child).value("name").toString(), QStringLiteral("Forearm"));
    QCOMPARE(boneMap(reopened, child).value("tail").value<QVector3D>(), QVector3D(3, 0, 0));
}

void MeshRigAuthoringTest::sessionSaveRecoverAndReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("rig.qtkmesh");
    const QString recovery = dir.filePath("recovery");
    QString documentId, bone;
    QImage painted;
    {
        DocumentManager manager;
        manager.recoveryManager()->setDirectory(recovery);
        auto *session = manager.createDocument("qindatk.mesh-rig.v2");
        QVERIFY2(session, qPrintable(manager.lastError()));
        session->setAutosaveDelayMs(600000);
        auto *mesh = qobject_cast<MeshRigDocument *>(session->modelObject());
        QVERIFY(mesh);
        QCOMPARE(mesh->undoService(), manager.undoService());
        const QString a = mesh->addVertex({0, 0, 0}), b = mesh->addVertex({8, 0, 0}), c = mesh->addVertex({8, 8, 0});
        QVERIFY(!mesh->addFace(a, b, c).isEmpty());
        QVERIFY(mesh->resizeTexture(8, 8));
        bone = mesh->addBone("Root", {}, {0, 0, 0}, {1, 0, 0});
        QSignalSpy finished(session, &DocumentSession::saveFinished);
        QVERIFY(session->requestSaveAs(path));
        QVERIFY(finished.wait(10000));
        QVERIFY(finished.first().first().toBool());
        QVERIFY(!session->isDirty());
        QVERIFY(mesh->beginPaint());
        QVERIFY(mesh->paintRay({6, 1, 5}, {0, 0, -1}, QColor(255, 0, 0), 1));
        QVERIFY(!session->isDirty()); // an open stroke never enters the saved document
        QVERIFY(mesh->endPaint());
        QVERIFY(session->isDirty());
        painted = mesh->texture();
        documentId = session->documentId();
        QVERIFY(session->autosaveNow());
        QCOMPARE(manager.recoveryManager()->inspectId(documentId).value("schemaId").toString(),
                 QStringLiteral("qindatk.mesh-rig.v2"));
    }
    DocumentManager restarted;
    restarted.recoveryManager()->setDirectory(recovery);
    auto *restored = restarted.restoreRecovered(documentId, path);
    QVERIFY2(restored, qPrintable(restarted.lastError()));
    auto *mesh = qobject_cast<MeshRigDocument *>(restored->modelObject());
    QVERIFY(mesh);
    QCOMPARE(mesh->texture(), painted);
    QVERIFY(restored->recovered() && restored->isDirty());
    QSignalSpy saved(restored, &DocumentSession::saveFinished);
    QVERIFY(restored->requestSave());
    QVERIFY(saved.wait(10000));
    QVERIFY(saved.first().first().toBool());
    QVERIFY(restarted.closeDocument(documentId, DocumentManager::ClosePolicy::Discard));
    auto *reopened = restarted.openDocument(path);
    QVERIFY2(reopened, qPrintable(restarted.lastError()));
    QCOMPARE(reopened->documentId(), documentId);
    auto *opened = qobject_cast<MeshRigDocument *>(reopened->modelObject());
    QVERIFY(opened);
    QCOMPARE(opened->texture(), painted);
    QCOMPARE(boneMap(*opened, bone).value("name").toString(), QStringLiteral("Root"));
    QVERIFY(!opened->canUndo());
    // Plain saveToFile files open through the same codec.
    MeshRigDocument plain;
    plain.addBone("Plain", {}, {0, 0, 0}, {0, 1, 0});
    QVERIFY(plain.saveToFile(dir.filePath("plain.json")));
    auto *legacy = restarted.openDocument(dir.filePath("plain.json"));
    QVERIFY2(legacy, qPrintable(restarted.lastError()));
    auto *legacyMesh = qobject_cast<MeshRigDocument *>(legacy->modelObject());
    QVERIFY(legacyMesh);
    QCOMPARE(legacyMesh->bones().size(), 1);
}

QTEST_GUILESS_MAIN(MeshRigAuthoringTest)
#include "tst_mesh_rig_authoring.moc"
