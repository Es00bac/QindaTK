// SPDX-License-Identifier: LGPL-3.0-or-later
#include "script_runtime.h"
#include "command_context.h"
#include "command_registry.h"
#include "property_document.h"
#include "undo_service.h"

#include <QtTest>

using namespace QindaTK;

class TstScriptRuntime final : public QObject {
    Q_OBJECT
private slots:
    void snapshotsOutputAndExplicitCommandCapability()
    {
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const QString scope = context.pushScope(QStringLiteral("app"), &owner);
        QVERIFY(context.activate(scope));
        QVERIFY(registry.registerCommand({QStringLiteral("edit.mark"), QStringLiteral("Mark"),
                                          {}, QStringLiteral("Edit"), {}, {}, false}));
        int calls = 0;
        QVERIFY(registry.registerHandler(scope, QStringLiteral("edit.mark"), &owner,
            [&calls](const CommandInvocationContext &, const QVariantMap &args) {
                calls += args.value(QStringLiteral("count")).toInt();
                return OperationError{};
            }));
        ScriptRuntime runtime;
        runtime.setCommandRegistry(&registry);
        runtime.setDocumentSnapshot({{QStringLiteral("title"), QStringLiteral("Canvas")}});
        QVERIFY(runtime.execute(QStringLiteral("app.print(app.inspect('title')); "
                                               "app.command('edit.mark', {count: 2});")));
        QTRY_VERIFY(!runtime.running());
        QCOMPARE(calls, 0);
        QVERIFY(runtime.errorString().contains(QStringLiteral("not exposed")));
        runtime.setExposedCommandIds({QStringLiteral("edit.mark")});
        QVERIFY(runtime.execute(QStringLiteral("app.print(app.inspect('title')); "
                                               "app.command('edit.mark', {count: 2});")));
        QTRY_VERIFY(!runtime.running());
        QCOMPARE(runtime.status(), QStringLiteral("completed"));
        QCOMPARE(runtime.output(), QStringLiteral("Canvas"));
        QCOMPARE(calls, 2);
        QCOMPARE(runtime.recordedCommands().size(), 1);
        QVERIFY(runtime.replayRecorded());
        QCOMPARE(calls, 4);
    }

    void semanticMacroUndoAndFailureRollback()
    {
        PropertyDocument document;
        QVERIFY(document.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                         0.0, {}, true, false}));
        const EntityId entity = EntityId::create();
        QVERIFY(document.addEntity(entity, {{QStringLiteral("gain"), 1.0}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        undo.setActiveDocument(&document);
        CommandContext context;
        CommandRegistry registry;
        registry.setContext(&context);
        QObject owner;
        const QString scope = context.pushScope(QStringLiteral("document"), &owner);
        QVERIFY(context.activate(scope));
        QVERIFY(registry.registerCommand({QStringLiteral("gain.set"), QStringLiteral("Set gain"),
                                          {}, QStringLiteral("Edit"), {}, {}, false}));
        QVERIFY(registry.registerHandler(scope, QStringLiteral("gain.set"), &owner,
            [&](const CommandInvocationContext &, const QVariantMap &args) {
                const double next = args.value(QStringLiteral("value")).toDouble();
                if (next > 8.0)
                    return OperationError{ErrorCode::Validation, QStringLiteral("Gain too high"), {}, {}};
                const double previous = document.value(entity, QStringLiteral("gain")).toDouble();
                return undo.commit(&document, QStringLiteral("Set gain"), {},
                    std::make_shared<PropertyCommand>(QVector<PropertyChange>{
                        {entity, QStringLiteral("gain"), previous, next}}));
            }));
        ScriptRuntime runtime;
        runtime.setCommandRegistry(&registry);
        runtime.setUndoService(&undo);
        runtime.setExposedCommandIds({QStringLiteral("gain.set")});
        QVERIFY(runtime.execute(QStringLiteral("app.command('gain.set',{value:2});"
                                               "app.command('gain.set',{value:4});")));
        QTRY_VERIFY(!runtime.running());
        QCOMPARE(runtime.status(), QStringLiteral("completed"));
        QCOMPARE(document.value(entity, QStringLiteral("gain")).toDouble(), 4.0);
        QCOMPARE(undo.historyEntries().size(), 1);
        QVERIFY(undo.undo());
        QCOMPARE(document.value(entity, QStringLiteral("gain")).toDouble(), 1.0);
        QVERIFY(undo.redo());
        QCOMPARE(document.value(entity, QStringLiteral("gain")).toDouble(), 4.0);

        QVERIFY(runtime.execute(QStringLiteral("app.command('gain.set',{value:5});"
                                               "app.command('gain.set',{value:9});")));
        QTRY_VERIFY(!runtime.running());
        QCOMPARE(runtime.status(), QStringLiteral("failed"));
        QVERIFY(runtime.errorString().contains(QStringLiteral("Gain too high")));
        QCOMPARE(document.value(entity, QStringLiteral("gain")).toDouble(), 4.0);
    }

    void timeoutCancelsBeforeAnyCommand()
    {
        ScriptRuntime runtime;
        runtime.setMaxRuntimeMs(100);
        QVERIFY(runtime.execute(QStringLiteral("while (true) {}")));
        QTRY_VERIFY_WITH_TIMEOUT(!runtime.running(), 3000);
        QCOMPARE(runtime.status(), QStringLiteral("canceled"));
        QVERIFY(runtime.errorString().contains(QStringLiteral("time limit")));
        QVERIFY(runtime.recordedCommands().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstScriptRuntime)
#include "tst_script_runtime.moc"
