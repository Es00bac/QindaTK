// SPDX-License-Identifier: LGPL-3.0-or-later
#include "transfer_payload.h"

#include <QBuffer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QImageReader>
#include <QSet>
#include <cmath>

namespace QindaTK {
namespace {
constexpr auto formatId = "qindatk.transfer";
constexpr auto textMime = "text/plain";
constexpr auto imageMime = "image/png";
constexpr auto pngSignature = "\x89PNG\r\n\x1a\n";

void fail(OperationError *error, ErrorCode code, const QString &message,
          const QString &path = {})
{
    if (error) *error = {code, message, path, {}};
}

bool hasOnlyKeys(const QJsonObject &object,
                 std::initializer_list<const char *> names)
{
    QSet<QString> allowed;
    for (const char *name : names) allowed.insert(QString::fromLatin1(name));
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key())) return false;
    return true;
}

QString operationName(TransferOperation operation)
{
    switch (operation) {
    case TransferOperation::Copy: return QStringLiteral("copy");
    case TransferOperation::Move: return QStringLiteral("move");
    case TransferOperation::Link: return QStringLiteral("link");
    }
    return {};
}

std::optional<TransferOperation> parseOperation(const QString &name)
{
    if (name == QStringLiteral("copy")) return TransferOperation::Copy;
    if (name == QStringLiteral("move")) return TransferOperation::Move;
    if (name == QStringLiteral("link")) return TransferOperation::Link;
    return std::nullopt;
}

QString kindName(TransferItemKind kind)
{
    switch (kind) {
    case TransferItemKind::Text: return QStringLiteral("text");
    case TransferItemKind::Image: return QStringLiteral("image");
    case TransferItemKind::Range: return QStringLiteral("range");
    case TransferItemKind::Object: return QStringLiteral("object");
    }
    return {};
}

std::optional<TransferItemKind> parseKind(const QString &name)
{
    if (name == QStringLiteral("text")) return TransferItemKind::Text;
    if (name == QStringLiteral("image")) return TransferItemKind::Image;
    if (name == QStringLiteral("range")) return TransferItemKind::Range;
    if (name == QStringLiteral("object")) return TransferItemKind::Object;
    return std::nullopt;
}

bool validJsonValue(const QJsonValue &value, int depth, qsizetype *nodes,
                    QString *reason)
{
    if (++*nodes > 100000) {
        *reason = QStringLiteral("payload contains too many JSON values");
        return false;
    }
    if (depth > 32) {
        *reason = QStringLiteral("payload nesting exceeds 32 levels");
        return false;
    }
    if (value.isString()) {
        if (value.toString().toUtf8().size() > TransferPayloadCodec::MaximumBytes) {
            *reason = QStringLiteral("a string exceeds the 16 MiB envelope limit");
            return false;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &child : value.toArray())
            if (!validJsonValue(child, depth + 1, nodes, reason)) return false;
    } else if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key().size() > 4096
                || !validJsonValue(it.value(), depth + 1, nodes, reason)) {
                if (reason->isEmpty())
                    *reason = QStringLiteral("an object key exceeds 4096 characters");
                return false;
            }
        }
    } else if (value.isDouble() && !std::isfinite(value.toDouble())) {
        *reason = QStringLiteral("non-finite numbers are not valid JSON payload data");
        return false;
    }
    return true;
}

bool validItem(const TransferItem &item, QString *why)
{
    if (!item.entityId.isValid()) {
        *why = QStringLiteral("entityId must be a valid stable UUID");
        return false;
    }
    if (kindName(item.kind).isEmpty()) {
        *why = QStringLiteral("item kind is unsupported");
        return false;
    }
    if (item.kind == TransferItemKind::Text) {
        if (!hasOnlyKeys(item.payload, {"text"})
            || !item.payload.value(QStringLiteral("text")).isString()) {
            *why = QStringLiteral("text items require only a string 'text' field");
            return false;
        }
        if (item.payload.value(QStringLiteral("text")).toString().toUtf8().size()
            > TransferPayloadCodec::MaximumTextBytes) {
            *why = QStringLiteral("text item exceeds the 4 MiB limit");
            return false;
        }
    } else if (item.kind == TransferItemKind::Image) {
        if (!hasOnlyKeys(item.payload, {"pngBase64"})
            || !item.payload.value(QStringLiteral("pngBase64")).isString()) {
            *why = QStringLiteral("image items require only a base64 'pngBase64' field");
            return false;
        }
        const QByteArray encoded = item.payload.value(QStringLiteral("pngBase64"))
                                      .toString().toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(
            encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.toBase64() != encoded
            || decoded.decoded.size() > TransferPayloadCodec::MaximumImageBytes
            || !decoded.decoded.startsWith(QByteArray(pngSignature, 8))) {
            *why = QStringLiteral("image must be canonical base64 PNG data no larger than 8 MiB");
            return false;
        }
        QBuffer buffer;
        buffer.setData(decoded.decoded);
        if (!buffer.open(QIODevice::ReadOnly)) {
            *why = QStringLiteral("image PNG bytes could not be read");
            return false;
        }
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        if (!reader.canRead() || reader.format().toLower() != QByteArrayLiteral("png")
            || !size.isValid() || size.width() > 32768 || size.height() > 32768
            || qint64(size.width()) * size.height() > 100000000) {
            *why = QStringLiteral("image must be a readable PNG within dimension limits");
            return false;
        }
    }
    qsizetype nodes = 0;
    if (!validJsonValue(item.payload, 0, &nodes, why)) return false;
    return true;
}

QJsonObject itemObject(const TransferItem &item)
{
    return {{QStringLiteral("entityId"), item.entityId.toString()},
            {QStringLiteral("kind"), kindName(item.kind)},
            {QStringLiteral("payload"), item.payload}};
}

QJsonObject rootObject(const TransferPayload &payload)
{
    QJsonArray items;
    for (const TransferItem &item : payload.items) items.append(itemObject(item));
    return {{QStringLiteral("format"), QString::fromLatin1(formatId)},
            {QStringLiteral("version"), TransferPayloadCodec::CurrentVersion},
            {QStringLiteral("sourceDocumentId"), payload.sourceDocumentId.toString()},
            {QStringLiteral("sourceRevision"), payload.sourceRevision.toDecimal()},
            {QStringLiteral("operation"), operationName(payload.operation)},
            {QStringLiteral("items"), items}};
}

bool canonicalUuid(const QString &text, const EntityId &id)
{
    return id.isValid() && id.toString() == text;
}

bool canonicalUuid(const QString &text, const DocumentId &id)
{
    return id.isValid() && id.toString() == text;
}
} // namespace

QByteArray TransferPayloadCodec::encode(const TransferPayload &payload,
                                         OperationError *error)
{
    if (error) *error = {};
    if (!payload.sourceDocumentId.isValid()) {
        fail(error, ErrorCode::InvalidArgument,
             QStringLiteral("sourceDocumentId must be valid"),
             QStringLiteral("sourceDocumentId"));
        return {};
    }
    if (operationName(payload.operation).isEmpty()) {
        fail(error, ErrorCode::Unsupported,
             QStringLiteral("transfer operation is unsupported"),
             QStringLiteral("operation"));
        return {};
    }
    if (payload.items.isEmpty() || payload.items.size() > MaximumItems) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("transfer requires 1 to 4096 items"),
             QStringLiteral("items"));
        return {};
    }
    QSet<QString> identities;
    for (qsizetype i = 0; i < payload.items.size(); ++i) {
        QString why;
        const TransferItem &item = payload.items[i];
        if (!validItem(item, &why)) {
            fail(error, ErrorCode::Validation, why,
                 QStringLiteral("items[%1]").arg(i));
            return {};
        }
        if (identities.contains(item.entityId.toString())) {
            fail(error, ErrorCode::Validation,
                 QStringLiteral("duplicate entity IDs make the transfer ambiguous"),
                 QStringLiteral("items[%1].entityId").arg(i));
            return {};
        }
        identities.insert(item.entityId.toString());
    }
    const QByteArray bytes = QJsonDocument(rootObject(payload))
                                 .toJson(QJsonDocument::Compact);
    if (bytes.size() > MaximumBytes) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("transfer payload exceeds the 16 MiB limit"));
        return {};
    }
    return bytes;
}

std::optional<TransferPayload> TransferPayloadCodec::decode(
    const QByteArray &bytes, OperationError *error)
{
    if (error) *error = {};
    if (bytes.isEmpty() || bytes.size() > MaximumBytes) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("transfer payload must be between 1 byte and 16 MiB"));
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("transfer payload is not a valid JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).toString() != QString::fromLatin1(formatId)) {
        fail(error, ErrorCode::Unsupported,
             QStringLiteral("transfer payload format is unsupported"),
             QStringLiteral("format"));
        return std::nullopt;
    }
    if (!root.value(QStringLiteral("version")).isDouble()
        || root.value(QStringLiteral("version")).toInt(-1) != CurrentVersion) {
        fail(error, ErrorCode::Unsupported,
             QStringLiteral("transfer payload version is unsupported"),
             QStringLiteral("version"));
        return std::nullopt;
    }
    if (!hasOnlyKeys(root, {"format", "version", "sourceDocumentId",
                            "sourceRevision", "operation", "items"})) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("transfer payload contains unknown top-level fields"));
        return std::nullopt;
    }
    const QString documentText = root.value(QStringLiteral("sourceDocumentId")).toString();
    const DocumentId documentId = DocumentId::parse(documentText);
    const QString revisionText = root.value(QStringLiteral("sourceRevision")).toString();
    bool revisionOk = false;
    const quint64 revision = revisionText.toULongLong(&revisionOk, 10);
    if (!root.value(QStringLiteral("sourceDocumentId")).isString()
        || !canonicalUuid(documentText, documentId)
        || !root.value(QStringLiteral("sourceRevision")).isString()
        || !revisionOk || QString::number(revision) != revisionText) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("source document ID and canonical decimal revision are required"),
             QStringLiteral("sourceDocumentId"));
        return std::nullopt;
    }
    const auto operation = parseOperation(root.value(QStringLiteral("operation")).toString());
    if (!root.value(QStringLiteral("operation")).isString() || !operation) {
        fail(error, ErrorCode::Unsupported,
             QStringLiteral("transfer operation is unsupported"),
             QStringLiteral("operation"));
        return std::nullopt;
    }
    if (!root.value(QStringLiteral("items")).isArray()) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("items must be an array"), QStringLiteral("items"));
        return std::nullopt;
    }
    const QJsonArray array = root.value(QStringLiteral("items")).toArray();
    if (array.isEmpty() || array.size() > MaximumItems) {
        fail(error, ErrorCode::Validation,
             QStringLiteral("transfer requires 1 to 4096 items"),
             QStringLiteral("items"));
        return std::nullopt;
    }
    TransferPayload result{documentId, RevisionId{revision}, *operation, {}};
    QSet<QString> identities;
    for (qsizetype i = 0; i < array.size(); ++i) {
        if (!array[i].isObject()) {
            fail(error, ErrorCode::Validation,
                 QStringLiteral("each transfer item must be an object"),
                 QStringLiteral("items[%1]").arg(i));
            return std::nullopt;
        }
        const QJsonObject object = array[i].toObject();
        if (!hasOnlyKeys(object, {"entityId", "kind", "payload"})
            || !object.value(QStringLiteral("entityId")).isString()
            || !object.value(QStringLiteral("kind")).isString()
            || !object.value(QStringLiteral("payload")).isObject()) {
            fail(error, ErrorCode::Validation,
                 QStringLiteral("item fields or value types are invalid"),
                 QStringLiteral("items[%1]").arg(i));
            return std::nullopt;
        }
        const QString idText = object.value(QStringLiteral("entityId")).toString();
        const EntityId id = EntityId::parse(idText);
        if (!canonicalUuid(idText, id) || identities.contains(idText)) {
            fail(error, ErrorCode::Validation,
                 QStringLiteral("entity ID is invalid, noncanonical, or duplicated"),
                 QStringLiteral("items[%1].entityId").arg(i));
            return std::nullopt;
        }
        const auto kind = parseKind(object.value(QStringLiteral("kind")).toString());
        if (!kind) {
            fail(error, ErrorCode::Unsupported,
                 QStringLiteral("transfer item kind is unsupported"),
                 QStringLiteral("items[%1].kind").arg(i));
            return std::nullopt;
        }
        TransferItem item{id, *kind, object.value(QStringLiteral("payload")).toObject()};
        QString why;
        if (!validItem(item, &why)) {
            fail(error, ErrorCode::Validation, why,
                 QStringLiteral("items[%1].payload").arg(i));
            return std::nullopt;
        }
        identities.insert(idText);
        result.items.append(std::move(item));
    }
    qsizetype nodes = 0;
    QString reason;
    if (!validJsonValue(root, 0, &nodes, &reason)) {
        fail(error, ErrorCode::Validation, reason);
        return std::nullopt;
    }
    return result;
}

QMap<QString, QByteArray> TransferPayloadCodec::mimeAlternatives(
    const TransferPayload &payload, OperationError *error)
{
    if (error) *error = {};
    const QByteArray internal = encode(payload, error);
    if (internal.isEmpty()) return {};
    QMap<QString, QByteArray> result;
    result.insert(QString::fromLatin1(InternalMime), internal);
    if (payload.items.size() != 1) return result;
    const TransferItem &item = payload.items.front();
    if (item.kind == TransferItemKind::Text) {
        result.insert(QString::fromLatin1(textMime),
                      item.payload.value(QStringLiteral("text")).toString().toUtf8());
    } else if (item.kind == TransferItemKind::Image) {
        const auto decoded = QByteArray::fromBase64Encoding(
            item.payload.value(QStringLiteral("pngBase64")).toString().toLatin1(),
            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded || decoded.decoded.size() > MaximumImageBytes
            || !decoded.decoded.startsWith(QByteArray(pngSignature, 8))) {
            fail(error, ErrorCode::Validation,
                 QStringLiteral("image item is not a supported PNG"));
            return {};
        }
        QBuffer buffer;
        buffer.setData(decoded.decoded);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        if (!reader.canRead() || reader.format().toLower() != QByteArrayLiteral("png")) {
            fail(error, ErrorCode::Validation,
                 QStringLiteral("image item is not a readable PNG"));
            return {};
        }
        result.insert(QString::fromLatin1(imageMime), decoded.decoded);
    }
    return result;
}

} // namespace QindaTK
