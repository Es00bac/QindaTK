// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

// Private helpers shared by the collaboration sources. Not installed.
#include "local_property_sync.h"
#include <QStringList>
#include <optional>

namespace QindaTK::sync_detail {

inline constexpr int MaxOperations = 4096;
inline constexpr qsizetype MaxValueBytes = 64 * 1024;
inline constexpr qsizetype MaxSnapshotBytes = 40 * 1024;
inline constexpr int MaxObserved = 1024;
inline constexpr int MaxTextInsert = 4096;
inline constexpr int MaxTextElements = 65536;
inline constexpr int MaxTextRanges = 1024;

inline QString restoreProperty() { return QStringLiteral("__qtk.restore.v1"); }
inline QString entityProperty() { return QStringLiteral("__qtk.entity"); }
inline QString checkpointProperty() { return QStringLiteral("__qtk.checkpoint.v1"); }
inline QString baseElementOp() { return QStringLiteral("base"); }

bool canonicalUuid(const QString &id);
inline QString keyOf(const QString &entity, const QString &property)
{
    return entity + QLatin1Char('/') + property;
}
// AGENT-CONTRACT (docs/collaboration.md "Conflict and history rule"): the
// maximum (logicalTime, actorId, operationId) wins. Every replica and every
// operation kind uses this one order.
bool outranks(const PropertySyncOperation &a, const PropertySyncOperation &b);
bool encodableValue(const QVariant &value);
bool sameOperation(const PropertySyncOperation &a, const PropertySyncOperation &b);
// True when the packet survives the bounded wire codec unchanged.
bool validOperation(const PropertySyncOperation &operation);

QByteArray encodeIdList(const QStringList &ids);
std::optional<QStringList> decodeIdList(const QByteArray &bytes);

struct TextRange {
    QString op;   // "base" or the inserting operation UUID
    int start = 0;
    int count = 0;
};
QByteArray encodeTextRanges(const QVector<TextRange> &ranges);
std::optional<QVector<TextRange>> decodeTextRanges(const QByteArray &bytes);
QString elementId(const QString &op, int index);
// Splits "<op>:<index>"; nullopt unless op is "base" or a canonical UUID.
std::optional<QPair<QString, int>> parseElementId(const QString &id);

struct CheckpointMeta {
    QString name;
    QString createdAt;
};
QString encodeCheckpointMeta(const CheckpointMeta &meta);
std::optional<CheckpointMeta> decodeCheckpointMeta(const QString &meta);

} // namespace QindaTK::sync_detail
