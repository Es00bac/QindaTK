// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_sync_detail_p.h"
#include "reference_document_codec.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <cmath>

namespace QindaTK {
namespace sync_detail {

bool canonicalUuid(const QString &id)
{
    const QUuid uuid(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

bool outranks(const PropertySyncOperation &a, const PropertySyncOperation &b)
{
    if (a.logicalTime != b.logicalTime) return a.logicalTime > b.logicalTime;
    if (a.actorId != b.actorId) return a.actorId > b.actorId;
    return a.operationId > b.operationId;
}

namespace {
std::optional<QJsonObject> encodeValue(const QVariant &value)
{
    QString kind;
    QJsonValue stored;
    switch (value.metaType().id()) {
    case QMetaType::Bool: kind = QStringLiteral("bool"); stored = value.toBool(); break;
    case QMetaType::Int: kind = QStringLiteral("int"); stored = value.toString(); break;
    case QMetaType::UInt: kind = QStringLiteral("uint"); stored = value.toString(); break;
    case QMetaType::LongLong: kind = QStringLiteral("int64"); stored = value.toString(); break;
    case QMetaType::ULongLong: kind = QStringLiteral("uint64"); stored = value.toString(); break;
    case QMetaType::Double:
        if (!std::isfinite(value.toDouble())) return std::nullopt;
        kind = QStringLiteral("double"); stored = QString::number(value.toDouble(), 'g', 17); break;
    case QMetaType::QString: kind = QStringLiteral("string"); stored = value.toString(); break;
    case QMetaType::QByteArray:
        kind = QStringLiteral("bytes"); stored = QString::fromLatin1(value.toByteArray().toBase64()); break;
    default: return std::nullopt;
    }
    QJsonObject result{{QStringLiteral("kind"), kind}, {QStringLiteral("data"), stored}};
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > MaxValueBytes)
        return std::nullopt;
    return result;
}

std::optional<QVariant> decodeValue(const QJsonObject &object)
{
    if (object.size() != 2 || !object.contains(QStringLiteral("kind"))
        || !object.contains(QStringLiteral("data"))
        || QJsonDocument(object).toJson(QJsonDocument::Compact).size() > MaxValueBytes)
        return std::nullopt;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    const QJsonValue data = object.value(QStringLiteral("data"));
    if (kind == QStringLiteral("bool") && data.isBool()) return QVariant(data.toBool());
    if (!data.isString()) return std::nullopt;
    const QString text = data.toString();
    if (kind == QStringLiteral("string")) return QVariant(text);
    if (kind == QStringLiteral("bytes")) {
        const QByteArray encoded = text.toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(encoded);
        if (!decoded || decoded.decoded.toBase64() != encoded) return std::nullopt;
        return QVariant(decoded.decoded);
    }
    bool ok = false;
    if (kind == QStringLiteral("int")) {
        const int n = text.toInt(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("uint")) {
        const uint n = text.toUInt(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("int64")) {
        const qlonglong n = text.toLongLong(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("uint64")) {
        const qulonglong n = text.toULongLong(&ok); if (ok && QString::number(n) == text) return QVariant(n);
    } else if (kind == QStringLiteral("double")) {
        const double n = text.toDouble(&ok);
        if (ok && std::isfinite(n) && QString::number(n, 'g', 17) == text) return QVariant(n);
    }
    return std::nullopt;
}

using Kind = PropertySyncOperation::Kind;

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Set: return {};
    case Kind::Restore: return QStringLiteral("restore");
    case Kind::Create: return QStringLiteral("create");
    case Kind::Delete: return QStringLiteral("delete");
    case Kind::TextInsert: return QStringLiteral("textInsert");
    case Kind::TextDelete: return QStringLiteral("textDelete");
    case Kind::Checkpoint: return QStringLiteral("checkpoint");
    }
    return {};
}

std::optional<Kind> kindFromName(const QString &name)
{
    for (Kind kind : {Kind::Restore, Kind::Create, Kind::Delete, Kind::TextInsert,
                      Kind::TextDelete, Kind::Checkpoint})
        if (kindName(kind) == name) return kind;
    return std::nullopt;
}

bool snapshotMatches(const PropertySyncOperation &op)
{
    if (op.value.metaType() != QMetaType::fromType<QByteArray>()
        || op.value.toByteArray().size() > MaxSnapshotBytes || op.entityId != op.documentId)
        return false;
    const auto snapshot = ReferenceDocumentCodec::decode(op.value.toByteArray());
    return snapshot && snapshot->document->documentIdString() == op.documentId;
}

bool validKindPayload(const PropertySyncOperation &op)
{
    const bool bytes = op.value.metaType() == QMetaType::fromType<QByteArray>();
    switch (op.kind) {
    case Kind::Set:
        return op.meta.isEmpty() && !op.propertyId.startsWith(QStringLiteral("__qtk."));
    case Kind::Restore:
        return op.meta.isEmpty() && op.propertyId == restoreProperty() && snapshotMatches(op);
    case Kind::Checkpoint:
        return op.propertyId == checkpointProperty() && snapshotMatches(op)
            && decodeCheckpointMeta(op.meta).has_value();
    case Kind::Create:
        return op.meta.isEmpty() && op.propertyId == entityProperty()
            && op.value.metaType() == QMetaType::fromType<bool>() && op.value.toBool();
    case Kind::Delete:
        return op.meta.isEmpty() && op.propertyId == entityProperty() && bytes
            && decodeIdList(op.value.toByteArray()).has_value();
    case Kind::TextInsert:
        return op.value.metaType() == QMetaType::fromType<QString>()
            && !op.value.toString().isEmpty() && op.value.toString().size() <= MaxTextInsert
            && (op.meta.isEmpty() || parseElementId(op.meta).has_value());
    case Kind::TextDelete:
        return op.meta.isEmpty() && bytes && decodeTextRanges(op.value.toByteArray()).has_value();
    }
    return false;
}
} // namespace

bool encodableValue(const QVariant &value)
{
    return encodeValue(value).has_value();
}

bool sameOperation(const PropertySyncOperation &a, const PropertySyncOperation &b)
{
    return a.documentId == b.documentId && a.operationId == b.operationId
        && a.actorId == b.actorId && a.logicalTime == b.logicalTime
        && a.entityId == b.entityId && a.propertyId == b.propertyId
        && a.value.metaType() == b.value.metaType() && a.value == b.value
        && a.kind == b.kind && a.meta == b.meta;
}

bool validOperation(const PropertySyncOperation &operation)
{
    const auto encoded = propertySyncOperationToJson(operation);
    return encoded && propertySyncOperationFromJson(*encoded).has_value();
}

QByteArray encodeIdList(const QStringList &ids)
{
    return QJsonDocument(QJsonArray::fromStringList(ids)).toJson(QJsonDocument::Compact);
}

std::optional<QStringList> decodeIdList(const QByteArray &bytes)
{
    const auto json = QJsonDocument::fromJson(bytes);
    if (!json.isArray() || json.array().size() > MaxObserved) return std::nullopt;
    QStringList ids;
    QSet<QString> seen;
    for (const auto &value : json.array()) {
        const QString id = value.toString();
        if (!value.isString() || !canonicalUuid(id) || seen.contains(id)) return std::nullopt;
        seen.insert(id);
        ids.append(id);
    }
    return ids;
}

QString elementId(const QString &op, int index)
{
    return op + QLatin1Char(':') + QString::number(index);
}

std::optional<QPair<QString, int>> parseElementId(const QString &id)
{
    const auto split = id.lastIndexOf(QLatin1Char(':'));
    if (split <= 0) return std::nullopt;
    const QString op = id.left(split);
    const QString number = id.mid(split + 1);
    bool ok = false;
    const int index = number.toInt(&ok);
    if (!ok || index < 0 || index >= MaxTextElements || QString::number(index) != number
        || (op != baseElementOp() && !canonicalUuid(op)))
        return std::nullopt;
    return QPair<QString, int>{op, index};
}

QByteArray encodeTextRanges(const QVector<TextRange> &ranges)
{
    QJsonArray rows;
    for (const auto &range : ranges)
        rows.append(QJsonArray{range.op, range.start, range.count});
    return QJsonDocument(rows).toJson(QJsonDocument::Compact);
}

std::optional<QVector<TextRange>> decodeTextRanges(const QByteArray &bytes)
{
    const auto json = QJsonDocument::fromJson(bytes);
    if (!json.isArray() || json.array().isEmpty() || json.array().size() > MaxTextRanges)
        return std::nullopt;
    QVector<TextRange> ranges;
    qint64 total = 0;
    for (const auto &value : json.array()) {
        const QJsonArray row = value.toArray();
        if (!value.isArray() || row.size() != 3 || !row.at(0).isString()
            || !row.at(1).isDouble() || !row.at(2).isDouble())
            return std::nullopt;
        const double start = row.at(1).toDouble();
        const double count = row.at(2).toDouble();
        TextRange range{row.at(0).toString(), int(start), int(count)};
        if (start != std::floor(start) || count != std::floor(count) || start < 0 || count < 1
            || start + count > MaxTextElements
            || (range.op != baseElementOp() && !canonicalUuid(range.op)))
            return std::nullopt;
        total += range.count;
        if (total > MaxTextElements) return std::nullopt;
        ranges.append(range);
    }
    return ranges;
}

QString encodeCheckpointMeta(const CheckpointMeta &meta)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("name"), meta.name},
        {QStringLiteral("createdAt"), meta.createdAt}}).toJson(QJsonDocument::Compact));
}

std::optional<CheckpointMeta> decodeCheckpointMeta(const QString &text)
{
    if (text.size() > 512) return std::nullopt;
    const auto json = QJsonDocument::fromJson(text.toUtf8());
    const QJsonObject object = json.object();
    CheckpointMeta meta{object.value(QStringLiteral("name")).toString(),
                        object.value(QStringLiteral("createdAt")).toString()};
    if (!json.isObject() || object.size() != 2 || meta.name.trimmed() != meta.name
        || meta.name.isEmpty() || meta.name.size() > 128
        || !QDateTime::fromString(meta.createdAt, Qt::ISODateWithMs).isValid())
        return std::nullopt;
    return meta;
}

} // namespace sync_detail

using namespace sync_detail;

std::optional<QJsonObject> propertySyncOperationToJson(const PropertySyncOperation &op)
{
    const auto value = encodeValue(op.value);
    if (!value) return std::nullopt;
    QJsonObject result{{QStringLiteral("document"), op.documentId},
                       {QStringLiteral("id"), op.operationId},
                       {QStringLiteral("actor"), op.actorId},
                       {QStringLiteral("time"), QString::number(op.logicalTime)},
                       {QStringLiteral("entity"), op.entityId},
                       {QStringLiteral("property"), op.propertyId},
                       {QStringLiteral("value"), *value}};
    // AGENT-GUARD: a Set packet keeps the original seven keys so stores and
    // peers written before the structural kinds still decode unchanged.
    if (op.kind != PropertySyncOperation::Kind::Set)
        result.insert(QStringLiteral("kind"), kindName(op.kind));
    if (!op.meta.isEmpty()) result.insert(QStringLiteral("meta"), op.meta);
    return result;
}

std::optional<PropertySyncOperation> propertySyncOperationFromJson(const QJsonObject &object)
{
    static const QSet<QString> required{
        QStringLiteral("document"), QStringLiteral("id"), QStringLiteral("actor"),
        QStringLiteral("time"), QStringLiteral("entity"), QStringLiteral("property"),
        QStringLiteral("value")};
    int optional = 0;
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (required.contains(it.key())) continue;
        if (it.key() != QStringLiteral("kind") && it.key() != QStringLiteral("meta"))
            return std::nullopt;
        if (!it.value().isString()) return std::nullopt;
        ++optional;
    }
    if (object.size() - optional != required.size()
        || !object.value(QStringLiteral("value")).isObject())
        return std::nullopt;
    PropertySyncOperation op;
    op.documentId = object.value(QStringLiteral("document")).toString();
    op.operationId = object.value(QStringLiteral("id")).toString();
    op.actorId = object.value(QStringLiteral("actor")).toString();
    op.entityId = object.value(QStringLiteral("entity")).toString();
    op.propertyId = object.value(QStringLiteral("property")).toString();
    op.meta = object.value(QStringLiteral("meta")).toString();
    const QString time = object.value(QStringLiteral("time")).toString();
    bool ok = false;
    op.logicalTime = time.toULongLong(&ok);
    const auto value = decodeValue(object.value(QStringLiteral("value")).toObject());
    if (!canonicalUuid(op.documentId) || !canonicalUuid(op.operationId)
        || !canonicalUuid(op.actorId) || !canonicalUuid(op.entityId)
        || !ok || !op.logicalTime || QString::number(op.logicalTime) != time
        || op.propertyId.isEmpty() || op.propertyId.size() > 256 || !value
        || (object.contains(QStringLiteral("meta")) && op.meta.isEmpty()))
        return std::nullopt;
    op.value = *value;
    if (object.contains(QStringLiteral("kind"))) {
        const auto kind = kindFromName(object.value(QStringLiteral("kind")).toString());
        if (!kind) return std::nullopt;
        op.kind = *kind;
    }
    if (!validKindPayload(op)) return std::nullopt;
    return op;
}

} // namespace QindaTK
