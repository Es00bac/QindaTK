// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QVector>
#include <optional>

namespace QindaTK {

enum class TransferOperation { Copy, Move, Link };
enum class TransferItemKind { Text, Image, Range, Object };

struct TransferItem {
    EntityId entityId;
    TransferItemKind kind = TransferItemKind::Object;
    QJsonObject payload;
};

struct TransferPayload {
    DocumentId sourceDocumentId;
    RevisionId sourceRevision;
    TransferOperation operation = TransferOperation::Copy;
    QVector<TransferItem> items;
};

// A pure versioned serializer/parser. It does not access the OS clipboard,
// resolve entities, or mutate a document. External formats are projections of
// one-item text/image payloads; object and range data stay in the typed format.
class TransferPayloadCodec final {
public:
    static constexpr int CurrentVersion = 1;
    static constexpr qsizetype MaximumBytes = 16 * 1024 * 1024;
    static constexpr qsizetype MaximumItems = 4096;
    static constexpr qsizetype MaximumTextBytes = 4 * 1024 * 1024;
    static constexpr qsizetype MaximumImageBytes = 8 * 1024 * 1024;

    static constexpr const char *InternalMime =
        "application/x-qindatk-transfer+json";

    static QByteArray encode(const TransferPayload &payload,
                             OperationError *error = nullptr);
    static std::optional<TransferPayload> decode(
        const QByteArray &bytes, OperationError *error = nullptr);
    static QMap<QString, QByteArray> mimeAlternatives(
        const TransferPayload &payload, OperationError *error = nullptr);
};

} // namespace QindaTK
