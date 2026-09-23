// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_context.h"
#include "command_registry.h"
#include "shortcut_map.h"
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace QindaTK;

class TstShortcutMap final : public QObject {
    Q_OBJECT
private:
    static void command(CommandRegistry &registry, CommandContext &context,
                        const QString &scope, QObject *owner, const QString &id,
                        int &calls)
    {
        QVERIFY(registry.registerCommand({id, id, {}, {}, {}, {}, false}));
        QVERIFY(registry.registerHandler(scope, id, owner,
            [&calls](const CommandInvocationContext &, const QVariantMap &) {
                ++calls;
                return OperationError{};
            }));
        QVERIFY(context.activate(scope));
    }

private slots:
    void portableBindingsDispatchAndOverridesRoundTrip()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("application"), &owner);
        int calls = 0;
        command(registry, context, scope, &owner, QStringLiteral("file.save"), calls);
        ShortcutMap map;
        map.setRegistry(&registry);
        QVERIFY(map.bind(QStringLiteral("file.save"), QStringLiteral("Ctrl+S")));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+S")));
        QCOMPARE(calls, 1);
        QCOMPARE(map.displaySequence(QStringLiteral("file.save")),
                 QKeySequence::fromString(QStringLiteral("Ctrl+S"),
                     QKeySequence::PortableText).toString(QKeySequence::NativeText));

        QVERIFY(map.setOverride(QStringLiteral("file.save"), QStringLiteral("Ctrl+Shift+S")));
        QVERIFY(!map.dispatchPortable(QStringLiteral("Ctrl+S")));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+Shift+S")));
        QCOMPARE(calls, 2);
        const auto saved = map.overrides();
        QCOMPARE(saved.value(QStringLiteral("file.save")).toString(),
                 QStringLiteral("Ctrl+Shift+S"));
        map.resetOverride(QStringLiteral("file.save"));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+S")));
        QVERIFY(map.loadOverrides(saved));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+Shift+S")));
        QCOMPARE(calls, 4);
        QVERIFY(!map.loadOverrides({{QStringLiteral("missing.command"),
                                     QStringLiteral("Ctrl+M")}}));
        QCOMPARE(map.overrides(), saved);
        QVERIFY(!map.setOverride(QStringLiteral("file.save"), QStringLiteral("invalid key name")));
    }

    void identicalBindingsReportConflictWithoutInvokingEither()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("application"), &owner);
        int first = 0;
        int second = 0;
        command(registry, context, scope, &owner, QStringLiteral("edit.first"), first);
        command(registry, context, scope, &owner, QStringLiteral("edit.second"), second);
        ShortcutMap map;
        map.setRegistry(&registry);
        QVERIFY(map.bind(QStringLiteral("edit.first"), QStringLiteral("Ctrl+J")));
        QVERIFY(map.bind(QStringLiteral("edit.second"), QStringLiteral("Ctrl+J")));
        QCOMPARE(map.conflicts().size(), 1);
        QSignalSpy conflict(&map, &ShortcutMap::conflict);
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+J")));
        QCOMPARE(first, 0);
        QCOMPARE(second, 0);
        QCOMPARE(conflict.size(), 1);
    }

    void specificScopeWinsAndClosedScopeFallsBack()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject appOwner;
        auto *documentOwner = new QObject;
        const auto app = context.pushScope(QStringLiteral("application"), &appOwner);
        const auto document = context.pushScope(QStringLiteral("document"), documentOwner, app);
        int appCalls = 0;
        int documentCalls = 0;
        command(registry, context, app, &appOwner, QStringLiteral("app.action"), appCalls);
        command(registry, context, document, documentOwner,
                QStringLiteral("document.action"), documentCalls);
        ShortcutMap map;
        map.setRegistry(&registry);
        QVERIFY(map.bind(QStringLiteral("app.action"), QStringLiteral("Ctrl+D")));
        QVERIFY(map.bind(QStringLiteral("document.action"), QStringLiteral("Ctrl+D"), document));
        QVERIFY(context.activate(document));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+D")));
        QCOMPARE(documentCalls, 1);
        QCOMPARE(appCalls, 0);
        delete documentOwner;
        QVERIFY(!map.hasBinding(QStringLiteral("document.action")));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+D")));
        QCOMPARE(appCalls, 1);
        QCOMPARE(documentCalls, 1);
    }

    void chordCancellationAndMismatchDoNotInvokeWrongCommand()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("application"), &owner);
        int commentCalls = 0;
        int saveCalls = 0;
        command(registry, context, scope, &owner, QStringLiteral("edit.comment"), commentCalls);
        command(registry, context, scope, &owner, QStringLiteral("file.save"), saveCalls);
        ShortcutMap map;
        map.setRegistry(&registry);
        QVERIFY(map.bind(QStringLiteral("edit.comment"),
                         QStringLiteral("Ctrl+K, Ctrl+C")));
        QVERIFY(map.bind(QStringLiteral("file.save"), QStringLiteral("Ctrl+S")));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+K")));
        QVERIFY(!map.pendingChord().isEmpty());
        QVERIFY(map.dispatchPortable(QStringLiteral("Escape")));
        QVERIFY(map.pendingChord().isEmpty());
        QCOMPARE(commentCalls, 0);
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+K")));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+S")));
        QCOMPARE(saveCalls, 1);
        QCOMPARE(commentCalls, 0);
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+K")));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+C")));
        QCOMPARE(commentCalls, 1);
    }

    void textEditingAndCompositionRetainKeys()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("application"), &owner);
        int undoCalls = 0;
        int saveCalls = 0;
        command(registry, context, scope, &owner, QStringLiteral("edit.undo"), undoCalls);
        command(registry, context, scope, &owner, QStringLiteral("file.save"), saveCalls);
        ShortcutMap map;
        map.setRegistry(&registry);
        QVERIFY(map.bind(QStringLiteral("edit.undo"), QStringLiteral("Ctrl+Z")));
        QVERIFY(map.bind(QStringLiteral("file.save"), QStringLiteral("Ctrl+S")));
        QVERIFY(!map.dispatchPortable(QStringLiteral("Ctrl+Z"), true, false));
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+S"), true, false));
        QVERIFY(!map.dispatchPortable(QStringLiteral("Ctrl+Z"), false, true));
        QCOMPARE(undoCalls, 0);
        QCOMPARE(saveCalls, 1);
        QVERIFY(map.dispatchPortable(QStringLiteral("Ctrl+Z"), false, false));
        QCOMPARE(undoCalls, 1);
    }

    void windowFilterRoutesKeyPressToRegistry()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("application"), &owner);
        int calls = 0;
        command(registry, context, scope, &owner, QStringLiteral("file.save"), calls);
        QQuickWindow window;
        ShortcutMap map;
        map.setRegistry(&registry);
        map.setWindow(&window);
        QVERIFY(map.bind(QStringLiteral("file.save"), QStringLiteral("Ctrl+S")));
        QKeyEvent key(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
        QCoreApplication::sendEvent(&window, &key);
        QCOMPARE(calls, 1);
    }

    void liveTextInputCompositionKeepsShortcutFromDispatching()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("application"), &owner);
        int calls = 0;
        command(registry, context, scope, &owner, QStringLiteral("file.save"), calls);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QtQuick.Window
Window {
    visible: true
    width: 240; height: 120
    TextInput { objectName: "imeInput"; focus: true; width: 200; height: 30 }
}
)qml", QUrl(QStringLiteral("inline-ime.qml")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(component.create());
        QVERIFY(window != nullptr);
        auto *input = window->findChild<QQuickItem *>(QStringLiteral("imeInput"));
        QVERIFY(input != nullptr);
        QTRY_COMPARE(window->activeFocusItem(), input);
        ShortcutMap map;
        map.setRegistry(&registry);
        map.setWindow(window);
        QVERIFY(map.bind(QStringLiteral("file.save"), QStringLiteral("Ctrl+S")));

        QInputMethodEvent preedit(QStringLiteral("あ"), {});
        QCoreApplication::sendEvent(input, &preedit);
        QTRY_VERIFY(input->property("inputMethodComposing").toBool());
        QKeyEvent key(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
        QCoreApplication::sendEvent(window, &key);
        QCOMPARE(calls, 0);

        QInputMethodEvent finish(QString{}, {});
        QCoreApplication::sendEvent(input, &finish);
        QTRY_VERIFY(!input->property("inputMethodComposing").toBool());
        QKeyEvent next(QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier);
        QCoreApplication::sendEvent(window, &next);
        QCOMPARE(calls, 1);
        delete window;
    }
};

QTEST_MAIN(TstShortcutMap)
#include "tst_shortcut_map.moc"
