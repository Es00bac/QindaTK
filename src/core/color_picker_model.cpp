// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_picker_model.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <cmath>

namespace QindaTK {
namespace {
constexpr auto Property = "color";
constexpr auto Schema = "qindatk.color-picker/1";
QVariantMap valueMap(const ColorValue &v)
{
    return {{"red", v.red}, {"green", v.green}, {"blue", v.blue},
            {"alpha", v.alpha}, {"profileId", v.profileId},
            {"transferId", v.transferId}};
}
ColorValue initialValue(const ColorProfile &p)
{
    return {0.29, 0.08, 0.6, 1.0, p.id(), p.transferId()};
}
}

ColorPickerModel::ColorPickerModel(QObject *parent) : QObject(parent)
{
    m_context.document = ColorProfile::sRgb();
    m_context.working = m_context.document;
    m_context.display = m_context.document;
    m_context.output = m_context.document;
    m_context.generation = 1;
    m_document.defineProperty({QString::fromLatin1(Property), QMetaType::fromType<ColorValue>(),
                               QVariant::fromValue(initialValue(m_context.document)), {}, true, false});
    m_undo.addDocument(&m_document);
    m_transaction.setDocument(&m_document);
    m_transaction.setUndoService(&m_undo);
    m_transaction.setPropertyId(QString::fromLatin1(Property));
    m_transaction.setLabel(QObject::tr("Change color"));
    const QString firstId = EntityId::create().toString();
    m_document.addEntity(EntityId::parse(firstId));
    m_targetIds = {firstId};
    connect(&m_document, &PropertyDocument::changed, this, &ColorPickerModel::changed);
    connect(&m_undo, &UndoService::changed, this, &ColorPickerModel::changed);
}

ColorValue ColorPickerModel::readValue(const QString &id) const
{
    return m_document.value(EntityId::parse(id), QString::fromLatin1(Property)).value<ColorValue>();
}

ColorValue ColorPickerModel::colorValue(const QString &id) const { return readValue(id); }

bool ColorPickerModel::addTarget(const QString &id, const ColorValue &value)
{
    if (id.isEmpty() || !value.isValid() || value.profileId != m_context.document.id()
        || value.transferId != m_context.document.transferId()
        || !m_document.addEntity(EntityId::parse(id), {{QString::fromLatin1(Property), QVariant::fromValue(value)}}))
        return false;
    return true;
}

bool ColorPickerModel::resolveTargets() const
{
    if (m_targetIds.isEmpty()) return false;
    QSet<QString> seen;
    for (const QString &id : m_targetIds) {
        const EntityId entity = EntityId::parse(id);
        if (!entity.isValid() || seen.contains(id) || !m_document.contains(entity)) return false;
        seen.insert(id);
    }
    return true;
}

void ColorPickerModel::setTargetIds(const QStringList &ids)
{
    if (ids == m_targetIds) return;
    cancelEdit();
    const QStringList previous = m_targetIds;
    m_targetIds = ids;
    if (!resolveTargets()) {
        m_targetIds = previous;
        setError({ErrorCode::InvalidArgument, tr("Selection contains an unknown or duplicate color target"), "targets", {}});
        return;
    }
    m_error.clear();
    emit changed();
}

void ColorPickerModel::setColorContext(const ColorContextSnapshot &context)
{
    const bool changedDocumentProfile = context.document.id() != m_context.document.id();
    if (!context.document.isValid() || !context.working.isValid() || !context.display.isValid()
        || context.generation == 0) {
        if (m_transaction.active()) cancelEdit();
        setError({ErrorCode::Unavailable, tr("Color context profiles must be resolved"), "context", {}});
        return;
    }
    const bool staleGesture = m_transaction.active()
        && context.generation != m_editGeneration;
    if (staleGesture) cancelEdit();
    if (changedDocumentProfile) {
        setError({ErrorCode::Unsupported, tr("Changing stored color profiles requires an explicit value migration"), "context.document", {}});
        return;
    }
    m_context = context;
    m_error = staleGesture ? tr("Color context changed during the edit") : QString{};
    emit changed();
}

void ColorPickerModel::setError(const OperationError &error)
{
    m_error = error.message;
    emit changed();
}

QColor ColorPickerModel::previewColor() const
{
    if (m_preview.isValid() && m_transaction.active()) return m_preview;
    if (!resolveTargets()) return {};
    const ColorValue sourceValue = readValue(m_targetIds.constFirst());
    ColorTransformRequest request{sourceValue, m_context.document, m_context, ColorOperation::Display};
    const auto converted = m_engine.convert(request);
    if (!converted.value || converted.generation != m_context.generation) return {};
    const auto &v = *converted.value;
    return QColor::fromRgbF(v.red, v.green, v.blue, v.alpha);
}

QVariantMap ColorPickerModel::typedValue() const
{
    return resolveTargets() ? valueMap(readValue(m_targetIds.constFirst())) : QVariantMap{};
}

bool ColorPickerModel::previewSrgb(const QColor &color)
{
    if (!color.isValid() || !resolveTargets()) {
        setError({ErrorCode::InvalidArgument, tr("A valid color target and sRGB preview are required"), "color", {}});
        return false;
    }
    if (!m_transaction.active()) {
        m_editGeneration = m_context.generation;
        QVector<EntityId> targets;
        targets.reserve(m_targetIds.size());
        for (const auto &id : m_targetIds) targets.append(EntityId::parse(id));
        if (!m_transaction.begin(targets, QString::fromLatin1(Property), tr("Change color"))) {
            setError(m_transaction.error());
            return false;
        }
    }
    if (m_editGeneration != m_context.generation) {
        cancelEdit();
        setError({ErrorCode::StaleGeneration, tr("Color preview belongs to an old context"), "context.generation", {}});
        return false;
    }
    const ColorProfile source = ColorProfile::sRgb();
    ColorValue encoded{color.redF(), color.greenF(), color.blueF(), color.alphaF(),
                       source.id(), source.transferId()};
    ColorContextSnapshot targetContext = m_context;
    targetContext.working = targetContext.document;
    const auto result = m_engine.convert({encoded, source, targetContext, ColorOperation::Working});
    if (!result.value || result.generation != m_editGeneration || m_context.generation != m_editGeneration) {
        const OperationError error = result.error ? result.error
            : OperationError{ErrorCode::StaleGeneration, tr("Color transform result is stale"), "context.generation", {}};
        cancelEdit();
        setError(error);
        return false;
    }
    const auto error = m_transaction.updateValue(QVariant::fromValue(*result.value));
    if (error) {
        cancelEdit();
        setError(error);
        return false;
    }
    m_preview = color;
    m_error.clear();
    emit changed();
    return true;
}

bool ColorPickerModel::commitEdit()
{
    if (!m_transaction.active() || m_editGeneration != m_context.generation) {
        cancelEdit();
        setError({ErrorCode::StaleGeneration, tr("Color edit is no longer current"), "context.generation", {}});
        return false;
    }
    const auto error = m_transaction.commitEdit();
    m_preview = {};
    setError(error);
    return !error;
}

void ColorPickerModel::cancelEdit()
{
    if (m_transaction.active()) m_transaction.cancel();
    m_preview = {};
    emit changed();
}

QByteArray ColorPickerModel::saveJson() const
{
    QJsonArray targets;
    for (const QString &id : m_targetIds)
        targets.append(QJsonObject{{"id", id}, {"value", QJsonObject::fromVariantMap(valueMap(readValue(id)))}});
    return QJsonDocument(QJsonObject{{"schema", Schema}, {"targets", targets}}).toJson(QJsonDocument::Compact);
}

bool ColorPickerModel::loadJson(const QByteArray &bytes)
{
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()
        || doc.object().value("schema").toString() != QString::fromLatin1(Schema)) {
        setError({ErrorCode::Validation, tr("Invalid color document JSON"), "schema", {}});
        return false;
    }
    QVector<QPair<EntityId, QVariantMap>> entities;
    QStringList ids;
    for (const auto &entry : doc.object().value("targets").toArray()) {
        const auto object = entry.toObject();
        const QString id = object.value("id").toString();
        const auto v = object.value("value").toObject();
        ColorValue value{v.value("red").toDouble(qQNaN()), v.value("green").toDouble(qQNaN()),
                         v.value("blue").toDouble(qQNaN()), v.value("alpha").toDouble(qQNaN()),
                         v.value("profileId").toString(), v.value("transferId").toString()};
        const EntityId entity = EntityId::parse(id);
        if (!entity.isValid() || ids.contains(id) || !value.isValid()
            || value.profileId != m_context.document.id()
            || value.transferId != m_context.document.transferId()) {
            setError({ErrorCode::Validation, tr("Color document has an unresolved or invalid value"), "targets", {}});
            return false;
        }
        ids.append(id);
        entities.append({entity, {{QString::fromLatin1(Property), QVariant::fromValue(value)}}});
    }
    if (entities.isEmpty()) {
        setError({ErrorCode::Validation, tr("Color document has no targets"), "targets", {}});
        return false;
    }
    cancelEdit();
    if (!m_document.resetModel(entities)) return false;
    m_targetIds = ids;
    m_error.clear();
    emit changed();
    return true;
}

} // namespace QindaTK
