// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QUrl>

#include <qindatk/application_contracts.h>
#include <qindatk/command_context.h>
#include <qindatk/command_registry.h>
#include <qindatk/shortcut_map.h>

namespace {
constexpr auto commandId = "document.save";

QVariantMap snapshotValues(const QindaTK::DocumentId &documentId, quint64 revision)
{
    const QindaTK::SnapshotHeader header{
        QStringLiteral("consumer.document.v1"), documentId,
        QindaTK::RevisionId{revision}, revision};
    return {{QStringLiteral("snapshotHeader"), QindaTK::ContractCodec::toQml(header)}};
}

bool check(bool condition, int code)
{
    if (condition)
        return true;
    qCritical("QindaTK commands consumer check failed: %d", code);
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QindaTK::CommandContext context;
    QindaTK::CommandRegistry registry;
    registry.setContext(&context);
    QindaTK::ShortcutMap shortcuts;
    shortcuts.setRegistry(&registry);

    QObject applicationOwner;
    QObject documentAOwner;
    QObject documentBOwner;
    const QString applicationScope =
        context.pushScope(QStringLiteral("application"), &applicationOwner);
    const QString documentAScope = context.pushScope(
        QStringLiteral("document-a"), &documentAOwner, applicationScope);
    const QString documentBScope = context.pushScope(
        QStringLiteral("document-b"), &documentBOwner, applicationScope);
    const auto documentA = QindaTK::DocumentId::create();
    const auto documentB = QindaTK::DocumentId::create();
    if (!context.updateScope(documentAScope, snapshotValues(documentA, 7))
        || !context.updateScope(documentBScope, snapshotValues(documentB, 12))
        || !context.activate(documentAScope)) {
        return 1;
    }

    if (!registry.registerCommand({QString::fromLatin1(commandId),
            QStringLiteral("Save document"), QStringLiteral("save"),
            QStringLiteral("File"), QStringLiteral("write commit"),
            QStringLiteral("Ctrl+S"), false})) {
        return 2;
    }

    bool documentAEnabled = true;
    int documentACalls = 0;
    int documentBCalls = 0;
    bool receivedExpectedDocument = true;
    const auto handler = [&receivedExpectedDocument](int *calls,
                                                     QindaTK::DocumentId expected) {
        return [calls, expected, &receivedExpectedDocument](
                   const QindaTK::CommandInvocationContext &invocation,
                   const QVariantMap &) {
            ++*calls;
            receivedExpectedDocument = receivedExpectedDocument
                && invocation.request.operationId == QString::fromLatin1(commandId)
                && invocation.request.expected.documentId == expected;
            return QindaTK::OperationError{};
        };
    };
    if (!registry.registerHandler(documentAScope, QString::fromLatin1(commandId),
            &documentAOwner, handler(&documentACalls, documentA),
            [&documentAEnabled](const QindaTK::CommandInvocationContext &) {
                return QindaTK::CommandState{
                    documentAEnabled, false, true,
                    documentAEnabled ? QString{} : QStringLiteral("Document A is read only")};
            })
        || !registry.registerHandler(documentBScope, QString::fromLatin1(commandId),
            &documentBOwner, handler(&documentBCalls, documentB))) {
        return 3;
    }
    if (!shortcuts.bindCommand(QString::fromLatin1(commandId)))
        return 4;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.rootContext()->setContextProperty(QStringLiteral("appCommandRegistry"), &registry);
    engine.rootContext()->setContextProperty(QStringLiteral("appShortcutMap"), &shortcuts);
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (!check(engine.rootObjects().size() == 1, 5))
        return 5;

    QObject *root = engine.rootObjects().first();
    if (!check(root->property("bindingEnabled").toBool()
                   && root->property("commandButtonEnabled").toBool()
                   && root->property("bindingReason").toString().isEmpty()
                   && root->property("bindingShortcut").toString()
                       == shortcuts.displaySequence(QString::fromLatin1(commandId)), 6)) {
        return 6;
    }

    if (!check(shortcuts.setOverride(QString::fromLatin1(commandId),
                                     QStringLiteral("Ctrl+Shift+S")), 7))
        return 7;
    QCoreApplication::processEvents();
    if (!check(root->property("bindingShortcut").toString()
                   == shortcuts.displaySequence(QString::fromLatin1(commandId))
                   && !shortcuts.dispatchPortable(QStringLiteral("Ctrl+S"))
                   && shortcuts.dispatchPortable(QStringLiteral("Ctrl+Shift+S"))
                   && documentACalls == 1, 8)) {
        return 8;
    }

    documentAEnabled = false;
    registry.invalidate(QString::fromLatin1(commandId));
    QCoreApplication::processEvents();
    if (!check(!root->property("bindingEnabled").toBool()
                   && !root->property("commandButtonEnabled").toBool()
                   && root->property("bindingReason").toString()
                       == QStringLiteral("Document A is read only")
                   && root->property("commandButtonReason").toString()
                       == QStringLiteral("Document A is read only")
                   && !shortcuts.dispatchPortable(QStringLiteral("Ctrl+Shift+S"))
                   && documentACalls == 1, 9)) {
        return 9;
    }

    if (!check(context.activate(documentBScope), 10))
        return 10;
    QCoreApplication::processEvents();
    if (!check(root->property("bindingEnabled").toBool()
                   && root->property("commandButtonEnabled").toBool()
                   && root->property("bindingReason").toString().isEmpty(), 11)) {
        return 11;
    }

    QObject *visualButton = root->property("visualButton").value<QObject *>();
    if (!check(visualButton != nullptr
                   && QMetaObject::invokeMethod(visualButton, "click")
                   && shortcuts.dispatchPortable(QStringLiteral("Ctrl+Shift+S"))
                   && documentACalls == 1 && documentBCalls == 2
                   && receivedExpectedDocument, 12)) {
        return 12;
    }
    return 0;
}
