// SPDX-License-Identifier: LGPL-3.0-or-later
#include "query_controller.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>

namespace QindaTK {
QString QueryController::saveQuery() const
{
    QuerySpec spec; QString error;
    QuerySchema schema;
    if (!m_source || !parseSchema(m_schemaMap, &schema, &error)
        || !QueryEngine::fromJson(QJsonObject::fromVariantMap(m_queryMap), &spec, &error))
        return {};
    const QJsonObject document{{"documentVersion", 1},
                               {"query", QueryEngine::toJson(spec)},
                               {"schema", QJsonObject::fromVariantMap(m_schemaMap)},
                               {"joinSchema", QJsonObject::fromVariantMap(m_joinSchemaMap)}};
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
}

bool QueryController::reopenQuery(const QString &document)
{
    auto fail = [this](const QString &why) {
        invalidate(why); emit rejected(m_requestId, why); return false;
    };
    if (document.toUtf8().size() > 1048576 || !m_source)
        return fail(QStringLiteral("Missing source or oversized saved query"));
    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(document.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()
        || parsed.object().value("documentVersion").toInt() != 1)
        return fail(QStringLiteral("Unsupported saved query document"));
    const auto saved = parsed.object();
    QuerySpec spec; QString error;
    if (!QueryEngine::fromJson(saved.value("query").toObject(), &spec, &error)) return fail(error);
    m_requestId = spec.id;
    QuerySchema oldSchema, currentSchema, oldJoin, currentJoin;
    if (!parseSchema(saved.value("schema").toObject().toVariantMap(), &oldSchema, &error)
        || !parseSchema(m_schemaMap, &currentSchema, &error)) return fail(error);
    if (oldSchema.id != currentSchema.id || spec.schemaId != oldSchema.id)
        return fail(QStringLiteral("Saved query schema identity changed"));
    const bool joins = !spec.join.sourceId.isEmpty();
    if (joins && (!m_joinSource
        || !parseSchema(saved.value("joinSchema").toObject().toVariantMap(), &oldJoin, &error)
        || !parseSchema(m_joinSchemaMap, &currentJoin, &error)
        || oldJoin.id != currentJoin.id || spec.join.sourceId != oldJoin.id))
        return fail(QStringLiteral("Saved join source or schema is unavailable"));
    QHash<QString, QueryType> oldTypes, currentTypes;
    for (const auto &field : oldSchema.fields) oldTypes.insert(field.id, field.type);
    for (const auto &field : currentSchema.fields) currentTypes.insert(field.id, field.type);
    if (joins) {
        for (const auto &field : oldJoin.fields)
            oldTypes.insert(oldJoin.id + QLatin1Char('.') + field.id, field.type);
        for (const auto &field : currentJoin.fields)
            currentTypes.insert(currentJoin.id + QLatin1Char('.') + field.id, field.type);
    }
    QSet<QString> required;
    for (const auto &field : requestedFields(spec)) required.insert(field);
    if (joins) {
        required.insert(spec.join.leftField);
        required.insert(currentJoin.id + QLatin1Char('.') + spec.join.rightField);
    }
    for (const auto &field : required)
        if (!oldTypes.contains(field) || !currentTypes.contains(field)
            || oldTypes.value(field) != currentTypes.value(field))
            return fail(QStringLiteral("Saved query field changed: %1").arg(field));
    spec.schemaRevision = currentSchema.revision;
    spec.sourceRevision = m_source->revision();
    if (joins) {
        spec.join.schemaRevision = currentJoin.revision;
        spec.join.sourceRevision = m_joinSource->revision();
    }
    spec.id += QStringLiteral(":reopen:%1").arg(++m_reopenSequence);
    QuerySchema effective = currentSchema;
    if (joins) for (auto field : currentJoin.fields) {
        field.id = currentJoin.id + QLatin1Char('.') + field.id;
        field.nullable |= spec.join.leftOuter;
        effective.fields.append(field);
    }
    error = QueryEngine::validate(effective, spec, QueryCapabilities::local());
    if (!error.isEmpty()) return fail(error);
    setQuery(QueryEngine::toJson(spec).toVariantMap());
    return refresh();
}
} // namespace QindaTK
