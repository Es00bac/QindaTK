// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QDebug>
#include <QPointer>
#include <QStandardItemModel>
#include <QTextStream>
#include <QObject>
#include <qindatk/document_manager.h>
#include <qindatk/editable_grid_model.h>
#include <qindatk/edit_transaction.h>
#include <qindatk/property_document.h>
#include <qindatk/command_context.h>
#include <qindatk/command_registry.h>
#include <qindatk/property_binding.h>
#include <qindatk/shortcut_map.h>
#include <memory>
#include <vector>

using namespace QindaTK;

namespace {
const QStringList columns{QStringLiteral("name"), QStringLiteral("kind"),
                          QStringLiteral("owner"), QStringLiteral("state")};
struct Record {
    const char *id;
    const char *name;
    const char *kind;
    const char *owner;
    const char *state;
};
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
        if (!descriptor || !descriptor->writable)
            return QStringLiteral("Property is not editable");
        const auto error = m_document->validate(EntityId::parse(rowId), columnId, candidate);
        return error ? error.message : QString{};
    }
private:
    QPointer<PropertyDocument> m_document;
};

} // namespace

class WorkbenchHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QindaTK::DocumentManager *manager READ manager CONSTANT)
    Q_PROPERTY(QindaTK::DocumentSession *boardSession READ boardSession CONSTANT)
    Q_PROPERTY(QindaTK::DocumentSession *sheetSession READ sheetSession CONSTANT)
    Q_PROPERTY(QindaTK::EditableGridModel *boardGrid READ boardGrid CONSTANT)
    Q_PROPERTY(QindaTK::EditableGridModel *sheetGrid READ sheetGrid CONSTANT)
    Q_PROPERTY(QAbstractItemModel *boardAssets READ boardAssets CONSTANT)
    Q_PROPERTY(QAbstractItemModel *sheetAssets READ sheetAssets CONSTANT)
    Q_PROPERTY(QindaTK::CommandRegistry *commands READ commands CONSTANT)
    Q_PROPERTY(QindaTK::CommandContext *commandContext READ commandContext CONSTANT)
public:
    explicit WorkbenchHost(QObject *parent = nullptr) : QObject(parent)
    {
        m_manager.setObjectName(QStringLiteral("workbenchDocumentManager"));
        m_boardGrid.setObjectName(QStringLiteral("workbenchBoardGrid"));
        m_sheetGrid.setObjectName(QStringLiteral("workbenchSheetGrid"));
        m_boardAssets.setObjectName(QStringLiteral("workbenchBoardAssets"));
        m_sheetAssets.setObjectName(QStringLiteral("workbenchSheetAssets"));
        m_board = seed(boardRows, &m_boardGrid, &m_boardAssets);
        m_sheet = seed(sheetRows, &m_sheetGrid, &m_sheetAssets);
        m_commands.setContext(&m_commandContext);
        const QString appScope = m_commandContext.pushScope(QStringLiteral("workbench"), this);
        m_boardScope = m_commandContext.pushScope(QStringLiteral("storyboard"), this, appScope);
        m_sheetScope = m_commandContext.pushScope(QStringLiteral("mix-sheet"), this, appScope);
        m_commands.registerCommand({QStringLiteral("workbench.rename"), QStringLiteral("Apply name"),
                                    QStringLiteral("check"), QStringLiteral("Inspector"),
                                    QStringLiteral("rename name edit"), QStringLiteral("Ctrl+Shift+R"), false});
        if (m_board) registerNameHandler(m_board, m_boardScope);
        if (m_sheet) registerNameHandler(m_sheet, m_sheetScope);
        connect(&m_manager, &DocumentManager::changed, this, &WorkbenchHost::syncCommandScope);
        if (m_board) m_manager.activate(m_board->documentId());
        syncCommandScope();
    }
    DocumentManager *manager() { return &m_manager; }
    DocumentSession *boardSession() { return m_board; }
    DocumentSession *sheetSession() { return m_sheet; }
    EditableGridModel *boardGrid() { return &m_boardGrid; }
    EditableGridModel *sheetGrid() { return &m_sheetGrid; }
    QAbstractItemModel *boardAssets() { return &m_boardAssets; }
    QAbstractItemModel *sheetAssets() { return &m_sheetAssets; }
    CommandRegistry *commands() { return &m_commands; }
    CommandContext *commandContext() { return &m_commandContext; }

    Q_INVOKABLE QStringList rowIdsForDocument(const QString &documentId) const
    {
        if (m_board && m_board->documentId() == documentId) return m_boardRowIds;
        if (m_sheet && m_sheet->documentId() == documentId) return m_sheetRowIds;
        return {};
    }

    void setNameBinding(PropertyBinding *binding) { m_nameBinding = binding; }

    Q_INVOKABLE bool stageNameEdit(const QString &entityId, const QString &value)
    {
        auto *session = m_manager.activeSession();
        if (!session || entityId.isEmpty() || !session->document()->containsId(entityId))
            return false;
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

    Q_INVOKABLE bool triggerRename() { return m_commands.trigger(QStringLiteral("workbench.rename")); }

private:
    void registerNameHandler(DocumentSession *session, const QString &scope)
    {
        m_commands.registerHandler(scope, QStringLiteral("workbench.rename"), this,
            [this, session](const CommandInvocationContext &invocation, const QVariantMap &) {
                PropertyDocument *document = session->document();
                const SnapshotHeader current = document->header();
                if (invocation.request.expected.documentId != current.documentId
                    || invocation.request.expected.revision != current.revision
                    || invocation.request.expected.resetGeneration != current.resetGeneration
                    || invocation.selection != QVector<EntityId>{EntityId::parse(m_pendingEntityId)})
                    return OperationError{ErrorCode::StaleRevision,
                                          QStringLiteral("The selected row changed before rename")};
                if (!m_nameBinding || m_nameBinding->document() != document
                    || !m_nameBinding->targetIds().contains(m_pendingEntityId))
                    return OperationError{ErrorCode::Unavailable,
                                          QStringLiteral("The inspector is not bound to this document row")};
                if (m_pendingName.trimmed().isEmpty())
                    return OperationError{ErrorCode::Validation, QStringLiteral("Name cannot be empty")};
                if (!m_nameBinding->begin() || !m_nameBinding->update(m_pendingName.trimmed())) {
                    m_nameBinding->cancel();
                    return OperationError{ErrorCode::Validation,
                                          QStringLiteral("The name edit was rejected")};
                }
                if (!m_nameBinding->commit())
                    return OperationError{ErrorCode::Validation,
                                          QStringLiteral("The name edit could not commit")};
                return OperationError{};
            });
    }

    void syncCommandScope()
    {
        auto *session = m_manager.activeSession();
        const QString scope = session == m_board ? m_boardScope
                              : session == m_sheet ? m_sheetScope : QString{};
        if (!scope.isEmpty()) m_commandContext.activate(scope);
        m_manager.undoService()->setActiveDocument(session ? session->document() : nullptr);
    }

    DocumentSession *seed(const QVector<Record> &rows, EditableGridModel *grid,
                          QStandardItemModel *assets)
    {
        auto *session = m_manager.createReferenceDocument();
        if (!session) return nullptr;
        PropertyDocument *document = session->document();
        for (const QString &column : columns)
            if (!document->defineProperty({column, QMetaType::fromType<QString>(),
                                           QString(), {}, true, false})) return nullptr;
        QStringList ids;
        assets->setItemRoleNames({{Qt::UserRole + 1, "id"},
                                 {Qt::UserRole + 2, "resourceId"},
                                 {Qt::UserRole + 3, "label"},
                                 {Qt::UserRole + 4, "caption"}});
        for (const Record &row : rows) {
            const QString id = QString::fromLatin1(row.id);
            const QString name = QString::fromLatin1(row.name);
            const QString kind = QString::fromLatin1(row.kind);
            const QString state = QString::fromLatin1(row.state);
            if (!document->addEntity(EntityId::parse(id),
                                     {{columns[0], name}, {columns[1], kind},
                                      {columns[2], QString::fromLatin1(row.owner)},
                                      {columns[3], state}})) return nullptr;
            ids.append(id);
            auto *item = new QStandardItem;
            item->setData(id, Qt::UserRole + 1);
            item->setData(QString(), Qt::UserRole + 2);
            item->setData(name, Qt::UserRole + 3);
            item->setData(name + QStringLiteral(" · ") + state, Qt::UserRole + 4);
            assets->appendRow(item);
        }
        if (grid == &m_boardGrid) m_boardRowIds = ids;
        else if (grid == &m_sheetGrid) m_sheetRowIds = ids;
        grid->setRowIds(ids);
        grid->setColumnIds(columns);
        auto source = std::make_unique<DocumentCellSource>(document, grid);
        grid->setSource(source.get());
        m_sources.push_back(std::move(source));
        grid->setExternalUndo(true);
        grid->setReadOnly(false);
        connect(grid, &EditableGridModel::committed, this,
                [this, document, grid](const QString &transactionId, const QString &label,
                                       const QVariantList &changes) {
            applyGridCommit(document, grid, transactionId, label, changes);
        });
        connect(document, &PropertyDocument::propertyChanged, this,
                [document, grid, assets, ids](const QString &entityId, const QString &propertyId) {
            const int row = ids.indexOf(entityId);
            const int column = columns.indexOf(propertyId);
            if (row < 0 || column < 0) return;
            const int viewRow = grid->rowIndex(entityId);
            if (viewRow >= 0)
                emit grid->dataChanged(grid->index(viewRow, column), grid->index(viewRow, column));
            if (column == 0) assets->item(row)->setData(document->valueFor(entityId, propertyId),
                                                      Qt::UserRole + 3);
            if (column == 0 || column == 3)
                assets->item(row)->setData(document->valueFor(entityId, columns[0]).toString()
                                              + QStringLiteral(" · ")
                                              + document->valueFor(entityId, columns[3]).toString(),
                                          Qt::UserRole + 4);
        });
        return session;
    }

    void applyGridCommit(PropertyDocument *document, EditableGridModel *grid,
                         const QString &transactionId, const QString &label,
                         const QVariantList &changes)
    {
        const auto rejectGridEdit = [&](const QString &reason) {
            grid->applyHistoryChanges(transactionId, changes, true, grid->revision());
            grid->releaseHistoryTransaction(transactionId);
            qWarning().noquote() << "Workbench grid edit rejected:" << reason;
        };
        if (!document || changes.isEmpty()) {
            rejectGridEdit(QStringLiteral("No document cell changes"));
            return;
        }
        DocumentSession *active = m_manager.activeSession();
        UndoService *undo = m_manager.undoService();
        if (!active || active->document() != document || undo->activeDocument() != document) {
            rejectGridEdit(QStringLiteral("Only the active document can accept grid edits"));
            return;
        }

        for (const QVariant &entry : changes) {
            const QVariantMap change = entry.toMap();
            const QString rowId = change.value(QStringLiteral("rowId")).toString();
            const QString propertyId = change.value(QStringLiteral("columnId")).toString();
            const EntityId target = EntityId::parse(rowId);
            if (!target.isValid() || !document->contains(target)
                || document->valueFor(rowId, propertyId) != change.value(QStringLiteral("before"))) {
                rejectGridEdit(QStringLiteral("Cell identity or document value is stale"));
                return;
            }
            const QString validation = grid->source()->validate(
                rowId, propertyId, change.value(QStringLiteral("after")));
            if (!validation.isEmpty()) {
                rejectGridEdit(validation);
                return;
            }
        }

        if (!undo->beginMacro(label.isEmpty() ? QStringLiteral("Edit grid cells") : label)) {
            rejectGridEdit(QStringLiteral("Could not begin shared grid undo step"));
            return;
        }
        QString failure;
        for (const QVariant &entry : changes) {
            const QVariantMap change = entry.toMap();
            EditTransaction edit;
            edit.setDocument(document);
            edit.setUndoService(undo);
            const EntityId target = EntityId::parse(change.value(QStringLiteral("rowId")).toString());
            if (!edit.begin({target}, change.value(QStringLiteral("columnId")).toString(), label)
                || edit.updateValue(change.value(QStringLiteral("after")))
                || edit.commitEdit()) {
                failure = edit.error().message.isEmpty()
                    ? QStringLiteral("The authoritative document transaction failed")
                    : edit.error().message;
                edit.cancel();
                break;
            }
        }
        if (!failure.isEmpty()) {
            if (!undo->cancelMacro())
                qWarning().noquote() << "Workbench could not cancel failed grid edit:" << undo->lastError();
            rejectGridEdit(failure);
            return;
        }
        if (!undo->endMacro()) {
            qWarning().noquote() << "Workbench could not record shared grid undo step:" << undo->lastError();
            rejectGridEdit(QStringLiteral("Could not finish shared grid undo step"));
            return;
        }

        if (!grid->applyHistoryChanges(transactionId, changes, true, grid->revision())) {
            qWarning().noquote() << "Workbench could not release grid cell projection:" << grid->lastError();
        }
        grid->releaseHistoryTransaction(transactionId);
    }

    DocumentManager m_manager;
    DocumentSession *m_board = nullptr;
    DocumentSession *m_sheet = nullptr;
    EditableGridModel m_boardGrid;
    EditableGridModel m_sheetGrid;
    QStandardItemModel m_boardAssets;
    QStandardItemModel m_sheetAssets;
    QStringList m_boardRowIds;
    QStringList m_sheetRowIds;
    CommandContext m_commandContext;
    CommandRegistry m_commands;
    QString m_boardScope;
    QString m_sheetScope;
    QString m_pendingEntityId;
    QString m_pendingName;
    QPointer<PropertyBinding> m_nameBinding;
    std::vector<std::unique_ptr<GridCellSource>> m_sources;
};
