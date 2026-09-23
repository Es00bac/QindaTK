// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_binding.h"

#include <QMetaType>
#include <QSet>

namespace QindaTK {
namespace {

bool exactIntegerType(QMetaType type)
{
    return type.id() == QMetaType::LongLong || type.id() == QMetaType::ULongLong;
}

bool parseExactInteger(QMetaType type, const QString &text, QVariant *result)
{
    if (type.id() == QMetaType::LongLong) {
        bool ok = false;
        const qlonglong parsed = text.toLongLong(&ok, 10);
        if (!ok || QString::number(parsed) != text) return false;
        *result = QVariant::fromValue(parsed);
        return true;
    }
    if (type.id() == QMetaType::ULongLong) {
        bool ok = false;
        const qulonglong parsed = text.toULongLong(&ok, 10);
        if (!ok || QString::number(parsed) != text) return false;
        *result = QVariant::fromValue(parsed);
        return true;
    }
    return false;
}

}

PropertyBinding::PropertyBinding(QObject *parent)
    : QObject(parent), m_transaction(new EditTransaction(this))
{
    connect(m_transaction, &EditTransaction::committed, this, [this] {
        const QVariant accepted = m_pendingValue;
        const bool changedByUser = m_hasPendingValue
            && (m_originalMixed || accepted != m_originalValue);
        const QString property = m_propertyId;
        const QStringList targets = m_targetIds;
        m_pendingValue.clear();
        m_originalValue.clear();
        m_hasPendingValue = false;
        refresh();
        if (changedByUser) emit userEditAccepted(property, targets, accepted);
    });
    connect(m_transaction, &EditTransaction::canceled, this, [this] {
        m_pendingValue.clear();
        m_originalValue.clear();
        m_hasPendingValue = false;
        refresh();
    });
    connect(m_transaction, &EditTransaction::rejected, this,
            [this](const QString &message, int code) {
        m_pendingValue.clear();
        m_originalValue.clear();
        m_hasPendingValue = false;
        refresh();
        emit userEditRejected(message, code);
    });
    connect(m_transaction, &EditTransaction::stateChanged, this, &PropertyBinding::changed);
    refresh();
}

PropertyBinding::~PropertyBinding()
{
    cancel();
}

void PropertyBinding::setDocument(PropertyDocument *document)
{
    if (m_document == document) return;
    cancel();
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &PropertyDocument::changed, this, &PropertyBinding::refresh);
        connect(document, &PropertyDocument::reset, this, &PropertyBinding::refresh);
        connect(document, &QObject::destroyed, this, [this] { refresh(); });
    }
    configureTransaction();
    emit configurationChanged();
    refresh();
}

void PropertyBinding::setUndoService(UndoService *undo)
{
    if (m_undo == undo) return;
    cancel();
    m_undo = undo;
    configureTransaction();
    emit configurationChanged();
    refresh();
}

void PropertyBinding::setPropertyId(const QString &id)
{
    if (m_propertyId == id) return;
    cancel();
    m_propertyId = id;
    configureTransaction();
    emit configurationChanged();
    refresh();
}

void PropertyBinding::setTargetIds(const QStringList &ids)
{
    if (m_targetIds == ids) return;
    cancel();
    m_targetIds = ids;
    configureTransaction();
    emit configurationChanged();
    refresh();
}

void PropertyBinding::configureTransaction()
{
    m_transaction->setDocument(m_document);
    m_transaction->setUndoService(m_undo);
    m_transaction->setTargetIds(m_targetIds);
    m_transaction->setPropertyId(m_propertyId);
    m_transaction->setLabel(m_propertyId.isEmpty() ? QStringLiteral("Edit property") : m_propertyId);
}

bool PropertyBinding::editing() const
{
    return m_transaction && m_transaction->active();
}

QVariantMap PropertyBinding::descriptorMap(const PropertyDescriptor &entry) const
{
    QVariantMap result{{QStringLiteral("id"), entry.id},
                       {QStringLiteral("typeName"), QString::fromLatin1(entry.type.name())},
                       {QStringLiteral("typeId"), entry.type.id()},
                       {QStringLiteral("unitId"), entry.unitId},
                       {QStringLiteral("defaultValue"), entry.defaultValue},
                       {QStringLiteral("writable"), entry.writable},
                       {QStringLiteral("animatable"), entry.animatable},
                       {QStringLiteral("requiresExactString"), exactIntegerType(entry.type)}};
    if (exactIntegerType(entry.type))
        result.insert(QStringLiteral("exactIntegerDefault"), entry.defaultValue.toString());
    return result;
}

void PropertyBinding::refresh()
{
    QVariantMap descriptorValue;
    QVariant valueValue;
    QVariant defaultValueValue;
    QString validation;
    QString availability;
    QString exact;
    QString exactDefault;
    bool mixedValue = false;
    bool writableValue = false;
    bool animatedValue = false;
    bool exactString = false;

    if (!m_document) {
        availability = QStringLiteral("No property document is configured");
    } else if (m_propertyId.isEmpty()) {
        availability = QStringLiteral("No property is configured");
    } else if (const auto entry = m_document->descriptor(m_propertyId)) {
        descriptorValue = descriptorMap(*entry);
        defaultValueValue = entry->defaultValue;
        exactString = exactIntegerType(entry->type);
        if (exactString) exactDefault = entry->defaultValue.toString();
        if (m_targetIds.isEmpty()) {
            availability = QStringLiteral("No targets are selected");
        } else {
            bool first = true;
            bool allWritable = entry->writable;
            bool anyAnimated = false;
            QSet<QString> seenTargets;
            QVariant firstValue;
            for (const QString &idText : m_targetIds) {
                const EntityId id = EntityId::parse(idText);
                const QString canonicalId = id.toString();
                if (!id.isValid() || seenTargets.contains(canonicalId) || !m_document->contains(id)) {
                    if (availability.isEmpty()) availability = QStringLiteral("A selected target is unavailable");
                    allWritable = false;
                    continue;
                }
                seenTargets.insert(canonicalId);
                const QVariant candidate = m_document->value(id, m_propertyId);
                if (!candidate.isValid() || candidate.metaType() != entry->type) {
                    if (availability.isEmpty()) availability = QStringLiteral("A selected target has an incompatible property type");
                    allWritable = false;
                    continue;
                }
                if (first) {
                    firstValue = candidate;
                    first = false;
                } else if (candidate != firstValue || candidate.metaType() != firstValue.metaType()) {
                    mixedValue = true;
                }
                anyAnimated |= m_document->isTargetAnimated(id, m_propertyId);
                if (!m_document->isTargetWritable(id, m_propertyId)) allWritable = false;
                const OperationError targetValidation = m_document->validate(id, m_propertyId, candidate);
                if (validation.isEmpty() && targetValidation
                    && targetValidation.code != ErrorCode::Unavailable)
                    validation = targetValidation.message;
            }
            if (!first && !mixedValue) valueValue = firstValue;
            if (availability.isEmpty()) writableValue = allWritable;
            else writableValue = false;
            animatedValue = anyAnimated;
            if (allWritable && availability.isEmpty()) availability.clear();
            else if (availability.isEmpty() && !allWritable)
                availability = QStringLiteral("One or more selected targets are read only");
            if (exactString && valueValue.isValid() && !mixedValue)
                exact = valueValue.toString();
        }
    } else {
        availability = QStringLiteral("Unknown property");
    }

    if (descriptorValue == m_descriptor && valueValue == m_value
        && defaultValueValue == m_defaultValue && mixedValue == m_mixed
        && writableValue == m_writable && animatedValue == m_animated
        && validation == m_validationError && availability == m_availabilityError
        && exactString == m_requiresExactString && exact == m_exactIntegerValue
        && exactDefault == m_exactIntegerDefault)
        return;
    m_descriptor = descriptorValue;
    m_value = valueValue;
    m_defaultValue = defaultValueValue;
    m_mixed = mixedValue;
    m_writable = writableValue;
    m_animated = animatedValue;
    m_validationError = validation;
    m_availabilityError = availability;
    m_requiresExactString = exactString;
    m_exactIntegerValue = exact;
    m_exactIntegerDefault = exactDefault;
    emit changed();
}

void PropertyBinding::setFailure(const QString &message)
{
    m_validationError = message;
    emit changed();
}

bool PropertyBinding::begin()
{
    refresh();
    if (!writable()) {
        const QString message = availabilityError().isEmpty()
            ? QStringLiteral("Property is read only") : availabilityError();
        setFailure(message);
        emit userEditRejected(message, static_cast<int>(ErrorCode::Unavailable));
        return false;
    }
    if (!m_transaction->begin()) {
        setFailure(m_transaction->errorMessage());
        return false;
    }
    m_originalValue = m_value;
    m_originalMixed = m_mixed;
    m_hasPendingValue = false;
    m_validationError.clear();
    emit changed();
    return true;
}

bool PropertyBinding::update(const QVariant &candidate)
{
    if (!editing() && !begin()) return false;
    const OperationError failure = m_transaction->updateValue(candidate);
    if (failure) {
        setFailure(failure.message);
        cancel();
        setFailure(failure.message);
        return false;
    }
    m_pendingValue = candidate;
    m_hasPendingValue = true;
    m_validationError.clear();
    emit changed();
    return true;
}

bool PropertyBinding::updateComponent(int componentIndex, const QVariant &candidateComponent)
{
    if (!editing() && !begin()) return false;
    const OperationError failure = m_transaction->updateComponent(componentIndex, candidateComponent);
    if (failure) {
        setFailure(failure.message);
        cancel();
        setFailure(failure.message);
        return false;
    }
    m_pendingValue = QVariantMap{{QStringLiteral("componentIndex"), componentIndex},
                                 {QStringLiteral("value"), candidateComponent}};
    m_hasPendingValue = true;
    m_validationError.clear();
    emit changed();
    return true;
}

bool PropertyBinding::updateExactInteger(const QString &decimal)
{
    if (!m_document) return false;
    const auto entry = m_document->descriptor(m_propertyId);
    QVariant exactValue;
    if (!entry || !parseExactInteger(entry->type, decimal, &exactValue)) {
        const QString message = QStringLiteral("Invalid exact integer value");
        setFailure(message);
        emit userEditRejected(message, static_cast<int>(ErrorCode::Validation));
        return false;
    }
    return update(exactValue);
}

bool PropertyBinding::commit()
{
    if (!editing()) return false;
    if (!m_transaction->hasEffectiveChanges()) m_hasPendingValue = false;
    const OperationError failure = m_transaction->commitEdit();
    if (failure) {
        setFailure(failure.message);
        return false;
    }
    return true;
}

void PropertyBinding::cancel()
{
    if (!editing()) return;
    m_transaction->cancel();
    m_pendingValue.clear();
    m_originalValue.clear();
    m_hasPendingValue = false;
    refresh();
}

bool PropertyBinding::reset()
{
    if (!writable() || !m_defaultValue.isValid()) return false;
    if (!begin()) return false;
    if (!update(m_defaultValue)) {
        cancel();
        return false;
    }
    return commit();
}

} // namespace QindaTK
