// SPDX-License-Identifier: LGPL-3.0-or-later
#include "reference_document_codec.h"
#include "reference_collection_value.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr auto formatId = "qindatk.reference-property-document";

bool onlyKeys(const QJsonObject &object, std::initializer_list<const char *> allowed)
{
    QSet<QString> names;
    for (const auto *name : allowed) names.insert(QString::fromLatin1(name));
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!names.contains(it.key())) return false;
    return true;
}

void fail(OperationError *error, ErrorCode code, const QString &message,
          const QString &path = {})
{
    if (error) *error = {code, message, path, {}};
}

QString kindFor(QMetaType type)
{
    switch (type.id()) {
    case QMetaType::Bool: return QStringLiteral("bool");
    case QMetaType::Int: return QStringLiteral("int");
    case QMetaType::UInt: return QStringLiteral("uint");
    case QMetaType::LongLong: return QStringLiteral("int64");
    case QMetaType::ULongLong: return QStringLiteral("uint64");
    case QMetaType::Double: return QStringLiteral("double");
    case QMetaType::QString: return QStringLiteral("string");
    case QMetaType::QByteArray: return QStringLiteral("bytes");
    case QMetaType::QVariantList: return QStringLiteral("objectCollection");
    default: return {};
    }
}

QMetaType typeFor(const QString &kind)
{
    if (kind == QStringLiteral("bool")) return QMetaType::fromType<bool>();
    if (kind == QStringLiteral("int")) return QMetaType::fromType<int>();
    if (kind == QStringLiteral("uint")) return QMetaType::fromType<uint>();
    if (kind == QStringLiteral("int64")) return QMetaType::fromType<qlonglong>();
    if (kind == QStringLiteral("uint64")) return QMetaType::fromType<qulonglong>();
    if (kind == QStringLiteral("double")) return QMetaType::fromType<double>();
    if (kind == QStringLiteral("string")) return QMetaType::fromType<QString>();
    if (kind == QStringLiteral("bytes")) return QMetaType::fromType<QByteArray>();
    if (kind == QStringLiteral("objectCollection")) return QMetaType::fromType<QVariantList>();
    return {};
}

bool canonicalUnsigned(const QString &text, quint64 *value)
{
    if (text.isEmpty()) return false;
    for (QChar ch : text)
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return false;
    bool ok = false;
    const auto parsed = text.toULongLong(&ok, 10);
    if (!ok || QString::number(parsed) != text) return false;
    *value = parsed;
    return true;
}

bool canonicalSigned(const QString &text, qint64 *value)
{
    if (text.isEmpty()) return false;
    const qsizetype start = text.front() == QLatin1Char('-') ? 1 : 0;
    if (start == text.size()) return false;
    for (qsizetype i = start; i < text.size(); ++i)
        if (text[i] < QLatin1Char('0') || text[i] > QLatin1Char('9')) return false;
    bool ok = false;
    const auto parsed = text.toLongLong(&ok, 10);
    if (!ok || QString::number(parsed) != text) return false;
    *value = parsed;
    return true;
}

std::optional<QJsonObject> encodeValue(const QVariant &value)
{
    const QString kind = kindFor(value.metaType());
    if (kind.isEmpty() || !value.isValid()) return std::nullopt;
    QJsonValue stored;
    if (kind == QStringLiteral("bool")) stored = value.toBool();
    else if (kind == QStringLiteral("string")) stored = value.toString();
    else if (kind == QStringLiteral("bytes"))
        stored = QString::fromLatin1(value.toByteArray().toBase64());
    else if (kind == QStringLiteral("objectCollection")) {
        const auto collection = ReferenceCollectionValue::encode(value);
        if (!collection) return std::nullopt;
        stored = *collection;
    } else if (kind == QStringLiteral("double")) {
        const double number = value.toDouble();
        if (!std::isfinite(number)) return std::nullopt;
        stored = QString::number(number, 'g', 17);
    } else stored = value.toString();
    return QJsonObject{{QStringLiteral("kind"), kind},
                       {QStringLiteral("value"), stored}};
}

std::optional<QVariant> decodeValue(const QJsonValue &json, QMetaType expected,
                                    bool legacyNumericDouble)
{
    if (!json.isObject()) return std::nullopt;
    const QJsonObject object = json.toObject();
    if (!onlyKeys(object, {"kind", "value"})) return std::nullopt;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    if (kind.isEmpty() || typeFor(kind) != expected) return std::nullopt;
    const QJsonValue stored = object.value(QStringLiteral("value"));
    if (kind == QStringLiteral("bool")) {
        if (!stored.isBool()) return std::nullopt;
        return QVariant(stored.toBool());
    }
    if (kind == QStringLiteral("string")) {
        if (!stored.isString()) return std::nullopt;
        return QVariant(stored.toString());
    }
    if (kind == QStringLiteral("bytes")) {
        if (!stored.isString()) return std::nullopt;
        const QByteArray encoded = stored.toString().toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(encoded);
        if (!decoded || decoded.decoded.toBase64() != encoded) return std::nullopt;
        return QVariant(decoded.decoded);
    }
    if (kind == QStringLiteral("objectCollection")) {
        if (!stored.isArray()) return std::nullopt;
        const auto collection = ReferenceCollectionValue::decode(stored.toArray());
        if (!collection) return std::nullopt;
        return QVariant(*collection);
    }
    if (kind == QStringLiteral("double")) {
        double number = 0.0;
        if (legacyNumericDouble && stored.isDouble()) number = stored.toDouble();
        else {
            if (!stored.isString()) return std::nullopt;
            bool ok = false;
            number = stored.toString().toDouble(&ok);
            if (!ok) return std::nullopt;
        }
        if (!std::isfinite(number)) return std::nullopt;
        return QVariant(number);
    }
    if (!stored.isString()) return std::nullopt;
    if (kind == QStringLiteral("int") || kind == QStringLiteral("int64")) {
        qint64 number = 0;
        if (!canonicalSigned(stored.toString(), &number)) return std::nullopt;
        if (kind == QStringLiteral("int")) {
            if (number < std::numeric_limits<int>::min()
                || number > std::numeric_limits<int>::max()) return std::nullopt;
            return QVariant(static_cast<int>(number));
        }
        return QVariant::fromValue(static_cast<qlonglong>(number));
    }
    quint64 number = 0;
    if (!canonicalUnsigned(stored.toString(), &number)) return std::nullopt;
    if (kind == QStringLiteral("uint")) {
        if (number > std::numeric_limits<uint>::max()) return std::nullopt;
        return QVariant(static_cast<uint>(number));
    }
    return QVariant::fromValue(static_cast<qulonglong>(number));
}

QStringList sortedNames(const QSet<QString> &names)
{
    QStringList sorted = names.values();
    sorted.sort();
    return sorted;
}

QJsonArray encodedNames(const QSet<QString> &names)
{
    QJsonArray array;
    for (const auto &name : sortedNames(names)) array.append(name);
    return array;
}

std::optional<QSet<QString>> decodeNames(const QJsonValue &value,
                                         const QSet<QString> &known)
{
    if (!value.isArray()) return std::nullopt;
    QSet<QString> names;
    for (const auto &item : value.toArray()) {
        if (!item.isString() || !known.contains(item.toString())
            || names.contains(item.toString())) return std::nullopt;
        names.insert(item.toString());
    }
    return names;
}
} // namespace

SnapshotPtr ReferenceDocumentCodec::capture(const PropertyDocument &document,
                                            const QJsonObject &extensions,
                                            OperationError *error)
{
    if (error) *error = {};
    QJsonArray properties;
    QStringList propertyNames = document.m_descriptors.keys();
    propertyNames.sort();
    for (const auto &name : propertyNames) {
        const auto &entry = document.m_descriptors[name];
        const auto defaultValue = encodeValue(entry.defaultValue);
        if (!defaultValue) {
            fail(error, ErrorCode::Unsupported,
                 QStringLiteral("Property type cannot be serialized"), name);
            return {};
        }
        properties.append(QJsonObject{{QStringLiteral("id"), entry.id},
                                      {QStringLiteral("type"), kindFor(entry.type)},
                                      {QStringLiteral("default"), *defaultValue},
                                      {QStringLiteral("unitId"), entry.unitId},
                                      {QStringLiteral("writable"), entry.writable},
                                      {QStringLiteral("animatable"), entry.animatable}});
    }
    QJsonArray entities;
    QStringList entityIds = document.m_entities.keys();
    entityIds.sort();
    for (const auto &id : entityIds) {
        const auto &state = document.m_entities[id];
        QJsonObject values;
        for (const auto &name : propertyNames) {
            const auto encoded = encodeValue(state.values.value(name));
            if (!encoded) {
                fail(error, ErrorCode::Unsupported,
                     QStringLiteral("Entity value cannot be serialized"), name);
                return {};
            }
            values.insert(name, *encoded);
        }
        entities.append(QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("values"), values},
                                    {QStringLiteral("readOnly"), encodedNames(state.readOnlyProperties)},
                                    {QStringLiteral("animated"), encodedNames(state.animatedProperties)}});
    }
    const QJsonObject root{{QStringLiteral("format"), QString::fromLatin1(formatId)},
                           {QStringLiteral("version"), CurrentVersion},
                           {QStringLiteral("documentId"), document.documentIdString()},
                           {QStringLiteral("revision"), document.revisionString()},
                           {QStringLiteral("resetGeneration"), document.resetGenerationString()},
                           {QStringLiteral("properties"), properties},
                           {QStringLiteral("entities"), entities},
                           {QStringLiteral("extensions"), extensions}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaximumBytes) {
        fail(error, ErrorCode::Unsupported, QStringLiteral("Reference document exceeds size limit"));
        return {};
    }
    return std::make_shared<const ImmutableSnapshot>(ImmutableSnapshot{document.header(), bytes});
}

std::optional<DecodedReferenceDocument> ReferenceDocumentCodec::decode(
    const QByteArray &bytes, OperationError *error)
{
    if (error) *error = {};
    if (bytes.isEmpty() || bytes.size() > MaximumBytes) {
        fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid document size"));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        fail(error, ErrorCode::InvalidArgument, QStringLiteral("Malformed document JSON"));
        return std::nullopt;
    }
    const QJsonObject root = json.object();
    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if (root.value(QStringLiteral("format")).toString() != QString::fromLatin1(formatId)
        || (version != 1 && version != CurrentVersion)) {
        fail(error, ErrorCode::Unsupported, QStringLiteral("Unsupported document format/version"));
        return std::nullopt;
    }
    if (version == CurrentVersion
        ? !onlyKeys(root, {"format", "version", "documentId", "revision",
                           "resetGeneration", "properties", "entities", "extensions"})
        : !onlyKeys(root, {"format", "version", "documentId", "revision",
                           "properties", "entities"})) {
        fail(error, ErrorCode::Unsupported, QStringLiteral("Unknown document field"));
        return std::nullopt;
    }
    const DocumentId documentId = DocumentId::parse(root.value(QStringLiteral("documentId")).toString());
    quint64 revision = 0;
    quint64 generation = 0;
    if (!documentId.isValid()
        || !canonicalUnsigned(root.value(QStringLiteral("revision")).toString(), &revision)
        || (version == CurrentVersion && !canonicalUnsigned(
                root.value(QStringLiteral("resetGeneration")).toString(), &generation))
        || !root.value(QStringLiteral("properties")).isArray()
        || !root.value(QStringLiteral("entities")).isArray()) {
        fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid document header"));
        return std::nullopt;
    }
    const QJsonArray properties = root.value(QStringLiteral("properties")).toArray();
    const QJsonArray entities = root.value(QStringLiteral("entities")).toArray();
    if (properties.size() > 100000 || entities.size() > 100000) {
        fail(error, ErrorCode::Unsupported, QStringLiteral("Reference document item limit exceeded"));
        return std::nullopt;
    }
    auto document = std::make_unique<PropertyDocument>();
    for (const auto &property : properties) {
        if (!property.isObject()) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid property descriptor"));
            return std::nullopt;
        }
        const QJsonObject entry = property.toObject();
        if (!onlyKeys(entry, {"id", "type", "default", "unitId",
                              "writable", "animatable"})) {
            fail(error, ErrorCode::Unsupported, QStringLiteral("Unknown property field"));
            return std::nullopt;
        }
        const QString name = entry.value(QStringLiteral("id")).toString();
        const QMetaType type = typeFor(entry.value(QStringLiteral("type")).toString());
        const auto defaultValue = decodeValue(entry.value(QStringLiteral("default")), type,
                                              version == 1);
        if (name.isEmpty() || !type.isValid() || !defaultValue
            || !entry.value(QStringLiteral("unitId")).isString()
            || !entry.value(QStringLiteral("writable")).isBool()
            || !entry.value(QStringLiteral("animatable")).isBool()
            || !document->defineProperty({name, type, *defaultValue,
                                         entry.value(QStringLiteral("unitId")).toString(),
                                         entry.value(QStringLiteral("writable")).toBool(),
                                         entry.value(QStringLiteral("animatable")).toBool()})) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid or duplicate property"), name);
            return std::nullopt;
        }
    }
    const QSet<QString> known = QSet<QString>(document->m_descriptors.keyBegin(),
                                               document->m_descriptors.keyEnd());
    for (const auto &entity : entities) {
        if (!entity.isObject()) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid entity"));
            return std::nullopt;
        }
        const QJsonObject entry = entity.toObject();
        if (version == CurrentVersion
            ? !onlyKeys(entry, {"id", "values", "readOnly", "animated"})
            : !onlyKeys(entry, {"id", "values"})) {
            fail(error, ErrorCode::Unsupported, QStringLiteral("Unknown entity field"));
            return std::nullopt;
        }
        const EntityId id = EntityId::parse(entry.value(QStringLiteral("id")).toString());
        if (!id.isValid() || document->m_entities.contains(id.toString())
            || !entry.value(QStringLiteral("values")).isObject()) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid or duplicate entity"));
            return std::nullopt;
        }
        const QJsonObject values = entry.value(QStringLiteral("values")).toObject();
        if (values.size() != known.size()) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("Entity property set differs from schema"));
            return std::nullopt;
        }
        PropertyDocument::EntityState state;
        for (const auto &name : known) {
            const auto decoded = decodeValue(values.value(name), document->m_descriptors[name].type,
                                             version == 1);
            if (!decoded) {
                fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid entity value"), name);
                return std::nullopt;
            }
            state.values.insert(name, *decoded);
        }
        if (version == CurrentVersion) {
            const auto readOnly = decodeNames(entry.value(QStringLiteral("readOnly")), known);
            const auto animated = decodeNames(entry.value(QStringLiteral("animated")), known);
            if (!readOnly || !animated) {
                fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid entity metadata"));
                return std::nullopt;
            }
            for (const auto &name : *animated)
                if (!document->m_descriptors[name].animatable) {
                    fail(error, ErrorCode::InvalidArgument, QStringLiteral("Nonanimatable property marked animated"), name);
                    return std::nullopt;
                }
            state.readOnlyProperties = *readOnly;
            state.animatedProperties = *animated;
        }
        document->m_entities.insert(id.toString(), state);
    }
    QJsonObject extensions;
    if (version == CurrentVersion) {
        const QJsonValue extras = root.value(QStringLiteral("extensions"));
        if (!extras.isObject()) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("Invalid extensions object"));
            return std::nullopt;
        }
        extensions = extras.toObject();
    }
    document->m_documentId = documentId;
    document->m_revision = revision;
    document->m_resetGeneration = generation;
    return DecodedReferenceDocument{std::move(document), extensions, version == 1};
}

} // namespace QindaTK
