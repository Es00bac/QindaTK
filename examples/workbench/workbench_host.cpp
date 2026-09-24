// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbench_host.h"

#include <QDebug>
#include <QMetaType>
#include <qindatk/edit_transaction.h>

namespace {
const QStringList columns{QStringLiteral("name"), QStringLiteral("kind"),
                          QStringLiteral("owner"), QStringLiteral("state")};
struct Record { const char *id; const char *name; const char *kind; const char *owner; const char *state; };
const QVector<Record> boardRows{
    {"00000000-0000-0000-0000-000000000101", "Opening shot", "Video", "Editorial", "Ready"},
    {"00000000-0000-0000-0000-000000000102", "Title card", "Graphic", "Design", "Review"},
    {"00000000-0000-0000-0000-000000000103", "Interview", "Video", "Editorial", "Ready"},
    {"00000000-0000-0000-0000-000000000104", "End frame", "Graphic", "Design", "Draft"}};
const QVector<Record> sheetRows{
    {"00000000-0000-0000-0000-000000000201", "Dialogue", "Audio", "Mix", "Ready"},
    {"00000000-0000-0000-0000-000000000202", "Room tone", "Audio", "Mix", "Review"},
    {"00000000-0000-0000-0000-000000000203", "Music", "Audio", "Mix", "Draft"},
    {"00000000-0000-0000-0000-000000000204", "Master", "Bus", "Mix", "Ready"}};

class DocumentCellSource : public GridCellSource {
public:
    explicit DocumentCellSource(PropertyDocument *document, QObject *parent = nullptr)
        : GridCellSource(parent), m_document(document) {}
    QVariant value(const QString &rowId, const QString &columnId) const override
    { return m_document ? m_document->valueFor(rowId, columnId) : QVariant{}; }
    QString validate(const QString &rowId, const QString &columnId,
                     const QVariant &candidate) const override
    {
        if (!m_document) return QStringLiteral("Document is unavailable");
        const auto descriptor = m_document->descriptor(columnId);
        if (!descriptor || !descriptor->writable) return QStringLiteral("Property is not editable");
        const auto error = m_document->validate(EntityId::parse(rowId), columnId, candidate);
        return error ? error.message : QString{};
    }
private:
    QPointer<PropertyDocument> m_document;
};
} // namespace

WorkbenchHost::WorkbenchHost(QObject *parent) : QObject(parent)
{
    m_manager.setObjectName(QStringLiteral("workbenchDocumentManager"));
    m_boardGrid.setObjectName(QStringLiteral("workbenchBoardGrid"));
    m_sheetGrid.setObjectName(QStringLiteral("workbenchSheetGrid"));
    m_boardAssets.setObjectName(QStringLiteral("workbenchBoardAssets"));
    m_sheetAssets.setObjectName(QStringLiteral("workbenchSheetAssets"));
    m_board = seed(true, &m_boardGrid, &m_boardAssets);
    m_sheet = seed(false, &m_sheetGrid, &m_sheetAssets);
    m_commands.setContext(&m_commandContext);
    const QString appScope = m_commandContext.pushScope(QStringLiteral("workbench"), this);
    m_boardScope = m_commandContext.pushScope(QStringLiteral("storyboard"), this, appScope);
    m_sheetScope = m_commandContext.pushScope(QStringLiteral("mix-sheet"), this, appScope);
    m_commands.registerCommand({QStringLiteral("workbench.rename"), QStringLiteral("Apply name"),
        QStringLiteral("check"), QStringLiteral("Inspector"), QStringLiteral("rename name edit"),
        QStringLiteral("Ctrl+Shift+R"), false});
    if (m_board) registerNameHandler(m_board, m_boardScope);
    if (m_sheet) registerNameHandler(m_sheet, m_sheetScope);
    connect(&m_manager, &DocumentManager::changed, this, &WorkbenchHost::syncCommandScope);
    connect(&m_manager, &DocumentManager::changed, this, &WorkbenchHost::invalidateClosedJobs);
    connect(&m_manager, &DocumentManager::changed, this, &WorkbenchHost::changed);
    connect(&m_jobs, &JobModel::jobFinished, this, &WorkbenchHost::finishAsyncJob);
    if (m_board) m_manager.activate(m_board->documentId());
    syncCommandScope();
}

WorkbenchHost::~WorkbenchHost() = default;

QStringList WorkbenchHost::rowIdsForDocument(const QString &id) const
{
    if (m_board && m_board->documentId() == id) return m_boardRowIds;
    if (m_sheet && m_sheet->documentId() == id) return m_sheetRowIds;
    return {};
}

bool WorkbenchHost::stageNameEdit(const QString &entityId, const QString &value)
{
    auto *session = m_manager.activeSession();
    if (!session || entityId.isEmpty() || !session->document()->containsId(entityId)) return false;
    const QString scope = session == m_board ? m_boardScope : m_sheetScope;
    if (scope.isEmpty() || !m_commandContext.activate(scope)) return false;
    m_pendingEntityId = entityId;
    m_pendingName = value;
    const QStringList views = m_manager.viewsFor(session->documentId());
    return m_commandContext.updateScope(scope,
        {{QStringLiteral("snapshotHeader"), ContractCodec::toQml(session->document()->header())},
         {QStringLiteral("selectionIds"), QStringList{entityId}},
         {QStringLiteral("focusedViewId"), views.isEmpty() ? QString() : views.first()}});
}

bool WorkbenchHost::triggerRename()
{ return m_commands.trigger(QStringLiteral("workbench.rename")); }

bool WorkbenchHost::report(const QString &message, bool ok)
{
    m_workflowMessage = message;
    emit changed();
    return ok;
}

EntityId WorkbenchHost::archiveFor(DocumentSession *session) const
{
    if (session == m_board) return EntityId::parse(QStringLiteral("00000000-0000-0000-0000-000000000303"));
    if (session == m_sheet) return EntityId::parse(QStringLiteral("00000000-0000-0000-0000-000000000304"));
    return {};
}

void WorkbenchHost::bindProjection(DocumentSession *session, const QStringList &ids,
                                   EditableGridModel *grid, QStandardItemModel *assets)
{
    if (!session || !grid || !assets) return;
    PropertyDocument *document = session->document();
    assets->clear();
    assets->setItemRoleNames({{Qt::UserRole + 1, "id"}, {Qt::UserRole + 2, "resourceId"},
        {Qt::UserRole + 3, "label"}, {Qt::UserRole + 4, "caption"}});
    for (const QString &id : ids) {
        if (!document->containsId(id)) continue;
        const QString name = document->valueFor(id, columns[0]).toString();
        const QString state = document->valueFor(id, columns[3]).toString();
        auto *item = new QStandardItem;
        item->setData(id, Qt::UserRole + 1); item->setData(QString(), Qt::UserRole + 2);
        item->setData(name, Qt::UserRole + 3);
        item->setData(name + QStringLiteral(" · ") + state, Qt::UserRole + 4);
        assets->appendRow(item);
    }
    grid->setRowIds(ids); grid->setColumnIds(columns);
    auto source = std::make_unique<DocumentCellSource>(document, grid);
    grid->setSource(source.get()); m_sources.push_back(std::move(source));
    grid->setExternalUndo(true); grid->setReadOnly(false);
    connect(grid, &EditableGridModel::committed, this,
        [this, document, grid](const QString &transactionId, const QString &label, const QVariantList &changes) {
            applyGridCommit(document, grid, transactionId, label, changes);
        });
    connect(document, &PropertyDocument::propertyChanged, this,
        [document, grid, assets, ids](const QString &entityId, const QString &propertyId) {
            const int row = ids.indexOf(entityId), column = columns.indexOf(propertyId);
            if (row < 0 || column < 0) return;
            const int viewRow = grid->rowIndex(entityId);
            if (viewRow >= 0) emit grid->dataChanged(grid->index(viewRow, column), grid->index(viewRow, column));
            if (column == 0) assets->item(row)->setData(document->valueFor(entityId, propertyId), Qt::UserRole + 3);
            if (column == 0 || column == 3)
                assets->item(row)->setData(document->valueFor(entityId, columns[0]).toString()
                    + QStringLiteral(" · ") + document->valueFor(entityId, columns[3]).toString(), Qt::UserRole + 4);
        });
}

void WorkbenchHost::registerNameHandler(DocumentSession *session, const QString &scope)
{
    m_commands.registerHandler(scope, QStringLiteral("workbench.rename"), this,
        [this, session](const CommandInvocationContext &invocation, const QVariantMap &) {
            PropertyDocument *document = session->document();
            const SnapshotHeader current = document->header();
            if (invocation.request.expected.documentId != current.documentId
                || invocation.request.expected.revision != current.revision
                || invocation.request.expected.resetGeneration != current.resetGeneration
                || invocation.selection != QVector<EntityId>{EntityId::parse(m_pendingEntityId)})
                return OperationError{ErrorCode::StaleRevision, QStringLiteral("The selected row changed before rename")};
            if (!m_nameBinding || m_nameBinding->document() != document
                || !m_nameBinding->targetIds().contains(m_pendingEntityId))
                return OperationError{ErrorCode::Unavailable, QStringLiteral("The inspector is not bound to this document row")};
            if (m_pendingName.trimmed().isEmpty())
                return OperationError{ErrorCode::Validation, QStringLiteral("Name cannot be empty")};
            if (!m_nameBinding->begin() || !m_nameBinding->update(m_pendingName.trimmed())) {
                m_nameBinding->cancel();
                return OperationError{ErrorCode::Validation, QStringLiteral("The name edit was rejected")};
            }
            if (!m_nameBinding->commit())
                return OperationError{ErrorCode::Validation, QStringLiteral("The name edit could not commit")};
            return OperationError{};
        });
}

void WorkbenchHost::syncCommandScope()
{
    auto *session = m_manager.activeSession();
    const QString scope = session == m_board ? m_boardScope : session == m_sheet ? m_sheetScope : QString{};
    if (!scope.isEmpty()) m_commandContext.activate(scope);
    m_manager.undoService()->setActiveDocument(session ? session->document() : nullptr);
}

DocumentSession *WorkbenchHost::seed(bool board, EditableGridModel *grid, QStandardItemModel *assets)
{
    auto *session = m_manager.createReferenceDocument();
    if (!session) return nullptr;
    PropertyDocument *document = session->document();
    for (const QString &column : columns)
        if (!document->defineProperty({column, QMetaType::fromType<QString>(), QString(), {}, true, false})) return nullptr;
    if (!document->defineProperty({QStringLiteral("transfers"), QMetaType::fromType<QVariantList>(),
                                   QVariantList{}, {}, true, false})) return nullptr;
    QStringList ids;
    const auto &rows = board ? boardRows : sheetRows;
    for (const Record &row : rows) {
        const QString id = QString::fromLatin1(row.id);
        if (!document->addEntity(EntityId::parse(id), {{columns[0], QString::fromLatin1(row.name)},
            {columns[1], QString::fromLatin1(row.kind)}, {columns[2], QString::fromLatin1(row.owner)},
            {columns[3], QString::fromLatin1(row.state)}})) return nullptr;
        ids.append(id);
    }
    const QString archiveId = board ? QStringLiteral("00000000-0000-0000-0000-000000000303")
                                    : QStringLiteral("00000000-0000-0000-0000-000000000304");
    if (!document->addEntity(EntityId::parse(archiveId), {{QStringLiteral("transfers"), QVariantList{}}})) return nullptr;
    if (board) m_boardRowIds = ids; else m_sheetRowIds = ids;
    bindProjection(session, ids, grid, assets);
    return session;
}

void WorkbenchHost::applyGridCommit(PropertyDocument *document, EditableGridModel *grid,
                                    const QString &transactionId, const QString &label,
                                    const QVariantList &changes)
{
    const auto reject = [&](const QString &reason) {
        grid->applyHistoryChanges(transactionId, changes, true, grid->revision());
        grid->releaseHistoryTransaction(transactionId);
        qWarning().noquote() << "Workbench grid edit rejected:" << reason;
    };
    UndoService *undo = m_manager.undoService();
    if (!document || changes.isEmpty() || m_manager.activeSession() == nullptr
        || m_manager.activeSession()->document() != document || undo->activeDocument() != document) {
        reject(QStringLiteral("Grid edit is not bound to the active document"));
        return;
    }
    for (const QVariant &entry : changes) {
        const QVariantMap change = entry.toMap();
        const QString row = change.value(QStringLiteral("rowId")).toString();
        const QString property = change.value(QStringLiteral("columnId")).toString();
        if (!document->containsId(row) || document->valueFor(row, property) != change.value(QStringLiteral("before"))
            || !grid->source()->validate(row, property, change.value(QStringLiteral("after")).isValid()
                ? change.value(QStringLiteral("after")) : QVariant{}).isEmpty()) {
            reject(QStringLiteral("Cell identity, value, or validation is stale"));
            return;
        }
    }
    if (!undo->beginMacro(label.isEmpty() ? QStringLiteral("Edit grid cells") : label)) {
        reject(QStringLiteral("Could not begin shared undo step"));
        return;
    }
    QString failure;
    for (const QVariant &entry : changes) {
        const QVariantMap change = entry.toMap();
        EditTransaction edit;
        edit.setDocument(document); edit.setUndoService(undo);
        if (!edit.begin({EntityId::parse(change.value(QStringLiteral("rowId")).toString())},
                        change.value(QStringLiteral("columnId")).toString(), label)
            || edit.updateValue(change.value(QStringLiteral("after"))) || edit.commitEdit()) {
            failure = edit.error().message.isEmpty() ? QStringLiteral("Document transaction failed")
                                                     : edit.error().message;
            edit.cancel(); break;
        }
    }
    if (!failure.isEmpty() || !undo->endMacro()) {
        undo->cancelMacro();
        reject(failure.isEmpty() ? QStringLiteral("Could not finish shared undo step") : failure);
        return;
    }
    grid->applyHistoryChanges(transactionId, changes, true, grid->revision());
    grid->releaseHistoryTransaction(transactionId);
}
