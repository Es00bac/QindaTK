// SPDX-License-Identifier: LGPL-3.0-or-later
// Real Qt Quick 3D lifecycle of SceneView3D: viewport resizing, two views in
// two windows over one SceneModel3D and one posed MeshRigDocument, and a
// genuine scene-graph invalidation (a hidden window without persistent
// graphics on the threaded render loop: the same release/re-initialize path
// Qt takes after a graphics device loss).
// Needs a display and a Qt RHI backend, for example
//   QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl tst_scene_view_3d_render
// The headless ctest run (offscreen, software adaptation) skips it.
#include "mesh_rig_document.h"
#include "scene_model_3d.h"

#include <QGuiApplication>
#include <QJSValue>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <atomic>
#include <memory>

using namespace QindaTK;

namespace {

struct Host {
    // Counters outlive the window: its destructor still reports invalidation.
    std::atomic_int invalidated{0};
    std::atomic_int initialized{0};
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> view;
};

const QString meshId = QStringLiteral("__mesh_document__");

std::unique_ptr<Host> makeHost(SceneModel3D *scene, MeshRigDocument *mesh, QSize size, bool persistent)
{
    auto host = std::make_unique<Host>();
    host->engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    QQmlComponent component(&host->engine);
    component.setData(R"(
import QtQuick
import QindaTK as Tk
Tk.SceneView3D {
    cameraPosition: Qt.vector3d(0, 0, 600)
    cameraEulerDegrees: Qt.vector3d(0, 0, 0)
}
)", QUrl());
    QObject *object = component.createWithInitialProperties(
        {{"scene", QVariant::fromValue(scene)}, {"meshDocument", QVariant::fromValue(mesh)}});
    if (!object) {
        qWarning() << component.errors();
        return nullptr;
    }
    host->view.reset(qobject_cast<QQuickItem *>(object));
    Host *raw = host.get();
    QObject::connect(&host->window, &QQuickWindow::sceneGraphInvalidated, &host->window,
                     [raw] { ++raw->invalidated; }, Qt::DirectConnection);
    QObject::connect(&host->window, &QQuickWindow::sceneGraphInitialized, &host->window,
                     [raw] { ++raw->initialized; }, Qt::DirectConnection);
    host->window.setPersistentGraphics(persistent);
    host->window.setPersistentSceneGraph(persistent);
    host->window.resize(size);
    host->view->setParentItem(host->window.contentItem());
    host->view->setSize(size);
    host->window.show();
    return host;
}

QVariantMap toMap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>())
        return value.value<QJSValue>().toVariant().toMap();
    return value.toMap();
}

QString pick(QQuickItem *view, QPointF point)
{
    QVariant result;
    QMetaObject::invokeMethod(view, "pickEntity", Q_RETURN_ARG(QVariant, result),
                              Q_ARG(QVariant, point.x()), Q_ARG(QVariant, point.y()));
    return toMap(result).value(QStringLiteral("entityId")).toString();
}

QPointF project(QQuickItem *view, QVector3D position)
{
    QVariant result;
    QMetaObject::invokeMethod(view, "projectWorldPosition", Q_RETURN_ARG(QVariant, result),
                              Q_ARG(QVariant, QVariant::fromValue(position)));
    const QVector3D mapped = result.value<QVector3D>();
    return QPointF(mapped.x(), mapped.y());
}

int generation(const Host &host)
{
    return host.view->property("renderGeneration").toInt();
}

QVector3D worldPosition(const SceneModel3D &scene, const QString &id)
{
    return scene.worldTransform(id).value(QStringLiteral("position")).value<QVector3D>();
}

} // namespace

class TstSceneView3DRender : public QObject {
    Q_OBJECT
public:
    static void initMain()
    {
        // Only the threaded loop releases a hidden non-persistent window's scene graph.
        if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP"))
            qputenv("QSG_RENDER_LOOP", "threaded");
    }
private slots:
    void initTestCase();
    void resizeMultiViewAndSceneGraphLoss();
};

void TstSceneView3DRender::initTestCase()
{
    if (QGuiApplication::platformName() == QLatin1String("offscreen")
        || QQuickWindow::sceneGraphBackend() == QLatin1String("software"))
        QSKIP("Needs a display and a Qt RHI backend (xcb or wayland with QT_QUICK_BACKEND=rhi)");
}

void TstSceneView3DRender::resizeMultiViewAndSceneGraphLoss()
{
    SceneModel3D scene;
    const QString id = scene.addEntity(QStringLiteral("Cube"), QStringLiteral("cube"),
                                       QVector3D(0, 0, 0), QColor(QStringLiteral("#35bbc4")));
    QVERIFY(!id.isEmpty());
    // A posed mesh quad left of the cube: toolkit-provided geometry in every view.
    MeshRigDocument mesh;
    const QString a = mesh.addVertex(QVector3D(-260, -50, 0));
    const QString b = mesh.addVertex(QVector3D(-160, -50, 0));
    const QString c = mesh.addVertex(QVector3D(-160, 50, 0));
    const QString d = mesh.addVertex(QVector3D(-260, 50, 0));
    QVERIFY(!mesh.addFace(a, b, c).isEmpty() && !mesh.addFace(a, c, d).isEmpty());
    const QVector3D meshPoint(-210, 0, 0);
    auto first = makeHost(&scene, &mesh, QSize(640, 480), false);
    auto second = makeHost(&scene, &mesh, QSize(480, 360), true);
    QVERIFY(first && second);
    QVERIFY(QTest::qWaitForWindowExposed(&first->window));
    QVERIFY(QTest::qWaitForWindowExposed(&second->window));
    QTRY_VERIFY_WITH_TIMEOUT(first->view->property("rendererAvailable").toBool(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(pick(first->view.get(), QPointF(320, 240)), id, 10000);
    QTRY_COMPARE_WITH_TIMEOUT(pick(second->view.get(), QPointF(240, 180)), id, 10000);
    for (Host *host : {first.get(), second.get()})
        QTRY_COMPARE(pick(host->view.get(), project(host->view.get(), meshPoint)), meshId);

    // Viewport sizing: projection and GPU picking follow a resize.
    first->window.resize(400, 300);
    first->view->setSize(QSizeF(400, 300));
    QTRY_VERIFY(qAbs(project(first->view.get(), QVector3D(0, 0, 0)).x() - 200) < 0.5);
    QTRY_COMPARE(pick(first->view.get(), QPointF(200, 150)), id);

    // Two views, one model: a committed edit reaches both GPU scenes.
    QVERIFY(scene.setTransform(id, QVector3D(120, 0, 0), QVector3D(), 1));
    for (Host *host : {first.get(), second.get()})
        QTRY_COMPARE(pick(host->view.get(), project(host->view.get(), QVector3D(120, 0, 0))), id);

    const int firstGeneration = generation(*first);
    const int secondGeneration = generation(*second);
    QPointer<QObject> firstRenderer = first->view->findChild<QObject *>(QStringLiteral("sceneRenderer3D"));
    QPointer<QObject> secondRenderer = second->view->findChild<QObject *>(QStringLiteral("sceneRenderer3D"));
    QVERIFY(firstRenderer && secondRenderer);

    // Begin a drag in the first view, then lose that window's scene graph.
    const QPoint grab = project(first->view.get(), QVector3D(120, 0, 0)).toPoint();
    QTest::mousePress(&first->window, Qt::LeftButton, {}, grab);
    QTest::mouseMove(&first->window, grab + QPoint(40, 0));
    QVERIFY(scene.editing());
    first->window.hide();
    QTRY_VERIFY(first->invalidated.load() > 0);
    QTRY_VERIFY(firstRenderer.isNull());
    QTRY_VERIFY(generation(*first) > firstGeneration);
    QVERIFY(!scene.editing());
    QCOMPARE(worldPosition(scene, id), QVector3D(120, 0, 0));
    QVERIFY(scene.canUndo());

    // The other window keeps its device, renderer and picking.
    QCOMPARE(second->invalidated.load(), 0);
    QCOMPARE(generation(*second), secondGeneration);
    QCOMPARE(second->view->findChild<QObject *>(QStringLiteral("sceneRenderer3D")), secondRenderer.data());
    QTRY_COMPARE(pick(second->view.get(), project(second->view.get(), QVector3D(120, 0, 0))), id);
    QTRY_COMPARE(pick(second->view.get(), project(second->view.get(), meshPoint)), meshId);

    // Recovery: a new device initializes and the rebuilt renderer draws and picks the model.
    const int initialized = first->initialized.load();
    first->window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&first->window));
    QTRY_VERIFY(first->initialized.load() > initialized);
    const QPointF entity = project(first->view.get(), QVector3D(120, 0, 0));
    QTRY_COMPARE(pick(first->view.get(), entity), id);
    QTRY_COMPARE(pick(first->view.get(), project(first->view.get(), meshPoint)), meshId);
    QTest::mouseRelease(&first->window, Qt::LeftButton, {}, grab);
    const QImage frame = first->window.grabWindow();
    const qreal dpr = frame.devicePixelRatio();
    QVERIFY(!frame.isNull());
    const QColor object = frame.pixelColor(int(entity.x() * dpr), int(entity.y() * dpr));
    const QColor background = frame.pixelColor(int((first->view->width() - 8) * dpr),
                                               int((first->view->height() - 8) * dpr));
    QVERIFY2(object != background, qPrintable(object.name() + QLatin1Char(' ') + background.name()));

    // Manipulation works again and is one undo step.
    const QPoint start = entity.toPoint();
    QTest::mousePress(&first->window, Qt::LeftButton, {}, start);
    QTest::mouseMove(&first->window, start + QPoint(30, 0));
    QTest::mouseRelease(&first->window, Qt::LeftButton, {}, start + QPoint(30, 0));
    QVERIFY(worldPosition(scene, id).x() > 125);
    QVERIFY(scene.undo());
    QCOMPARE(worldPosition(scene, id), QVector3D(120, 0, 0));

    // Closing the second window leaves the first view's resources intact.
    second.reset();
    QTRY_COMPARE(pick(first->view.get(), project(first->view.get(), QVector3D(120, 0, 0))), id);
}

QTEST_MAIN(TstSceneView3DRender)
#include "tst_scene_view_3d_render.moc"
