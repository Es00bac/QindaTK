// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_model.h"
#include "property_document.h"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Per-document histories keyed by document identity, behind one
// active-document facade. Every model kind records DocumentCommand steps
// through the same engine; a structural removal/reset invalidates the
// affected history, so inverse data never resolves to a replacement row,
// recycled delegate or recreated object.
class UndoService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PropertyDocument *activeDocument READ activeDocument WRITE setActiveDocument NOTIFY changed)
    Q_PROPERTY(QObject *activeModel READ activeModelObject WRITE setActiveModelObject NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
    Q_PROPERTY(QString redoText READ redoText NOTIFY changed)
    Q_PROPERTY(bool clean READ isClean NOTIFY changed)
    Q_PROPERTY(QString cleanRevision READ cleanRevision NOTIFY changed)
    Q_PROPERTY(QString memoryLimit READ memoryLimitString WRITE setMemoryLimitString NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QVariantList historyEntries READ historyEntries NOTIFY changed)
    Q_PROPERTY(int historyIndex READ historyIndex NOTIFY changed)
    Q_PROPERTY(int cleanIndex READ cleanIndex NOTIFY changed)
public:
    // Identifies the history position a save captured, so a late receipt can
    // mark exactly that position clean (or nothing) instead of the cursor.
    struct CleanMark {
        QString stepId;
        quint64 stepMerges = 0;
        quint64 epoch = 0;
        qsizetype evicted = 0;
        bool valid = false;
    };

    explicit UndoService(QObject *parent = nullptr) : QObject(parent) {}
    ~UndoService() override;

    // Reference PropertyDocument API (source compatible).
    bool addDocument(PropertyDocument *document) { return addModel(document); }
    bool removeDocument(PropertyDocument *document) { return removeModel(document); }
    PropertyDocument *activeDocument() const;
    void setActiveDocument(PropertyDocument *document) { setActiveModel(document); }
    bool isClean(PropertyDocument *document) const { return isModelClean(document); }
    void markClean(PropertyDocument *document) { markModelClean(document); }

    // Generic model API. addModel refuses a model whose live history belongs
    // to another service; adoptModel moves that history here instead.
    bool addModel(DocumentModel *model);
    bool adoptModel(DocumentModel *model);
    bool removeModel(DocumentModel *model);
    [[nodiscard]] bool hasModel(const DocumentModel *model) const;
    DocumentModel *activeModel() const;
    void setActiveModel(DocumentModel *model);
    QObject *activeModelObject() const;
    void setActiveModelObject(QObject *object);
    // Validates `command` (no mutation on failure), records it, then publishes
    // it through the model. Rejections leave model, revision, cursor and
    // notifications untouched and are returned, not emitted.
    OperationError commit(DocumentModel *model, const QString &label,
                          const QString &mergeKey, DocumentCommandPtr command);
    OperationError undo(DocumentModel *model) { return step(model, false); }
    OperationError redo(DocumentModel *model) { return step(model, true); }
    [[nodiscard]] bool canUndo(const DocumentModel *model) const;
    [[nodiscard]] bool canRedo(const DocumentModel *model) const;
    [[nodiscard]] QString undoText(const DocumentModel *model) const;
    [[nodiscard]] QString redoText(const DocumentModel *model) const;
    [[nodiscard]] bool isModelClean(const DocumentModel *model) const;
    void markModelClean(DocumentModel *model);
    [[nodiscard]] CleanMark cleanMark(const DocumentModel *model) const;
    void markCleanAt(DocumentModel *model, const CleanMark &mark, quint64 revision);
    [[nodiscard]] int stepLimit(const DocumentModel *model) const;
    bool setStepLimit(DocumentModel *model, int steps); // 0 = unlimited
    [[nodiscard]] int stepCount(const DocumentModel *model) const;
    [[nodiscard]] quint64 retainedBytes(const DocumentModel *model) const;

    // Active-document facade used by QML chrome.
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool isClean() const;
    QString cleanRevision() const;
    quint64 memoryLimit() const { return m_memoryLimit; }
    QString memoryLimitString() const { return QString::number(m_memoryLimit); }
    void setMemoryLimit(quint64 bytes);
    void setMemoryLimitString(const QString &bytes);
    QString lastError() const { return m_lastError; }
    QVariantList historyEntries() const;
    int historyIndex() const;
    int cleanIndex() const;
    Q_INVOKABLE bool navigateToHistoryIndex(int index);
    Q_INVOKABLE bool navigateToHistoryEntry(const QString &entryId);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE void markClean();
    Q_INVOKABLE bool beginMacro(const QString &label);
    Q_INVOKABLE bool endMacro();
    Q_INVOKABLE bool cancelMacro();
    Q_INVOKABLE bool addDocumentObject(PropertyDocument *document) { return addDocument(document); }
signals:
    void changed();
    void rejected(const QString &message);
    // One document's history, cursor or clean position changed.
    void historyChanged(const QString &documentId);
private:
    friend class DocumentModel;
    friend class EditTransaction;
    struct Step {
        QString id;       // stable across merges (history navigation)
        QString label;
        QString mergeKey;
        DocumentCommandPtr command;
        quint64 bytes = 0;
        quint64 merges = 0; // bumps when a merge changes the step's content
    };
    struct History {
        DocumentModel *model = nullptr;
        QPointer<QObject> object;
        QVector<Step> steps;
        qsizetype cursor = 0;
        qsizetype cleanCursor = 0; // -1 when the saved state is unreachable
        quint64 cleanRevision = 0;
        quint64 epoch = 0;         // bumps when positions stop mapping to content
        qsizetype evicted = 0;     // steps dropped from the front in this epoch
        int stepLimit = 0;
        QString macroLabel;
        DocumentCommandPtr macroCommand;
        bool busy = false;         // publishing; nested operations are refused
        QMetaObject::Connection destroyed;
    };
    History *historyFor(const DocumentModel *model);
    const History *historyFor(const DocumentModel *model) const;
    const History *activeHistory() const;
    void attach(DocumentModel *model, History state);
    bool release(DocumentModel *model, History *state, bool notifyNow);
    void forget(const QString &key, const DocumentModel *model, bool notifyNow);
    void invalidateKey(const QString &key);
    void invalidateCleanKey(const QString &key);
    OperationError step(DocumentModel *model, bool forward);
    OperationError canRecord(DocumentModel *model, const DocumentCommand &command) const;
    void recordStep(History &state, DocumentModel *model, const QString &label,
                    const QString &mergeKey, const DocumentCommandPtr &command);
    void enforceLimits(History &state);
    bool record(PropertyDocument *document, const QString &label, const QString &mergeKey,
                const DocumentCommandPtr &command);
    void notify(const QString &key);
    bool fail(const QString &message);
    QHash<QString, History> m_histories;
    QString m_activeKey;
    QPointer<QObject> m_activeObject;
    quint64 m_memoryLimit = 8 * 1024 * 1024;
    QString m_lastError;
};

} // namespace QindaTK
