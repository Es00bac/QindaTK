// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QGuiApplication>
#include <QScreen>
#include "detached_panel_host.h"
#include "dock_model.h"
#include "window_host.h"

using namespace QindaTK;

class tst_detached_panel_host : public QObject {
    Q_OBJECT
private slots:
    void nativeWindowKeepsContentAndPlacement();
};

void tst_detached_panel_host::nativeWindowKeepsContentAndPlacement()
{
    DockModel model;
    model.registerPanel(QStringLiteral("edit"), QStringLiteral("layers"),
                        {{QStringLiteral("title"), QStringLiteral("Layers")},
                         {QStringLiteral("zone"), QStringLiteral("right")}});
    model.setWorkspace(QStringLiteral("edit"));
    WindowHost windows;
    DetachedPanelHost detached;
    detached.setModel(&model);
    detached.setWindowHost(&windows);
    QQuickWindow mainWindow;
    mainWindow.show();
    QQuickItem home;
    home.setParentItem(mainWindow.contentItem());
    QQuickItem content;
    content.setObjectName(QStringLiteral("persistentEditor"));
    content.setParentItem(&home);
    QQuickItem focusTarget;
    focusTarget.setParentItem(&content);
    focusTarget.forceActiveFocus();
    QTRY_COMPARE(mainWindow.activeFocusItem(), &focusTarget);
    QVERIFY(detached.registerContent(QStringLiteral("layers"), &content));
    model.detachPanel(QStringLiteral("layers"), QRectF(100000, 100000, 320, 400));
    model.setDetachedScreen(QStringLiteral("layers"), QStringLiteral("missing-screen"));
    QVERIFY(detached.detach(QStringLiteral("layers")));
    QTRY_VERIFY(!detached.windowId(QStringLiteral("layers")).isEmpty());
    const QString firstId = detached.windowId(QStringLiteral("layers"));
    QQuickWindow *native = windows.window(firstId);
    QVERIFY(native);
    QCOMPARE(content.window(), native);
    QTRY_COMPARE(native->activeFocusItem(), &focusTarget);
    QCOMPARE(content.objectName(), QStringLiteral("persistentEditor"));
    QVERIFY(model.panel(QStringLiteral("layers")).value(QStringLiteral("detached")).toBool());
    QVERIFY(model.floatingPanels().isEmpty());
    QCOMPARE(model.detachedPanels().size(), 1);
    QVERIFY(QGuiApplication::primaryScreen()->availableGeometry().intersects(native->geometry()));
    QTRY_COMPARE(model.panel(QStringLiteral("layers"))
                     .value(QStringLiteral("detachedScreenId")).toString(),
                 QGuiApplication::primaryScreen()->name());

    const QString saved = model.serialize();
    DockModel restored;
    restored.registerPanel(QStringLiteral("edit"), QStringLiteral("layers"), {});
    restored.setWorkspace(QStringLiteral("edit"));
    QVERIFY(restored.deserialize(saved));
    QVERIFY(restored.panel(QStringLiteral("layers")).value(QStringLiteral("detached")).toBool());

    QVERIFY(detached.reattach(QStringLiteral("layers")));
    QCOMPARE(content.parentItem(), &home);
    QTRY_COMPARE(mainWindow.activeFocusItem(), &focusTarget);
    QVERIFY(detached.windowId(QStringLiteral("layers")).isEmpty());
    QCOMPARE(model.panel(QStringLiteral("layers")).value(QStringLiteral("mode")).toString(),
             QStringLiteral("docked"));
    QVERIFY(detached.detach(QStringLiteral("layers")));
    QTRY_VERIFY(!detached.windowId(QStringLiteral("layers")).isEmpty());
    QVERIFY(windows.closeWindow(detached.windowId(QStringLiteral("layers"))));
    QTRY_COMPARE(content.parentItem(), &home);
    QTRY_COMPARE(mainWindow.activeFocusItem(), &focusTarget);
    QCOMPARE(model.panel(QStringLiteral("layers")).value(QStringLiteral("mode")).toString(),
             QStringLiteral("docked"));
}

QTEST_MAIN(tst_detached_panel_host)
#include "tst_detached_panel_host.moc"
