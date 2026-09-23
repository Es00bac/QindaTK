// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "edit_transaction.h"
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// QML-facing projection for one property over a stable set of entity IDs.
// Owner refreshes only publish state; interaction is explicitly routed through
// an EditTransaction and reported separately by userEditAccepted.
class PropertyBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::PropertyDocument *document READ document WRITE setDocument NOTIFY configurationChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService WRITE setUndoService NOTIFY configurationChanged)
    Q_PROPERTY(QString propertyId READ propertyId WRITE setPropertyId NOTIFY configurationChanged)
    Q_PROPERTY(QStringList targetIds READ targetIds WRITE setTargetIds NOTIFY configurationChanged)
    Q_PROPERTY(QVariantMap descriptor READ descriptor NOTIFY changed)
    Q_PROPERTY(QVariant value READ value NOTIFY changed)
    Q_PROPERTY(QVariant defaultValue READ defaultValue NOTIFY changed)
    Q_PROPERTY(bool mixed READ mixed NOTIFY changed)
    Q_PROPERTY(bool writable READ writable NOTIFY changed)
    Q_PROPERTY(bool animated READ animated NOTIFY changed)
    Q_PROPERTY(QString validationError READ validationError NOTIFY changed)
    Q_PROPERTY(QString availabilityError READ availabilityError NOTIFY changed)
    // AGENT-NOTE: JavaScript Number cannot preserve every 64-bit integer.
    // For LongLong/ULongLong descriptors, read exactIntegerValue and submit
    // edits with updateExactInteger instead of round-tripping value through JS.
    Q_PROPERTY(bool requiresExactString READ requiresExactString NOTIFY changed)
    Q_PROPERTY(QString exactIntegerValue READ exactIntegerValue NOTIFY changed)
    Q_PROPERTY(QString exactIntegerDefault READ exactIntegerDefault NOTIFY changed)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
public:
    explicit PropertyBinding(QObject *parent = nullptr);
    ~PropertyBinding() override;

    PropertyDocument *document() const { return m_document; }
    void setDocument(PropertyDocument *document);
    UndoService *undoService() const { return m_undo; }
    void setUndoService(UndoService *undo);
    QString propertyId() const { return m_propertyId; }
    void setPropertyId(const QString &id);
    QStringList targetIds() const { return m_targetIds; }
    void setTargetIds(const QStringList &ids);

    QVariantMap descriptor() const { return m_descriptor; }
    QVariant value() const { return m_value; }
    QVariant defaultValue() const { return m_defaultValue; }
    bool mixed() const { return m_mixed; }
    bool writable() const { return m_writable; }
    bool animated() const { return m_animated; }
    QString validationError() const { return m_validationError; }
    QString availabilityError() const { return m_availabilityError; }
    bool requiresExactString() const { return m_requiresExactString; }
    QString exactIntegerValue() const { return m_exactIntegerValue; }
    QString exactIntegerDefault() const { return m_exactIntegerDefault; }
    bool editing() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool begin();
    Q_INVOKABLE bool update(const QVariant &candidate);
    Q_INVOKABLE bool updateComponent(int componentIndex, const QVariant &candidateComponent);
    Q_INVOKABLE bool updateExactInteger(const QString &decimal);
    Q_INVOKABLE bool commit();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool reset();
signals:
    void configurationChanged();
    void changed();
    void userEditAccepted(const QString &propertyId, const QStringList &targetIds,
                          const QVariant &value);
    void userEditRejected(const QString &message, int code);
private:
    void configureTransaction();
    void setFailure(const QString &message);
    QVariantMap descriptorMap(const PropertyDescriptor &entry) const;
    QPointer<PropertyDocument> m_document;
    QPointer<UndoService> m_undo;
    QString m_propertyId;
    QStringList m_targetIds;
    EditTransaction *m_transaction = nullptr;
    QVariantMap m_descriptor;
    QVariant m_value;
    QVariant m_defaultValue;
    QString m_validationError;
    QString m_availabilityError;
    QString m_exactIntegerValue;
    QString m_exactIntegerDefault;
    QVariant m_pendingValue;
    QVariant m_originalValue;
    bool m_mixed = false;
    bool m_originalMixed = false;
    bool m_hasPendingValue = false;
    bool m_writable = false;
    bool m_animated = false;
    bool m_requiresExactString = false;
};

} // namespace QindaTK
