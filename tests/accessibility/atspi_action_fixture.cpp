// SPDX-License-Identifier: LGPL-3.0-or-later
#include "accessibility_adapter.h"
#include "command_registry.h"
#include "edit_transaction.h"
#include "property_document.h"
#include "undo_service.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>

using namespace QindaTK;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "usage: atspi_action_fixture <qml-import-dir> <fixture.qml>\n");
        return 2;
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[1]));
    engine.load(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[2])));
    if (engine.rootObjects().isEmpty()) return 3;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *adapter = window ? window->findChild<AccessibilityAdapter *>(QStringLiteral("atspiTimeline")) : nullptr;
    if (!window || !adapter) return 4;

    PropertyDocument document;
    UndoService undo;
    const auto entity = EntityId::create();
    if (!document.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                  1.0, QStringLiteral("unitless"), true, false})
        || !document.addEntity(entity, {{QStringLiteral("gain"), 1.0}})
        || !undo.addDocument(&document)) return 5;
    undo.setActiveDocument(&document);
    QVector<AccessibleNode> nodes;
    for (const auto &id : adapter->nodeIds()) {
        auto node = *adapter->node(id);
        if (id == QStringLiteral("timeline-intro")) node.id = entity.toString();
        nodes.append(node);
    }
    if (!adapter->replaceNodes(nodes)) return 9;

    QObject owner;
    CommandContext context;
    const auto scope = context.pushScope(QStringLiteral("atspi-fixture"), &owner);
    if (scope.isEmpty() || !context.activate(scope)) return 6;
    CommandRegistry commands;
    commands.setContext(&context);
    if (!commands.registerCommand({QStringLiteral("atspi.increment"), QStringLiteral("Increment"),
                                   {}, {}, {}, {}, false})) return 7;

    EditTransaction transaction;
    transaction.setDocument(&document);
    transaction.setUndoService(&undo);
    if (!commands.registerHandler(scope, QStringLiteral("atspi.increment"), &owner,
        [&](const CommandInvocationContext &, const QVariantMap &) {
            if (!transaction.begin({entity}, QStringLiteral("gain"), QStringLiteral("AT-SPI increment")))
                return transaction.error();
            const auto update = transaction.updateValue(document.value(entity, QStringLiteral("gain")).toDouble() + 1.0);
            if (update) return update;
            const auto committed = transaction.commitEdit();
            if (!committed) {
                QVector<AccessibleNode> updated;
                for (const auto &id : adapter->nodeIds()) {
                    auto node = *adapter->node(id);
                    if (id == entity.toString()) node.value = document.value(entity, QStringLiteral("gain"));
                    updated.append(node);
                }
                adapter->replaceNodes(updated);
                std::printf("ACTION_HANDLER_CALLED value=%s undo=%d\n",
                    qPrintable(document.value(entity, QStringLiteral("gain")).toString()), undo.canUndo());
                std::fflush(stdout);
            }
            return committed;
        })) return 8;
    adapter->setCommands(&commands);

    window->show();
    return app.exec();
}
