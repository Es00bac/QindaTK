// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "undo_service.h"
#include <QPointer>
#include <QStringList>
#include <functional>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// One semantic user gesture, regardless of how many preview updates arrive.
// The document keeps previews transient; only commit enters authoritative
// storage and the undo history. A controller calls cancel on Escape/grab loss.
class EditTransaction : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PropertyDocument *document READ document WRITE setDocument NOTIFY configurationChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService WRITE setUndoService NOTIFY configurationChanged)
    Q_PROPERTY(QStringList targetIds READ targetIds WRITE setTargetIds NOTIFY configurationChanged)
    Q_PROPERTY(QString propertyId READ propertyId WRITE setPropertyId NOTIFY configurationChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY configurationChanged)
    Q_PROPERTY(QString mergeKey READ mergeKey WRITE setMergeKey NOTIFY configurationChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(QString transactionId READ transactionId NOTIFY stateChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(int errorCode READ errorCode NOTIFY stateChanged)
public:
    explicit EditTransaction(QObject *parent = nullptr) : QObject(parent) {}
    ~EditTransaction() override { cancel(); }
    PropertyDocument *document() const { return m_document; }
    void setDocument(PropertyDocument *document);
    UndoService *undoService() const { return m_undo; }
    void setUndoService(UndoService *undo);
    QStringList targetIds() const { return m_targetIds; }
    void setTargetIds(const QStringList &ids);
    QString propertyId() const { return m_propertyId; }
    void setPropertyId(const QString &propertyId);
    QString label() const { return m_label; }
    void setLabel(const QString &label);
    QString mergeKey() const { return m_mergeKey; }
    void setMergeKey(const QString &mergeKey);
    bool active() const { return m_active; }
    QString transactionId() const { return m_active ? m_id.toString(QUuid::WithoutBraces) : QString{}; }
    QString errorMessage() const { return m_error.message; }
    int errorCode() const { return static_cast<int>(m_error.code); }
    OperationError error() const { return m_error; }
    void setValidator(std::function<OperationError(const QVector<PropertyChange> &)> validator);
    bool begin(const QVector<EntityId> &targets, const QString &propertyId,
               const QString &label, const QString &mergeKey = {});
    OperationError updateValue(const QVariant &candidate);
    OperationError updateComponent(int componentIndex, const QVariant &candidateComponent);
    bool hasEffectiveChanges() const;
    OperationError commitEdit();
    Q_INVOKABLE bool begin();
    Q_INVOKABLE bool update(const QVariant &candidate) { return !updateValue(candidate); }
    Q_INVOKABLE bool commit() { return !commitEdit(); }
    Q_INVOKABLE void cancel();
signals:
    void configurationChanged();
    void stateChanged();
    void committed(const QString &transactionId);
    void canceled(const QString &transactionId);
    void rejected(const QString &message, int code);
private:
    OperationError reject(OperationError error, bool end);
    void finish(bool emitCancel);
    QPointer<PropertyDocument> m_document;
    QPointer<UndoService> m_undo;
    QStringList m_targetIds;
    QString m_propertyId;
    QString m_label;
    QString m_mergeKey;
    std::function<OperationError(const QVector<PropertyChange> &)> m_validator;
    QUuid m_id;
    SnapshotHeader m_base;
    QVector<PropertyChange> m_changes;
    OperationError m_error;
    bool m_active = false;
};

} // namespace QindaTK
