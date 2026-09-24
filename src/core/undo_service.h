// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "property_document.h"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Per-document histories share one active-document facade. A structural
// removal/reset invalidates the affected history, so inverse data never
// resolves to a replacement row or recycled delegate.
class UndoService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PropertyDocument *activeDocument READ activeDocument WRITE setActiveDocument NOTIFY changed)
    // A QObject that is an UndoParticipant; while set, undo/redo/clean state
    // route to it instead of activeDocument (docs/editing.md#history-participants).
    Q_PROPERTY(QObject *activeParticipant READ activeParticipant WRITE setActiveParticipant NOTIFY changed)
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
    explicit UndoService(QObject *parent = nullptr) : QObject(parent) {}
    bool addDocument(PropertyDocument *document);
    bool removeDocument(PropertyDocument *document);
    PropertyDocument *activeDocument() const { return m_active; }
    void setActiveDocument(PropertyDocument *document);
    QObject *activeParticipant() const { return m_participantObject; }
    void setActiveParticipant(QObject *participant);
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    bool isClean() const;
    bool isClean(PropertyDocument *document) const;
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
    void markClean(PropertyDocument *document);
    Q_INVOKABLE bool beginMacro(const QString &label);
    Q_INVOKABLE bool endMacro();
    Q_INVOKABLE bool cancelMacro();
    Q_INVOKABLE bool addDocumentObject(PropertyDocument *document) { return addDocument(document); }
signals:
    void changed();
    void rejected(const QString &message);
private:
    friend class EditTransaction;
    struct Step {
        QString id;
        QString label;
        QString mergeKey;
        QVector<PropertyChange> changes;
        quint64 bytes = 0;
    };
    struct History {
        QPointer<PropertyDocument> document;
        QVector<Step> steps;
        qsizetype cursor = 0;
        qsizetype cleanCursor = 0; // -1 when the saved state was discarded
        quint64 cleanRevision = 0;
        QString macroLabel;
        QVector<PropertyChange> macroChanges;
    };
    static quint64 cost(const QVector<PropertyChange> &changes);
    bool canRecord(PropertyDocument *document, const QVector<PropertyChange> &changes) const;
    bool record(PropertyDocument *document, const QString &label,
                const QString &mergeKey, const QVector<PropertyChange> &changes);
    void clearDocument(const QString &id);
    History *history(PropertyDocument *document);
    const History *activeHistory() const;
    bool fail(const QString &message);
    UndoParticipant *participant() const;
    QHash<QString, History> m_histories;
    QPointer<PropertyDocument> m_active;
    QPointer<QObject> m_participantObject;
    QMetaObject::Connection m_participantConnection;
    quint64 m_memoryLimit = 8 * 1024 * 1024;
    QString m_lastError;
};

} // namespace QindaTK
