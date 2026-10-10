// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_model.h"

#include <QJsonObject>
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class LocalDatabaseProvider;
class UndoService;
class LocalDataDesignCodec;

// A form and report design as a session document (schema
// qindatk.local-data-design.v1, docs/forms-reports.md#design-document).
// The state is the design map LocalDataDesignStore validates: a stable
// SQLite binding (table, fields, their types when bound), the form layout
// and the report definition.
// AGENT-CONTRACT (docs/document-models.md#commands): every edit is one
// bounded whole-design DocumentCommand recorded by UndoService; a standalone
// document keeps a private history that a DocumentSession adopts, so save,
// autosave and recovery come from DocumentManager unchanged. An edit that
// would make the design structurally invalid is refused with lastError and
// changes nothing.
class LocalDataDesignDocument : public QObject, public DocumentModel {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("undo_service.h")
    Q_MOC_INCLUDE("local_database_provider.h")
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantMap design READ design NOTIFY changed)
    Q_PROPERTY(QVariantMap form READ form NOTIFY changed)
    Q_PROPERTY(QVariantMap report READ report NOTIFY changed)
    Q_PROPERTY(QString tableName READ tableName NOTIFY changed)
    Q_PROPERTY(QStringList boundFields READ boundFields NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    static constexpr int HistorySteps = 128;
    static constexpr qsizetype MaximumBytes = qsizetype(4) * 1024 * 1024;
    explicit LocalDataDesignDocument(QObject *parent = nullptr);
    ~LocalDataDesignDocument() override;

    const QJsonObject &state() const { return m_design; }
    QString documentId() const { return m_documentId; }
    QString revision() const { return QString::number(m_revision); }
    QVariantMap design() const { return m_design.toVariantMap(); }
    QVariantMap form() const;
    // The report definition LocalReportDocumentModel::build takes: tableName
    // and, when the design names no columns, every bound field.
    QVariantMap report() const;
    QString tableName() const;
    QStringList boundFields() const;
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    UndoService *undoService() const;
    QString lastError() const { return m_error; }

    // --- edits: each is one undo step, or false with lastError
    Q_INVOKABLE bool setDesign(const QVariantMap &design);
    // Binds to `table` and records the live column types; `fields` empty
    // binds every reportable column. Form fields, report columns, groups,
    // totals and a filter that name a field no longer bound are dropped.
    Q_INVOKABLE bool bindTable(QindaTK::LocalDatabaseProvider *provider, const QString &table,
                               const QStringList &fields = {});
    Q_INVOKABLE bool setFormTitle(const QString &title);
    Q_INVOKABLE bool setFormFieldLabel(const QString &field, const QString &label);
    Q_INVOKABLE bool setFormFieldHidden(const QString &field, bool hidden);
    Q_INVOKABLE bool moveFormField(const QString &field, int index);
    // Form controls (D-311, local_data_design_controls.cpp): `control` is
    // {type, ...properties}; missing properties take the type's defaults, a
    // missing id or name is generated. Returns the new control's id, or ""
    // with lastError. Each call is one undo step.
    Q_INVOKABLE QString addFormControl(const QVariantMap &control);
    // Merges `changes` into one control (an undefined value removes the
    // property); geometry-only changes are labelled "Move or size".
    Q_INVOKABLE bool setFormControl(const QString &controlId, const QVariantMap &changes);
    // Removes a control, and the controls on its pages for a tab control.
    Q_INVOKABLE bool removeFormControl(const QString &controlId);
    Q_INVOKABLE bool setFormControls(const QVariantList &controls);
    // key: headerText or footerText
    Q_INVOKABLE bool setReportText(const QString &key, const QString &text);
    Q_INVOKABLE bool setReportColumns(const QStringList &fields);
    Q_INVOKABLE bool setReportDetail(bool detail);
    // [{field, label, pageBreakBefore, totals: [{field, function}]}], outermost first
    Q_INVOKABLE bool setReportGroups(const QVariantList &groups);
    // groupField empty: the report footer. function: a WP-029 aggregate name.
    Q_INVOKABLE bool setReportTotal(const QString &groupField, const QString &field,
                                    const QString &function, bool enabled);
    Q_INVOKABLE bool setGroupPageBreak(const QString &groupField, bool pageBreakBefore);
    // WP-029 predicate JSON and typed parameters ({id: {type, value}}).
    Q_INVOKABLE bool setReportFilter(const QVariantMap &filter, const QVariantMap &parameters);
    Q_INVOKABLE bool setReportPage(const QString &paperId, const QString &orientation);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // LocalDataDesignStore::validate against the live schema.
    Q_INVOKABLE QVariantMap validate(QindaTK::LocalDatabaseProvider *provider) const;
    Q_INVOKABLE QString toJson() const;

    // DocumentModel
    QObject *modelObject() const override { return const_cast<LocalDataDesignDocument *>(this); }
    DocumentId modelId() const override { return DocumentId::parse(m_documentId); }
    QString modelKind() const override { return QStringLiteral("data-design"); }
    SnapshotHeader modelHeader() const override;
    QMetaObject::Connection connectModelChanged(QObject *receiver, std::function<void()> slot) override;
    OperationError prepareCommand(const DocumentCommand &command, bool forward) const override;
    void publishCommand(const DocumentCommand &command, bool forward) override;
    DocumentCommandPtr composeCommands(const DocumentCommand &first,
                                       const DocumentCommand &second) const override;

    static QString schemaId() { return QStringLiteral("qindatk.local-data-design.v1"); }
    static QJsonObject emptyDesign();
    // Pure codec shared by the DocumentCodec and the tests. A version 1 file
    // (no session header) opens with an identity derived from its designId.
    static QByteArray encodeState(const QJsonObject &design, const SnapshotHeader &header,
                                  const QJsonObject &extensions);
    static bool decodeState(const QByteArray &bytes, QJsonObject *design, SnapshotHeader *header,
                            QJsonObject *extensions, QString *problem);

signals:
    void changed();
    void historyChanged();
    void errorChanged();

private:
    friend class LocalDataDesignCodec;
    struct DesignCommand;
    void historyAttached(UndoService *service) override;
    UndoService *history() const;
    bool commit(const QVariantMap &after, const QString &label);
    bool fail(const QString &message);
    void adoptLoaded(QJsonObject design, const SnapshotHeader &header);
    static void migrateLegacyReport(QVariantMap *report, const QVariantMap &types);
    static int indexOfField(const QVariantList &list, const QString &key, const QString &field);
    QVariantMap editableReport(QVariantMap *design) const;

    QJsonObject m_design;
    QString m_documentId;
    quint64 m_revision = 1;
    quint64 m_resetGeneration = 0;
    UndoService *m_localHistory = nullptr;
    QMetaObject::Connection m_historyConnection;
    QString m_error;
};

} // namespace QindaTK
