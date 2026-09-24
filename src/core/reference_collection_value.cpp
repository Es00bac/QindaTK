// SPDX-License-Identifier: LGPL-3.0-or-later
#include "reference_collection_value.h"
#include "application_contracts.h"

#include <QJsonObject>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace QindaTK::ReferenceCollectionValue {
namespace {
constexpr qsizetype maximumRows = 4096;
constexpr qsizetype maximumNodes = 100000;
constexpr qsizetype maximumBytes = 16 * 1024 * 1024;
constexpr int maximumDepth = 24;

std::optional<QJsonValue> encodeNode(const QVariant &value, int depth, qsizetype &nodes)
{
    if (++nodes > maximumNodes || depth > maximumDepth || !value.isValid()) return std::nullopt;
    const QMetaType type = value.metaType();
    if (type == QMetaType::fromType<QString>()) return QJsonValue(value.toString());
    if (type == QMetaType::fromType<bool>()) return QJsonValue(value.toBool());
    if (type == QMetaType::fromType<double>() || type == QMetaType::fromType<int>()
        || type == QMetaType::fromType<qlonglong>() || type == QMetaType::fromType<qulonglong>()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < -9007199254740991.0
            || number > 9007199254740991.0)
            return std::nullopt;
        return QJsonValue(number);
    }
    if (type == QMetaType::fromType<QVariantMap>()) {
        QJsonObject object;
        const QVariantMap map = value.toMap();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (it.key().isEmpty() || it.key().size() > 4096) return std::nullopt;
            const auto child = encodeNode(it.value(), depth + 1, nodes);
            if (!child) return std::nullopt;
            object.insert(it.key(), *child);
        }
        return object;
    }
    if (type == QMetaType::fromType<QVariantList>()) {
        QJsonArray array;
        for (const QVariant &childValue : value.toList()) {
            const auto child = encodeNode(childValue, depth + 1, nodes);
            if (!child) return std::nullopt;
            array.append(*child);
        }
        return array;
    }
    return std::nullopt;
}

std::optional<QVariant> decodeNode(const QJsonValue &value, int depth, qsizetype &nodes)
{
    if (++nodes > maximumNodes || depth > maximumDepth) return std::nullopt;
    if (value.isString()) return QVariant(value.toString());
    if (value.isBool()) return QVariant(value.toBool());
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < -9007199254740991.0
            || number > 9007199254740991.0) return std::nullopt;
        return value.toVariant();
    }
    if (value.isObject()) {
        QVariantMap map;
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.key().isEmpty() || it.key().size() > 4096) return std::nullopt;
            const auto child = decodeNode(it.value(), depth + 1, nodes);
            if (!child) return std::nullopt;
            map.insert(it.key(), *child);
        }
        return map;
    }
    if (value.isArray()) {
        QVariantList list;
        for (const QJsonValue &item : value.toArray()) {
            const auto child = decodeNode(item, depth + 1, nodes);
            if (!child) return std::nullopt;
            list.append(*child);
        }
        return list;
    }
    return std::nullopt;
}

bool validReferences(const QJsonValue &value, const QSet<QString> &ids, bool root)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.contains(QStringLiteral("$ref")))
            return object.size() == 1 && object.value(QStringLiteral("$ref")).isString()
                && ids.contains(object.value(QStringLiteral("$ref")).toString());
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (root && it.key() == QStringLiteral("id")) continue;
            if (unresolvedField(it.key()) || !validReferences(it.value(), ids, false))
                return false;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &child : value.toArray())
            if (!validReferences(child, ids, false)) return false;
    } else if (value.isString()) {
        const QString text = value.toString();
        const QUuid uuid(text);
        if (!uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == text) return false;
    }
    return true;
}

bool validRows(const QJsonArray &array)
{
    if (array.size() > maximumRows) return false;
    QSet<QString> ids;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) return false;
        const QJsonValue idValue = value.toObject().value(QStringLiteral("id"));
        if (!idValue.isString()) return false;
        const EntityId id = EntityId::parse(idValue.toString());
        if (!id.isValid() || ids.contains(id.toString())) return false;
        ids.insert(id.toString());
    }
    for (const QJsonValue &value : array)
        if (!validReferences(value, ids, true)) return false;
    return true;
}
}

bool unresolvedField(const QString &key)
{
    const QString normalized = key.toCaseFolded();
    return normalized == QStringLiteral("resource")
        || normalized == QStringLiteral("resources")
        || normalized == QStringLiteral("resourceid")
        || normalized == QStringLiteral("resourceids")
        || normalized == QStringLiteral("resourceuri")
        || normalized == QStringLiteral("assetid")
        || normalized == QStringLiteral("asseturi")
        || normalized == QStringLiteral("pngbase64")
        || normalized == QStringLiteral("uri")
        || normalized == QStringLiteral("references")
        || normalized == QStringLiteral("referenceid")
        || normalized == QStringLiteral("referenceids")
        || normalized == QStringLiteral("documentid")
        || normalized == QStringLiteral("missingresource");
}

std::optional<QJsonArray> encode(const QVariant &value)
{
    if (value.metaType() != QMetaType::fromType<QVariantList>()) return std::nullopt;
    qsizetype nodes = 0;
    const auto encoded = encodeNode(value, 0, nodes);
    if (!encoded || !encoded->isArray()) return std::nullopt;
    const QJsonArray array = encoded->toArray();
    if (!validRows(array) || QJsonDocument(array).toJson(QJsonDocument::Compact).size() > maximumBytes)
        return std::nullopt;
    return array;
}

std::optional<QVariantList> decode(const QJsonArray &array)
{
    if (!validRows(array) || QJsonDocument(array).toJson(QJsonDocument::Compact).size() > maximumBytes)
        return std::nullopt;
    qsizetype nodes = 0;
    const auto decoded = decodeNode(array, 0, nodes);
    if (!decoded || decoded->metaType() != QMetaType::fromType<QVariantList>()) return std::nullopt;
    return decoded->toList();
}

} // namespace QindaTK::ReferenceCollectionValue
