// SPDX-License-Identifier: LGPL-3.0-or-later
#include "accessibility_adapter.h"
#include "edit_transaction.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>
using namespace QindaTK;
class TstAccessibilityAdapter : public QObject {
    Q_OBJECT
private slots:
    void stableVirtualTreeAndLifecycle()
    {
        auto adapter = std::make_unique<AccessibilityAdapter>();
        adapter->setAccessibleName(QStringLiteral("Timeline"));
        AccessibleNode first; first.id = QStringLiteral("clip-a"); first.name = QStringLiteral("Intro");
        first.description = QStringLiteral("Track 1, start 0 seconds, duration 4 seconds");
        first.role = QAccessible::ListItem; first.bounds = QRectF(0, 0, 100, 20);
        first.offscreen = true;
        AccessibleNode second = first; second.id = QStringLiteral("clip-b"); second.name = QStringLiteral("Verse");
        QVERIFY(adapter->replaceNodes({first, second}));
        auto *root = QAccessible::queryAccessibleInterface(adapter.get());
        QCOMPARE(root->text(QAccessible::Name), QStringLiteral("Timeline"));
        QCOMPARE(root->childCount(), 2);
        auto *a = root->child(0);
        const auto aId = QAccessible::uniqueId(a);
        QCOMPARE(a->parent(), root);
        QCOMPARE(a->text(QAccessible::Identifier), first.id);
        QCOMPARE(a->text(QAccessible::Description), first.description);
        QVERIFY(a->state().offscreen);
        first.offscreen = false; first.name = QStringLiteral("Renamed intro");
        QVERIFY(adapter->replaceNodes({second, first}));
        QCOMPARE(root->child(1), a);
        QCOMPARE(QAccessible::uniqueId(root->child(1)), aId);
        QCOMPARE(root->indexOfChild(a), 1);
        QCOMPARE(a->text(QAccessible::Name), first.name);
        QVERIFY(!adapter->replaceNodes({first, first}));
        QCOMPARE(root->childCount(), 2);
        QVERIFY(!root->child(-1)); QVERIFY(!root->child(2));
        QVERIFY(adapter->replaceNodes({second}));
        QVERIFY(!QAccessible::accessibleInterface(aId));
        QVERIFY(!adapter->invoke(first.id, QStringLiteral("press")));
        auto bId = QAccessible::uniqueId(root->child(0));
        adapter.reset();
        QVERIFY(!QAccessible::accessibleInterface(bId));
    }
    void actionsUseCommandsTransactionsAndSelection()
    {
        PropertyDocument document;
        UndoService undo;
        QVERIFY(document.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(), 0.0,
                                          QStringLiteral("dB"), true, true}));
        const EntityId entity = EntityId::create();
        QVERIFY(document.addEntity(entity, {{QStringLiteral("gain"), 1.0}}));
        QVERIFY(undo.addDocument(&document)); undo.setActiveDocument(&document);
        CommandContext context; QObject owner;
        const auto scope = context.pushScope(QStringLiteral("editor"), &owner);
        QVERIFY(context.activate(scope));
        CommandRegistry commands; commands.setContext(&context);
        QVERIFY(commands.registerCommand({QStringLiteral("edit"), QStringLiteral("Edit"), {}, {}, {}, {}, false}));
        QVERIFY(commands.registerCommand({QStringLiteral("select"), QStringLiteral("Select"), {}, {}, {}, {}, false}));
        EditTransaction transaction; transaction.setDocument(&document); transaction.setUndoService(&undo);
        SelectionModel selection;
        QVERIFY(commands.registerHandler(scope, QStringLiteral("edit"), &owner,
            [&](const CommandInvocationContext &, const QVariantMap &args) {
                Q_ASSERT(args.value(QStringLiteral("semanticId")).toString() == entity.toString());
                if (!transaction.begin({entity}, QStringLiteral("gain"), QStringLiteral("Accessible gain"))) return transaction.error();
                auto error = transaction.updateValue(args.value(QStringLiteral("value")));
                return error ? error : transaction.commitEdit();
            }));
        QVERIFY(commands.registerHandler(scope, QStringLiteral("select"), &owner,
            [&](const CommandInvocationContext &, const QVariantMap &args) {
                selection.select(args.value(QStringLiteral("semanticId")).toString()); return OperationError{};
            }));
        AccessibilityAdapter adapter; adapter.setCommands(&commands); adapter.setSelection(&selection);
        AccessibleNode n; n.id = entity.toString(); n.name = QStringLiteral("Gain"); n.role = QAccessible::Slider;
        n.value = 1.0; n.minimum = -60.0; n.maximum = 6.0; n.step = 0.5; n.readOnly = false;
        n.commands.insert(QStringLiteral("setValue"), QStringLiteral("edit"));
        n.commands.insert(QAccessibleActionInterface::setFocusAction(), QStringLiteral("select"));
        QVERIFY(adapter.replaceNodes({n}));
        auto *root = QAccessible::queryAccessibleInterface(&adapter);
        auto *child = root->child(0);
        QCOMPARE(child->valueInterface()->minimumValue(), QVariant(-60.0));
        child->valueInterface()->setCurrentValue(4.0);
        QCOMPARE(document.value(entity, QStringLiteral("gain")), QVariant(4.0));
        QVERIFY(undo.canUndo()); QVERIFY(undo.undo());
        QCOMPARE(document.value(entity, QStringLiteral("gain")), QVariant(1.0));
        child->actionInterface()->doAction(QAccessibleActionInterface::setFocusAction());
        QVERIFY(child->state().selected); QCOMPARE(root->focusChild(), child);
        n.readOnly = true; QVERIFY(adapter.replaceNodes({n}));
        QSignalSpy rejected(&adapter, &AccessibilityAdapter::actionRejected);
        child->valueInterface()->setCurrentValue(5.0);
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(document.value(entity, QStringLiteral("gain")), QVariant(1.0));
        adapter.setEnabled(false);
        QVERIFY(child->actionInterface()->actionNames().isEmpty());
        child->actionInterface()->doAction(QAccessibleActionInterface::setFocusAction());
        QCOMPARE(rejected.size(), 2);
    }
    void invalidSnapshotsAreAtomic()
    {
        AccessibilityAdapter adapter;
        QVERIFY(adapter.setNodes({QVariantMap{{QStringLiteral("id"), QStringLiteral("cell-a")},
            {QStringLiteral("name"), QStringLiteral("A1: 12")},
            {QStringLiteral("role"), QAccessible::Cell}}}));
        auto *root = QAccessible::queryAccessibleInterface(&adapter);
        QCOMPARE(root->child(0)->role(), QAccessible::Cell);
        QVERIFY(!adapter.setNodes({QVariantMap{{QStringLiteral("id"), QStringLiteral("bad")},
            {QStringLiteral("role"), -1}}}));
        QCOMPARE(root->childCount(), 1);
        AccessibleNode invalid; invalid.id = QStringLiteral("bad");
        invalid.bounds = QRectF(0, 0, -2, 10);
        QVERIFY(!adapter.replaceNodes({invalid}));
        QCOMPARE(root->child(0)->text(QAccessible::Identifier), QStringLiteral("cell-a"));
    }
    void windowTreeAndQueuedAnnouncements()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QindaTK as Tk
Tk.AccessibilityAdapter {
    width: 200; height: 100
    accessibleName: "Canvas objects"
    Accessible.role: Accessible.Pane
    Accessible.name: accessibleName
})", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *adapter = qobject_cast<AccessibilityAdapter *>(object.data());
        QVERIFY(adapter);
        QQuickWindow window;
        adapter->setParentItem(window.contentItem());
        window.show(); QTest::qWait(20);
        AccessibleNode n; n.id = QStringLiteral("shape-1"); n.name = QStringLiteral("Blue circle");
        n.description = QStringLiteral("Circle centered at 30, 30, radius 10");
        n.role = QAccessible::Graphic; n.bounds = QRectF(20, 20, 20, 20);
        QVERIFY(adapter->replaceNodes({n}));
        auto *root = QAccessible::queryAccessibleInterface(adapter);
        auto *parent = root->parent(); QVERIFY(parent);
        QVERIFY(parent->indexOfChild(root) >= 0);
        QCOMPARE(parent->child(parent->indexOfChild(root)), root);
        QCOMPARE(root->child(0)->role(), QAccessible::Graphic);
        const auto center = adapter->mapToGlobal(QPointF(30, 30)).toPoint();
        QCOMPARE(root->childAt(center.x(), center.y()), root->child(0));
        QSignalSpy announced(adapter, &AccessibilityAdapter::announcementDelivered);
        adapter->announce(QStringLiteral("Selected circle"));
        adapter->announce(QStringLiteral("Selected circle"));
        adapter->announce(QStringLiteral("Moved circle"));
        QTRY_COMPARE(announced.size(), 2);
        QCOMPARE(announced.at(0).at(0).toString(), QStringLiteral("Selected circle"));
        adapter->setParentItem(nullptr);
    }
};
QTEST_MAIN(TstAccessibilityAdapter)
#include "tst_accessibility_adapter.moc"
