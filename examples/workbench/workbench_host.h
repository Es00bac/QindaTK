// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QQuickWindow>
#include <QStandardItemModel>
#include <qindatk/command_context.h>
#include <qindatk/command_registry.h>
#include <qindatk/document_manager.h>
#include <qindatk/editable_grid_model.h>
#include <qindatk/job_model.h>
#include <qindatk/property_binding.h>
#include <qindatk/workspace_session.h>
#include <qindatk/dock_model.h>
#include <memory>
#include <vector>

using namespace QindaTK;

class WorkbenchHost : public QObject {
    Q_OBJECT
    Q_PROPERTY(QindaTK::DocumentManager *manager READ manager CONSTANT)
    Q_PROPERTY(QindaTK::DocumentSession *boardSession READ boardSession NOTIFY changed)
    Q_PROPERTY(QindaTK::DocumentSession *sheetSession READ sheetSession NOTIFY changed)
    Q_PROPERTY(QindaTK::EditableGridModel *boardGrid READ boardGrid CONSTANT)
    Q_PROPERTY(QindaTK::EditableGridModel *sheetGrid READ sheetGrid CONSTANT)
    Q_PROPERTY(QAbstractItemModel *boardAssets READ boardAssets CONSTANT)
    Q_PROPERTY(QAbstractItemModel *sheetAssets READ sheetAssets CONSTANT)
    Q_PROPERTY(QindaTK::CommandRegistry *commands READ commands CONSTANT)
    Q_PROPERTY(QindaTK::CommandContext *commandContext READ commandContext CONSTANT)
    Q_PROPERTY(QindaTK::WorkspaceSession *workspace READ workspace CONSTANT)
    Q_PROPERTY(QindaTK::DockModel *dock READ dock CONSTANT)
    Q_PROPERTY(QindaTK::JobModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(QString workflowMessage READ workflowMessage NOTIFY changed)
    Q_PROPERTY(QString lastJobMessage READ lastJobMessage NOTIFY changed)
public:
    explicit WorkbenchHost(QObject *parent = nullptr);
    ~WorkbenchHost() override;
    DocumentManager *manager() { return &m_manager; }
    DocumentSession *boardSession() { return m_board; }
    DocumentSession *sheetSession() { return m_sheet; }
    EditableGridModel *boardGrid() { return &m_boardGrid; }
    EditableGridModel *sheetGrid() { return &m_sheetGrid; }
    QAbstractItemModel *boardAssets() { return &m_boardAssets; }
    QAbstractItemModel *sheetAssets() { return &m_sheetAssets; }
    CommandRegistry *commands() { return &m_commands; }
    CommandContext *commandContext() { return &m_commandContext; }
    WorkspaceSession *workspace() { return &m_workspace; }
    DockModel *dock() { return &m_dock; }
    JobModel *jobs() { return &m_jobs; }
    QString workflowMessage() const { return m_workflowMessage; }
    QString lastJobMessage() const { return m_lastJobMessage; }
    void setWindow(QQuickWindow *window) { m_window = window; }
    void setNameBinding(PropertyBinding *binding) { m_nameBinding = binding; }
    Q_INVOKABLE QStringList rowIdsForDocument(const QString &documentId) const;
    Q_INVOKABLE bool stageNameEdit(const QString &entityId, const QString &value);
    Q_INVOKABLE bool triggerRename();
    Q_INVOKABLE bool openReferencePath(const QString &path);
    Q_INVOKABLE bool saveWorkspace(const QString &path);
    Q_INVOKABLE bool restoreWorkspace(const QString &path);
    Q_INVOKABLE bool restoreRecovery(const QString &documentId, const QString &originalPath);
    Q_INVOKABLE bool copyAsset(const QString &rowId);
    Q_INVOKABLE bool pasteAssetToOtherDocument();
    Q_INVOKABLE bool startCloseDuringJob();
signals:
    void changed();
private:
    bool report(const QString &message, bool ok);
    EntityId archiveFor(DocumentSession *session) const;
    void invalidateClosedJobs();
    void finishAsyncJob(const QString &jobId, JobModel::JobState state,
                        const QVariantMap &result, const QString &error);
    void bindProjection(DocumentSession *session, const QStringList &ids,
                        EditableGridModel *grid, QStandardItemModel *assets);
    void registerNameHandler(DocumentSession *session, const QString &scope);
    void syncCommandScope();
    DocumentSession *seed(bool board, EditableGridModel *grid, QStandardItemModel *assets);
    void applyGridCommit(PropertyDocument *document, EditableGridModel *grid,
                         const QString &transactionId, const QString &label,
                         const QVariantList &changes);

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
    WorkspaceSession m_workspace;
    DockModel m_dock;
    JobModel m_jobs;
    QPointer<QQuickWindow> m_window;
    QHash<QString, QString> m_jobDocuments;
    QString m_workflowMessage;
    QString m_lastJobMessage;
    QByteArray m_transferBytes;
};
