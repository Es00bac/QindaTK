// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QUuid>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <memory>
#include <optional>

namespace QindaTK {

// IDs are distinct C++ types even though their wire representation is UUID.
struct EntityId {
    QUuid value;
    static EntityId create() { return {QUuid::createUuid()}; }
    static EntityId parse(const QString &text) { return {QUuid(text)}; }
    bool isValid() const { return !value.isNull(); }
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
    friend bool operator==(const EntityId &, const EntityId &) = default;
};

struct DocumentId {
    QUuid value;
    static DocumentId create() { return {QUuid::createUuid()}; }
    static DocumentId parse(const QString &text) { return {QUuid(text)}; }
    bool isValid() const { return !value.isNull(); }
    QString toString() const { return value.toString(QUuid::WithoutBraces); }
    friend bool operator==(const DocumentId &, const DocumentId &) = default;
};

struct RevisionId {
    quint64 value = 0;
    QString toDecimal() const { return QString::number(value); }
    static std::optional<RevisionId> parseDecimal(const QString &text);
    friend bool operator==(const RevisionId &, const RevisionId &) = default;
};

enum class UnitDimension { Scalar, Length, Angle, Duration, Frequency, Gain };
struct UnitQuantity {
    double value = 0.0;
    UnitDimension dimension = UnitDimension::Scalar;
    QString unitId;
    bool isValid() const;
};

// Exact timeline position; consumers choose a domain-specific rational rate.
// No QML Number is used for ticks or rate components.
struct ExactTime {
    qint64 ticks = 0;
    qint64 rateNumerator = 1;
    qint64 rateDenominator = 1;
    bool isValid() const { return rateNumerator > 0 && rateDenominator > 0; }
    friend bool operator==(const ExactTime &, const ExactTime &) = default;
};

// A color value carries its profile and transfer identity; a conversion
// engine is required before blending values from different profiles.
struct ColorValue {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double alpha = 1.0;
    QString profileId;
    QString transferId;
    bool isValid() const;
};

enum class ErrorCode {
    None, InvalidArgument, Unsupported, Unavailable, Cancelled,
    StaleRevision, StaleGeneration, Validation, BackendFailure, Conflict
};
struct OperationError {
    ErrorCode code = ErrorCode::None;
    QString message;
    QString fieldPath;
    QString backendId;
    explicit operator bool() const { return code != ErrorCode::None; }
};
struct ValidationIssue {
    QString fieldPath;
    QString message;
    ErrorCode code = ErrorCode::Validation;
};

// A model reset increments resetGeneration. A request or delegate holding an
// older generation must resolve its entity IDs again before applying work.
struct SnapshotHeader {
    QString schemaId;
    DocumentId documentId;
    RevisionId revision;
    quint64 resetGeneration = 0;
};
struct ImmutableSnapshot {
    SnapshotHeader header;
    QByteArray payload;
};
using SnapshotPtr = std::shared_ptr<const ImmutableSnapshot>;

// AGENT-CONTRACT (docs/formats.md#snapshot-sources): a document model that is
// not a PropertyDocument hands FormatRegistry an immutable snapshot of its
// committed state (FormatRegistry.startExportSource). Workbook implements it.
class SnapshotSource {
public:
    virtual ~SnapshotSource() = default;
    virtual SnapshotPtr exportSnapshot() const = 0;
};

class CancellationToken {
public:
    CancellationToken() = default;
    bool isCancelled() const;
private:
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> state)
        : m_state(std::move(state)) {}
    std::shared_ptr<std::atomic_bool> m_state;
    friend class CancellationSource;
};
class CancellationSource {
public:
    CancellationSource() : m_state(std::make_shared<std::atomic_bool>(false)) {}
    CancellationToken token() const { return CancellationToken(m_state); }
    void cancel() const { if (m_state) m_state->store(true, std::memory_order_release); }
private:
    std::shared_ptr<std::atomic_bool> m_state;
};

struct RequestContext {
    QString operationId;
    SnapshotHeader expected;
    QVector<EntityId> targets;
    CancellationToken cancellation;
    bool accepts(const SnapshotHeader &current) const;
};

// Immutable dispatch inputs. WP-011 resolves the live context stack and
// snapshots it here; WP-012 owns transaction commit/rollback. A focused view
// and selected entities are IDs, never delegate or QObject pointers.
struct CommandInvocationContext {
    RequestContext request;
    EntityId focusedView;
    QVector<EntityId> selection;
    QString focusScopeId;
    QString toolId;
};
struct EditRequestContext {
    CommandInvocationContext command;
    QUuid transactionId;
    QString mergeKey;
};

// QObject services, models and QML Items stay on the GUI thread. Workers
// receive SnapshotPtr and CancellationToken by value and return versioned
// results; render/realtime callbacks never dereference GUI QObjects.
enum class ExecutionDomain { Gui, Worker, Render, Realtime };

class ContractCodec {
public:
    static QVariantMap toQml(const SnapshotHeader &header);
    static std::optional<SnapshotHeader> snapshotHeaderFromQml(
        const QVariantMap &value, OperationError *error = nullptr);
    static QVariantMap toQml(const ExactTime &time);
    static std::optional<ExactTime> exactTimeFromQml(
        const QVariantMap &value, OperationError *error = nullptr);
};

} // namespace QindaTK

Q_DECLARE_METATYPE(QindaTK::EntityId)
Q_DECLARE_METATYPE(QindaTK::DocumentId)
Q_DECLARE_METATYPE(QindaTK::RevisionId)
Q_DECLARE_METATYPE(QindaTK::SnapshotHeader)
