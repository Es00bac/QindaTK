// SPDX-License-Identifier: LGPL-3.0-or-later
#include "application_contracts.h"
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
bool decimalText(const QString &text, bool signedValue)
{
    if (text.isEmpty()) return false;
    qsizetype start = 0;
    if (signedValue && text.front() == QLatin1Char('-')) start = 1;
    if (start == text.size()) return false;
    for (qsizetype i = start; i < text.size(); ++i)
        if (text[i] < QLatin1Char('0') || text[i] > QLatin1Char('9')) return false;
    return true;
}
std::optional<quint64> unsignedDecimal(const QVariant &value)
{
    if (value.metaType().id() != QMetaType::QString) return std::nullopt;
    const QString text = value.toString();
    if (!decimalText(text, false)) return std::nullopt;
    bool ok = false;
    const auto parsed = text.toULongLong(&ok, 10);
    if (!ok) return std::nullopt;
    return parsed;
}
std::optional<qint64> signedDecimal(const QVariant &value)
{
    if (value.metaType().id() != QMetaType::QString) return std::nullopt;
    const QString text = value.toString();
    if (!decimalText(text, true)) return std::nullopt;
    bool ok = false;
    const auto parsed = text.toLongLong(&ok, 10);
    if (!ok) return std::nullopt;
    return parsed;
}
void fail(OperationError *error, const QString &field)
{
    if (error) *error = {ErrorCode::InvalidArgument,
                         QStringLiteral("Invalid or inexact %1").arg(field), field, {}};
}
} // namespace

std::optional<RevisionId> RevisionId::parseDecimal(const QString &text)
{
    const auto value = unsignedDecimal(text);
    if (!value) return std::nullopt;
    return RevisionId{*value};
}

bool UnitQuantity::isValid() const
{
    return std::isfinite(value) && !unitId.isEmpty();
}

bool ColorValue::isValid() const
{
    return std::isfinite(red) && std::isfinite(green) && std::isfinite(blue)
        && std::isfinite(alpha) && alpha >= 0.0 && alpha <= 1.0
        && !profileId.isEmpty() && !transferId.isEmpty();
}

bool CancellationToken::isCancelled() const
{
    return m_state && m_state->load(std::memory_order_acquire);
}

bool RequestContext::accepts(const SnapshotHeader &current) const
{
    return !cancellation.isCancelled() && expected.documentId.isValid()
        && !expected.schemaId.isEmpty() && expected.schemaId == current.schemaId
        && expected.documentId == current.documentId
        && expected.revision == current.revision
        && expected.resetGeneration == current.resetGeneration;
}

QVariantMap ContractCodec::toQml(const SnapshotHeader &header)
{
    return {{QStringLiteral("schemaId"), header.schemaId},
            {QStringLiteral("documentId"), header.documentId.toString()},
            {QStringLiteral("revision"), header.revision.toDecimal()},
            {QStringLiteral("resetGeneration"), QString::number(header.resetGeneration)}};
}

std::optional<SnapshotHeader> ContractCodec::snapshotHeaderFromQml(
    const QVariantMap &value, OperationError *error)
{
    if (error) *error = {};
    const auto document = value.value(QStringLiteral("documentId"));
    const auto schema = value.value(QStringLiteral("schemaId"));
    const auto revision = unsignedDecimal(value.value(QStringLiteral("revision")));
    const auto generation = unsignedDecimal(value.value(QStringLiteral("resetGeneration")));
    if (schema.metaType().id() != QMetaType::QString || schema.toString().isEmpty()) {
        fail(error, QStringLiteral("schemaId")); return std::nullopt;
    }
    if (document.metaType().id() != QMetaType::QString
        || !DocumentId::parse(document.toString()).isValid()) {
        fail(error, QStringLiteral("documentId")); return std::nullopt;
    }
    if (!revision) { fail(error, QStringLiteral("revision")); return std::nullopt; }
    if (!generation) { fail(error, QStringLiteral("resetGeneration")); return std::nullopt; }
    return SnapshotHeader{schema.toString(), DocumentId::parse(document.toString()),
                          RevisionId{*revision}, *generation};
}

QVariantMap ContractCodec::toQml(const ExactTime &time)
{
    return {{QStringLiteral("ticks"), QString::number(time.ticks)},
            {QStringLiteral("rateNumerator"), QString::number(time.rateNumerator)},
            {QStringLiteral("rateDenominator"), QString::number(time.rateDenominator)}};
}

std::optional<ExactTime> ContractCodec::exactTimeFromQml(
    const QVariantMap &value, OperationError *error)
{
    if (error) *error = {};
    const auto ticks = signedDecimal(value.value(QStringLiteral("ticks")));
    const auto numerator = signedDecimal(value.value(QStringLiteral("rateNumerator")));
    const auto denominator = signedDecimal(value.value(QStringLiteral("rateDenominator")));
    if (!ticks) { fail(error, QStringLiteral("ticks")); return std::nullopt; }
    if (!numerator || *numerator <= 0) {
        fail(error, QStringLiteral("rateNumerator")); return std::nullopt;
    }
    if (!denominator || *denominator <= 0) {
        fail(error, QStringLiteral("rateDenominator")); return std::nullopt;
    }
    return ExactTime{*ticks, *numerator, *denominator};
}

} // namespace QindaTK
