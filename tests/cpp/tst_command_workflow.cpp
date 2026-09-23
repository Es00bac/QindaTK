// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_context.h"
#include "command_registry.h"
#include "shortcut_map.h"

#include <QAccessible>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace QindaTK;

class TstCommandWorkflow final : public QObject {
    Q_OBJECT
private:
    static QObject *named(QObject *root, const char *name)
    {
        return root->findChild<QObject *>(QString::fromLatin1(name));
    }

private slots:
    void presentationsShareOneContextAwareInvocationPath()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        ShortcutMap shortcuts;
        shortcuts.setRegistry(&registry);

        QObject applicationOwner;
        QObject documentAOwner;
        QObject documentBOwner;
        const QString app = context.pushScope(QStringLiteral("application"), &applicationOwner);
        const QString documentA = context.pushScope(QStringLiteral("document-a"), &documentAOwner, app);
        const QString documentB = context.pushScope(QStringLiteral("document-b"), &documentBOwner, app);
        QVERIFY(context.activate(documentA));

        constexpr auto commandId = "document.commit";
        QVERIFY(registry.registerCommand({QString::fromLatin1(commandId),
            QStringLiteral("Commit changes"), QStringLiteral("check"),
            QStringLiteral("Document"), QStringLiteral("save apply"),
            QStringLiteral("Ctrl+Return"), true}));
        QVERIFY(shortcuts.bindCommand(QString::fromLatin1(commandId)));

        int callsA = 0;
        int callsB = 0;
        bool enabled = true;
        bool checked = false;
        bool operationIdCorrect = true;
        const auto handler = [&operationIdCorrect](int *calls) {
            return [calls, &operationIdCorrect](const CommandInvocationContext &invocation,
                                                const QVariantMap &) {
                ++*calls;
                operationIdCorrect = operationIdCorrect
                    && invocation.request.operationId == QStringLiteral("document.commit");
                return OperationError{};
            };
        };
        const auto state = [&enabled, &checked](const CommandInvocationContext &) {
            return CommandState{enabled, checked, true,
                                enabled ? QString{} : QStringLiteral("Document is read only")};
        };
        QVERIFY(registry.registerHandler(documentA, QString::fromLatin1(commandId),
                                         &documentAOwner, handler(&callsA), state));
        QVERIFY(registry.registerHandler(documentB, QString::fromLatin1(commandId),
                                         &documentBOwner, handler(&callsB), state));

        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("commandRegistryObject"), &registry);
        engine.rootContext()->setContextProperty(QStringLiteral("shortcutMapObject"), &shortcuts);
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QtQuick.Window
import QindaTK as Tk
Window {
    width: 640; height: 420; visible: true
    Item {
        anchors.fill: parent
        Tk.MenuItem {
            objectName: "workflowMenuItem"
            commandRegistry: commandRegistryObject
            shortcutMap: shortcutMapObject
            commandId: "document.commit"
        }
        Tk.CommandButton {
            objectName: "workflowToolbarButton"
            x: 120
            registry: commandRegistryObject
            shortcutMap: shortcutMapObject
            commandId: "document.commit"
        }
        Tk.CommandPalette {
            objectName: "workflowPalette"
            registry: commandRegistryObject
            shortcutMap: shortcutMapObject
        }
    }
}
)qml", QUrl(QStringLiteral("inline-command-workflow.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(component.create());
        QVERIFY(window != nullptr);
        QTRY_VERIFY(window->isVisible());

        auto *menu = named(window, "workflowMenuItem");
        auto *toolbar = named(window, "workflowToolbarButton");
        auto *palette = named(window, "workflowPalette");
        QVERIFY(menu != nullptr);
        QVERIFY(toolbar != nullptr);
        QVERIFY(palette != nullptr);

        // Each presentation dispatches through its command binding/registry;
        // invocation resolves the currently active document handler.
        QVERIFY(QMetaObject::invokeMethod(menu, "click"));
        QCOMPARE(callsA, 1);

        // The visual button's AbstractButton child owns pointer, key, and
        // accessibility press behavior. Find it by its accessible name.
        QObject *visualButton = toolbar->property("button").value<QObject *>();
        QVERIFY(visualButton != nullptr);
        auto *visualItem = qobject_cast<QQuickItem *>(visualButton);
        QVERIFY(visualItem != nullptr);
        QTRY_VERIFY(visualItem->width() > 0 && visualItem->height() > 0);
        const QPoint buttonPoint = visualItem->mapToScene(
            QPointF(visualItem->width() / 2, visualItem->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, buttonPoint);
        QCOMPARE(callsA, 2);

        auto *accessible = QAccessible::queryAccessibleInterface(visualButton);
        const bool hasAccessiblePress = accessible && accessible->actionInterface()
            && accessible->actionInterface()->actionNames().contains(
                QAccessibleActionInterface::pressAction());
        QVERIFY(hasAccessiblePress);
        accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(callsA, 3);

        QVERIFY(QMetaObject::invokeMethod(palette, "open"));
        QTRY_COMPARE(palette->property("resultCount").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(palette, "activateCurrent"));
        QCOMPARE(callsA, 4);
        QVERIFY(operationIdCorrect);

        QVERIFY(shortcuts.dispatchPortable(QStringLiteral("Ctrl+Return")));
        QCOMPARE(callsA, 5);

        checked = true;
        registry.invalidate(QString::fromLatin1(commandId));
        QTRY_VERIFY(menu->property("checked").toBool());
        QVERIFY(toolbar->property("enabled").toBool());
        QCOMPARE(menu->property("text").toString(), QStringLiteral("Commit changes"));

        enabled = false;
        registry.invalidate(QString::fromLatin1(commandId));
        QTRY_VERIFY(!menu->property("enabled").toBool());
        QTRY_VERIFY(!toolbar->property("enabled").toBool());
        QObject *menuBinding = menu->property("commandBinding").value<QObject *>();
        QVERIFY(menuBinding != nullptr);
        QCOMPARE(menuBinding->property("reason").toString(), QStringLiteral("Document is read only"));
        const int beforeDisabled = callsA;
        QVERIFY(QMetaObject::invokeMethod(menu, "click"));
        QVERIFY(QMetaObject::invokeMethod(visualButton, "click"));
        QVERIFY(!shortcuts.dispatchPortable(QStringLiteral("Ctrl+Return")));
        QCOMPARE(callsA, beforeDisabled);
        QVERIFY(QMetaObject::invokeMethod(palette, "open"));
        QTRY_COMPARE(palette->property("resultCount").toInt(), 1);
        QCOMPARE(palette->property("currentRow").toInt(), -1);
        QVERIFY(QMetaObject::invokeMethod(palette, "activateCurrent"));
        QCOMPARE(callsA, beforeDisabled);

        enabled = true;
        QVERIFY(context.activate(documentB));
        QTRY_VERIFY(menu->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(menu, "click"));
        QCOMPARE(callsA, beforeDisabled);
        QCOMPARE(callsB, 1);
        QVERIFY(shortcuts.dispatchPortable(QStringLiteral("Ctrl+Return")));
        QCOMPARE(callsB, 2);
        QVERIFY(shortcuts.setOverride(QString::fromLatin1(commandId),
                                       QStringLiteral("Ctrl+Shift+Return")));
        const QString displayed = shortcuts.displaySequence(QString::fromLatin1(commandId));
        QTRY_COMPARE(menu->property("shortcut").toString(), displayed);
        const auto rows = palette->property("rows").toList();
        QVERIFY(rows.size() >= 2);
        QCOMPARE(rows.at(1).toMap().value(QStringLiteral("shortcut")).toString(), displayed);
        QVERIFY(!shortcuts.dispatchPortable(QStringLiteral("Ctrl+Return")));
        QVERIFY(shortcuts.dispatchPortable(QStringLiteral("Ctrl+Shift+Return")));
        QCOMPARE(callsB, 3);
        QVERIFY(operationIdCorrect);

        delete window;
    }
};

QTEST_MAIN(TstCommandWorkflow)
#include "tst_command_workflow.moc"
