// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_registry.h"

#include <QtTest>

using namespace QindaTK;

class TstCommandRegistry final : public QObject {
    Q_OBJECT
private:
    static CommandDescriptor descriptor(const QString &id = QStringLiteral("edit.apply"))
    {
        return {id, QStringLiteral("Apply"), QStringLiteral("check"),
                QStringLiteral("Edit"), QStringLiteral("apply commit"),
                QStringLiteral("Ctrl+Return"), true};
    }

private slots:
    void mostSpecificActiveHandlerWinsAndDocumentsSwitch()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject appOwner;
        QObject firstOwner;
        QObject secondOwner;
        const auto app = context.pushScope(QStringLiteral("application"), &appOwner);
        const auto first = context.pushScope(QStringLiteral("document-a"), &firstOwner, app);
        const auto second = context.pushScope(QStringLiteral("document-b"), &secondOwner, app);
        QVERIFY(registry.registerCommand(descriptor()));
        int appCalls = 0;
        int firstCalls = 0;
        int secondCalls = 0;
        QVERIFY(registry.registerHandler(app, QStringLiteral("edit.apply"), &appOwner,
            [&appCalls](const CommandInvocationContext &, const QVariantMap &) {
                ++appCalls;
                return OperationError{};
            }));
        QVERIFY(registry.registerHandler(first, QStringLiteral("edit.apply"), &firstOwner,
            [&firstCalls](const CommandInvocationContext &, const QVariantMap &) {
                ++firstCalls;
                return OperationError{};
            }));
        QVERIFY(registry.registerHandler(second, QStringLiteral("edit.apply"), &secondOwner,
            [&secondCalls](const CommandInvocationContext &, const QVariantMap &) {
                ++secondCalls;
                return OperationError{};
            }));

        QVERIFY(context.activate(first));
        QCOMPARE(registry.resolve(QStringLiteral("edit.apply")).scopeToken, first);
        QVERIFY(!registry.invoke(QStringLiteral("edit.apply")));
        QVERIFY(context.activate(second));
        QCOMPARE(registry.resolve(QStringLiteral("edit.apply")).scopeToken, second);
        QVERIFY(!registry.invoke(QStringLiteral("edit.apply")));
        QVERIFY(context.activate(app));
        QCOMPARE(registry.resolve(QStringLiteral("edit.apply")).scopeToken, app);
        QVERIFY(!registry.invoke(QStringLiteral("edit.apply")));
        QCOMPARE(appCalls, 1);
        QCOMPARE(firstCalls, 1);
        QCOMPARE(secondCalls, 1);
    }

    void stateDrivesDisabledReasonCheckedAndVisibility()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("document"), &owner);
        QVERIFY(context.activate(scope));
        QVERIFY(registry.registerCommand(descriptor()));
        QVERIFY(registry.registerHandler(scope, QStringLiteral("edit.apply"), &owner,
            [](const CommandInvocationContext &, const QVariantMap &) { return OperationError{}; },
            [](const CommandInvocationContext &) {
                return CommandState{false, true, false, QStringLiteral("Read only")};
            }));

        const auto presentation = registry.resolve(QStringLiteral("edit.apply"));
        QVERIFY(!presentation.enabled);
        QVERIFY(presentation.checked);
        QVERIFY(!presentation.visible);
        QCOMPARE(presentation.reason, QStringLiteral("Read only"));
        QCOMPARE(registry.paletteCommands().size(), 0);
        const auto qml = registry.query(QStringLiteral("edit.apply"));
        QCOMPARE(qml.value(QStringLiteral("reason")).toString(), QStringLiteral("Read only"));
        QVERIFY(qml.value(QStringLiteral("checkable")).toBool());
        QVERIFY(qml.value(QStringLiteral("checked")).toBool());
        QVERIFY(!qml.value(QStringLiteral("visible")).toBool());
    }

    void invocationRunsExactlyOnceAndPublishesSignals()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("document"), &owner);
        QVERIFY(context.activate(scope));
        QVERIFY(registry.registerCommand(descriptor()));
        int calls = 0;
        QVariantMap received;
        bool operationIdCorrect = false;
        QVERIFY(registry.registerHandler(scope, QStringLiteral("edit.apply"), &owner,
            [&calls, &received, &operationIdCorrect](const CommandInvocationContext &invocation,
                                                     const QVariantMap &arguments) {
                ++calls;
                operationIdCorrect = invocation.request.operationId == QStringLiteral("edit.apply");
                received = arguments;
                return OperationError{};
            }));
        QSignalSpy invoked(&registry, &CommandRegistry::invoked);
        const QVariantMap arguments{{QStringLiteral("mode"), QStringLiteral("merge")}};

        QVERIFY(!registry.invoke(QStringLiteral("edit.apply"), arguments));
        QCOMPARE(calls, 1);
        QVERIFY(operationIdCorrect);
        QCOMPARE(received, arguments);
        QCOMPARE(invoked.size(), 1);
        QCOMPARE(invoked.at(0).at(0).toString(), QStringLiteral("edit.apply"));
        QCOMPARE(invoked.at(0).at(1).toString(), scope);
    }

    void replacingHandlerAndDestroyingOwnerDoNotRetainStaleCallbacks()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject scopeOwner;
        auto *oldOwner = new QObject;
        QObject newOwner;
        const auto scope = context.pushScope(QStringLiteral("document"), &scopeOwner);
        QVERIFY(context.activate(scope));
        QVERIFY(registry.registerCommand(descriptor()));
        int oldCalls = 0;
        int newCalls = 0;
        QVERIFY(registry.registerHandler(scope, QStringLiteral("edit.apply"), oldOwner,
            [&oldCalls](const CommandInvocationContext &, const QVariantMap &) {
                ++oldCalls;
                return OperationError{};
            }));
        QVERIFY(registry.registerHandler(scope, QStringLiteral("edit.apply"), &newOwner,
            [&newCalls](const CommandInvocationContext &, const QVariantMap &) {
                ++newCalls;
                return OperationError{};
            }));
        delete oldOwner; // the old destroyed callback must not remove the replacement
        QVERIFY(!registry.invoke(QStringLiteral("edit.apply")));
        QCOMPARE(oldCalls, 0);
        QCOMPARE(newCalls, 1);

        auto *scopedHandlerOwner = new QObject;
        QVERIFY(registry.registerHandler(scope, QStringLiteral("edit.apply"), scopedHandlerOwner,
            [](const CommandInvocationContext &, const QVariantMap &) { return OperationError{}; }));
        delete scopedHandlerOwner;
        QVERIFY(!registry.resolve(QStringLiteral("edit.apply")).enabled);
        QVERIFY(!registry.trigger(QStringLiteral("edit.apply")));
    }

    void bindingRefreshesOnContextAndRegistryChanges()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("document"), &owner);
        QVERIFY(context.activate(scope));
        QVERIFY(registry.registerCommand(descriptor()));
        bool enabled = false;
        QVERIFY(registry.registerHandler(scope, QStringLiteral("edit.apply"), &owner,
            [](const CommandInvocationContext &, const QVariantMap &) { return OperationError{}; },
            [&enabled](const CommandInvocationContext &) {
                return CommandState{enabled, false, true,
                                    enabled ? QString{} : QStringLiteral("Still loading")};
            }));

        CommandBinding binding;
        binding.setRegistry(&registry);
        binding.setCommandId(QStringLiteral("edit.apply"));
        QCOMPARE(binding.label(), QStringLiteral("Apply"));
        QCOMPARE(binding.iconName(), QStringLiteral("check"));
        QCOMPARE(binding.shortcut(), QStringLiteral("Ctrl+Return"));
        QVERIFY(binding.checkable());
        QVERIFY(!binding.enabled());
        QCOMPARE(binding.reason(), QStringLiteral("Still loading"));
        QSignalSpy changed(&binding, &CommandBinding::changed);

        enabled = true;
        registry.invalidate(QStringLiteral("edit.apply"));
        QVERIFY(binding.enabled());
        QVERIFY(binding.reason().isEmpty());
        QVERIFY(changed.size() >= 1);
        QVERIFY(binding.trigger());

        QVERIFY(registry.removeCommand(QStringLiteral("edit.apply")));
        QVERIFY(!binding.enabled());
        QCOMPARE(binding.reason(), QStringLiteral("Unknown command"));
        binding.setRegistry(nullptr);
        QCOMPARE(binding.reason(), QStringLiteral("No command registry"));
    }

    void invalidCommandsHandlersAndDisabledTriggerAreRejected()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const auto scope = context.pushScope(QStringLiteral("document"), &owner);
        QVERIFY(!registry.registerCommand({{}, QStringLiteral("No id"), {}, {}, {}, {}, false}));
        QVERIFY(!registry.registerCommand({QStringLiteral("missing.label"), {}, {}, {}, {}, {}, false}));
        QVERIFY(registry.registerCommand(descriptor()));
        QVERIFY(!registry.registerHandler(scope, QStringLiteral("unknown"), &owner,
            [](const CommandInvocationContext &, const QVariantMap &) { return OperationError{}; }));
        QVERIFY(!registry.registerHandler(QStringLiteral("stale-scope"), QStringLiteral("edit.apply"),
            &owner, [](const CommandInvocationContext &, const QVariantMap &) { return OperationError{}; }));
        QVERIFY(!registry.registerHandler(scope, QStringLiteral("edit.apply"), nullptr,
            [](const CommandInvocationContext &, const QVariantMap &) { return OperationError{}; }));

        QSignalSpy rejected(&registry, &CommandRegistry::rejected);
        QVERIFY(!registry.trigger(QStringLiteral("edit.apply")));
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(), QStringLiteral("edit.apply"));
        QVERIFY(!registry.trigger(QStringLiteral("unknown")));
        QCOMPARE(rejected.size(), 2);
        QVERIFY(!registry.query(QStringLiteral("unknown")).value(QStringLiteral("enabled")).toBool());
    }
};

QTEST_GUILESS_MAIN(TstCommandRegistry)
#include "tst_command_registry.moc"
