// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTimer>

#include <qindatk/clipboard_service.h>
#include <qindatk/command_context.h>
#include <qindatk/command_registry.h>
#include <qindatk/document_manager.h>
#include <qindatk/edit_transaction.h>
#include <qindatk/property_document.h>

#include <memory>

using namespace QindaTK;

namespace {
bool expect(bool condition, int step, const char *message)
{
    if (condition) return true;
    qCritical("WP-033 installed consumer step %d failed: %s", step, message);
    return false;
}

class MemoryClipboard final : public ClipboardAdapter {
public:
    const QMimeData *mimeData() const override { return m_data.get(); }
    bool setMimeData(QMimeData *data) override
    {
        if (!data) return false;
        m_data.reset(data);
        emit changed();
        return true;
    }
private:
    std::unique_ptr<QMimeData> m_data;
};

bool saveAs(DocumentSession *session, const QString &path)
{
    QEventLoop loop;
    bool finished = false, success = false;
    QObject::connect(session, &DocumentSession::saveFinished, &loop,
                     [&](bool ok, const QString &, const QString &) {
        finished = true; success = ok; loop.quit();
    });
    if (!session->requestSaveAs(path)) return false;
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    if (!finished) loop.exec();
    return finished && success;
}

QVariantList saveAll(DocumentManager &manager, bool *finished)
{
    QEventLoop loop;
    QVariantList results;
    *finished = false;
    QObject::connect(&manager, &DocumentManager::saveAllFinished, &loop,
                     [&](const QVariantList &value) {
        results = value; *finished = true; loop.quit();
    });
    if (!manager.requestSaveAll()) return {};
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    if (!*finished) loop.exec();
    return results;
}

bool defineNote(DocumentSession *session, EntityId id, const QString &value)
{
    return session && session->document()->defineProperty({QStringLiteral("note"),
               QMetaType::fromType<QString>(), QString{}, QString{}, true, false})
        && session->document()->addEntity(id, {{QStringLiteral("note"), value}});
}

QVariantMap scopeValues(DocumentSession *session, EntityId entity,
                        const QString &viewId)
{
    return {{QStringLiteral("snapshotHeader"), ContractCodec::toQml(session->document()->header())},
            {QStringLiteral("selectionIds"), QStringList{entity.toString()}},
            {QStringLiteral("focusedViewId"), viewId}};
}

OperationError checkCommandSnapshot(const CommandInvocationContext &invocation,
                                    DocumentSession *session, EntityId entity)
{
    const auto actual = session->document()->header();
    if (invocation.request.expected.documentId != actual.documentId
        || invocation.request.expected.revision != actual.revision
        || invocation.request.expected.resetGeneration != actual.resetGeneration
        || invocation.selection != QVector<EntityId>{entity})
        return {ErrorCode::StaleRevision, QStringLiteral("Document scope is stale")};
    return {};
}

OperationError editNote(DocumentSession *session, EntityId entity,
                        const QString &text)
{
    EditTransaction edit;
    edit.setDocument(session->document());
    edit.setUndoService(session->undoService());
    if (!edit.begin({entity}, QStringLiteral("note"), QStringLiteral("Edit note")))
        return {ErrorCode::Validation, QStringLiteral("Edit could not begin")};
    if (const auto error = edit.updateValue(text); error) {
        edit.cancel();
        return error;
    }
    return edit.commitEdit();
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir temp;
    if (!expect(temp.isValid(), 1, "temporary storage")) return 1;
    const QString pathA = temp.filePath(QStringLiteral("a.qtkdoc"));
    const QString pathB = temp.filePath(QStringLiteral("b.qtkdoc"));
    const QString recoveryDir = temp.filePath(QStringLiteral("recovery"));

    DocumentManager manager;
    manager.recoveryManager()->setDirectory(recoveryDir);
    auto *a = manager.createReferenceDocument();
    auto *b = manager.createReferenceDocument();
    const EntityId entityA = EntityId::create(), entityB = EntityId::create();
    if (!expect(defineNote(a, entityA, QStringLiteral("A0"))
                    && defineNote(b, entityB, QStringLiteral("B0")),
                2, "two authoritative documents and stable entity IDs")) return 2;
    if (!expect(saveAs(a, pathA) && saveAs(b, pathB)
                    && !a->isDirty() && !b->isDirty(),
                3, "independent Save As baselines")) return 3;

    CommandContext context;
    CommandRegistry commands;
    commands.setContext(&context);
    QObject appOwner, ownerA, ownerB;
    const QString appScope = context.pushScope(QStringLiteral("application"), &appOwner);
    const QString scopeA = context.pushScope(QStringLiteral("document"), &ownerA, appScope);
    const QString scopeB = context.pushScope(QStringLiteral("document"), &ownerB, appScope);
    const QString viewA = manager.viewsFor(a->documentId()).first();
    const QString viewB = manager.viewsFor(b->documentId()).first();
    if (!expect(context.updateScope(scopeA, scopeValues(a, entityA, viewA))
                    && context.updateScope(scopeB, scopeValues(b, entityB, viewB)),
                4, "document scopes carry exact revision and selection")) return 4;

    MemoryClipboard adapter;
    ClipboardService clipboard;
    clipboard.setAdapter(&adapter);
    const QString editId = QStringLiteral("note.edit");
    const QString pasteId = QStringLiteral("note.paste");
    if (!expect(commands.registerCommand({editId, QStringLiteral("Edit note"), {},
                                         QStringLiteral("Edit"), {}, {}, false})
                    && commands.registerCommand({pasteId, QStringLiteral("Paste note"), {},
                                                 QStringLiteral("Edit"), {}, {}, false}),
                5, "shared semantic command descriptors")) return 5;
    const auto registerDocument = [&](const QString &scope, QObject *owner,
                                      DocumentSession *session, EntityId entity) {
        return commands.registerHandler(scope, editId, owner,
            [session, entity](const CommandInvocationContext &inv, const QVariantMap &args) {
                if (const auto stale = checkCommandSnapshot(inv, session, entity); stale) return stale;
                return editNote(session, entity, args.value(QStringLiteral("text")).toString());
            }) && commands.registerHandler(scope, pasteId, owner,
            [&clipboard, session, entity](const CommandInvocationContext &inv, const QVariantMap &) {
                if (const auto stale = checkCommandSnapshot(inv, session, entity); stale) return stale;
                return clipboard.paste(session->document(), session->undoService(), entity,
                                       QStringLiteral("note"));
            });
    };
    if (!expect(registerDocument(scopeA, &ownerA, a, entityA)
                    && registerDocument(scopeB, &ownerB, b, entityB),
                6, "handlers resolve per active document")) return 6;

    if (!expect(manager.activate(a->documentId()) && context.activate(scopeA)
                    && !commands.invoke(editId, {{QStringLiteral("text"), QStringLiteral("A1")}})
                    && a->document()->value(entityA, QStringLiteral("note")).toString() == QStringLiteral("A1")
                    && b->document()->value(entityB, QStringLiteral("note")).toString() == QStringLiteral("B0"),
                7, "command edits only active document A")) return 7;
    const auto stale = commands.invoke(editId, {{QStringLiteral("text"), QStringLiteral("A2")}});
    if (!expect(stale.code == ErrorCode::StaleRevision
                    && a->document()->value(entityA, QStringLiteral("note")).toString() == QStringLiteral("A1"),
                8, "stale command snapshot cannot commit another edit")) return 8;
    if (!expect(context.updateScope(scopeA, scopeValues(a, entityA, viewA))
                    && manager.activate(b->documentId()) && context.activate(scopeB)
                    && !commands.invoke(editId, {{QStringLiteral("text"), QStringLiteral("B1")}}),
                9, "shared command routes to document B")) return 9;

    if (!expect(clipboard.copyText(QStringLiteral("A1"), a->documentId(),
                                   entityA.toString(), a->revision())
                    && context.updateScope(scopeB, scopeValues(b, entityB, viewB))
                    && clipboard.inspectPaste(b->document(), entityB, QStringLiteral("note"))
                           .value(QStringLiteral("state")).toString() == QStringLiteral("ready")
                    && !commands.invoke(pasteId)
                    && b->document()->value(entityB, QStringLiteral("note")).toString() == QStringLiteral("A1")
                    && a->document()->value(entityA, QStringLiteral("note")).toString() == QStringLiteral("A1"),
                10, "clipboard transfer through active document command")) return 10;
    if (!expect(manager.undoService()->undo()
                    && b->document()->value(entityB, QStringLiteral("note")).toString() == QStringLiteral("B1")
                    && manager.activate(a->documentId()) && manager.undoService()->undo()
                    && a->document()->value(entityA, QStringLiteral("note")).toString() == QStringLiteral("A0")
                    && manager.undoService()->redo()
                    && a->document()->value(entityA, QStringLiteral("note")).toString() == QStringLiteral("A1"),
                11, "one shared undo facade keeps document histories independent")) return 11;

    if (!expect(context.activate(scopeA)
                    && context.updateScope(scopeA, scopeValues(a, entityA, viewA))
                    && !commands.invoke(editId, {{QStringLiteral("text"), QStringLiteral("A2")}}),
                12, "new A edit follows undo and updated scope")) return 12;
    QFile external(pathA);
    if (!expect(external.open(QIODevice::WriteOnly | QIODevice::Truncate)
                    && external.write("foreign replacement") > 0,
                13, "external process replaces A save baseline")) return 13;
    external.close();
    if (!expect(a->checkExternalChange(), 14, "external change is observed")) return 14;
    bool finished = false;
    const QVariantList results = saveAll(manager, &finished);
    bool aFailed = false, bSaved = false;
    for (const auto &result : results) {
        const auto row = result.toMap();
        if (row.value(QStringLiteral("documentId")).toString() == a->documentId())
            aFailed = !row.value(QStringLiteral("success")).toBool();
        if (row.value(QStringLiteral("documentId")).toString() == b->documentId())
            bSaved = row.value(QStringLiteral("success")).toBool();
    }
    if (!expect(finished && results.size() == 2 && aFailed && bSaved
                    && a->isDirty() && a->externalChanged() && !b->isDirty(),
                15, "Save All isolates A conflict while B completes")) return 15;

    const QVariantMap recovery = manager.recoveryManager()->inspectId(a->documentId());
    if (!expect(recovery.value(QStringLiteral("revision")).toString() == a->revision(),
                16, "failed save leaves latest A recovery snapshot")) return 16;
    DocumentManager reopened;
    reopened.recoveryManager()->setDirectory(recoveryDir);
    auto *savedB = reopened.openReferenceDocument(pathB);
    auto *restoredA = reopened.restoreRecovered(a->documentId(), pathA);
    if (!expect(savedB && !savedB->isDirty()
                    && savedB->document()->value(entityB, QStringLiteral("note")).toString() == QStringLiteral("B1")
                    && restoredA && restoredA->recovered() && restoredA->isDirty()
                    && restoredA->document()->value(entityA, QStringLiteral("note")).toString() == QStringLiteral("A2"),
                17, "B reopens clean; A restores latest edit without replacing external file")) return 17;
    qInfo("WP-033 installed consumer: two documents, scoped commands, stale guard, clipboard, undo, partial Save All, recovery passed");
    return 0;
}
