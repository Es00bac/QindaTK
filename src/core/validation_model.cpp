// SPDX-License-Identifier: LGPL-3.0-or-later
#include "validation_model.h"

#include <QMetaType>
#include <limits>
#include <utility>

namespace QindaTK {
namespace {
std::optional<quint64> unsignedDecimal(const QString &text)
{
    if (text.isEmpty()) return std::nullopt;
    for (const QChar ch : text)
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) return std::nullopt;
    bool ok = false;
    const quint64 value = text.toULongLong(&ok, 10);
    return ok ? std::optional<quint64>(value) : std::nullopt;
}

bool stringValue(const QVariantMap &map, const QString &key, QString *out,
                 bool required = false)
{
    const auto value = map.value(key);
    if (!value.isValid()) {
        if (required) return false;
        *out = {};
        return true;
    }
    if (value.metaType().id() != QMetaType::QString) return false;
    *out = value.toString();
    return !required || !out->isEmpty();
}
} // namespace

ValidationModel::ValidationModel(QObject *parent) : QObject(parent) {}

QVariantList ValidationModel::issues() const
{
    QVariantList result;
    result.reserve(m_entries.size());
    for (const auto &entry : m_entries) result.push_back(entryMap(entry));
    return result;
}

bool ValidationModel::invalid() const
{
    for (const auto &entry : m_entries)
        if (entry.severity == Error) return true;
    return false;
}

QString ValidationModel::runId() const { return QString::number(m_runId); }
QString ValidationModel::resetGeneration() const
{
    return QString::number(m_snapshot.resetGeneration);
}

QVariantMap ValidationModel::entryMap(const ValidationEntry &entry) const
{
    static const QStringList names{QStringLiteral("error"), QStringLiteral("warning"),
                                   QStringLiteral("info")};
    return {{QStringLiteral("code"), entry.code},
            {QStringLiteral("severity"), names.value(entry.severity)},
            {QStringLiteral("targetId"), entry.target.toString()},
            {QStringLiteral("fieldPath"), entry.fieldPath},
            {QStringLiteral("message"), entry.message},
            {QStringLiteral("remedy"), entry.remedy}};
}

QVariantMap ValidationModel::firstIssue() const
{
    if (m_entries.isEmpty()) return {};
    for (const auto &entry : m_entries)
        if (entry.severity == Error) return entryMap(entry);
    return entryMap(m_entries.front());
}

QString ValidationModel::firstIssueTargetId() const
{
    return firstIssue().value(QStringLiteral("targetId")).toString();
}

QString ValidationModel::firstIssuePath() const
{
    return firstIssue().value(QStringLiteral("fieldPath")).toString();
}

bool ValidationModel::setSnapshot(const QString &documentIdText,
                                  const QString &revisionText,
                                  const QString &generationText)
{
    const DocumentId document = DocumentId::parse(documentIdText);
    const auto revision = RevisionId::parseDecimal(revisionText);
    const auto generation = unsignedDecimal(generationText);
    if (!document.isValid() || !revision || !generation) return false;
    return setSnapshot(SnapshotHeader{QStringLiteral("QindaTK.Validation"), document,
                                     *revision, *generation});
}

bool ValidationModel::setSnapshot(const SnapshotHeader &snapshot)
{
    if (!snapshot.documentId.isValid() || snapshot.schemaId.isEmpty()) return false;
    if (matchesCurrent(snapshot)) return true;
    m_snapshot = snapshot;
    m_pending = false;
    m_entries.clear();
    notifyAll();
    return true;
}

QString ValidationModel::beginRun()
{
    if (!m_snapshot.documentId.isValid() || m_snapshot.schemaId.isEmpty()
        || m_runId == std::numeric_limits<quint64>::max())
        return {};
    ++m_runId;
    m_entries.clear();
    m_pending = true;
    notifyAll();
    return QString::number(m_runId);
}

quint64 ValidationModel::beginRun(const SnapshotHeader &snapshot)
{
    if (!setSnapshot(snapshot)) return 0;
    const auto token = beginRun();
    return token.toULongLong();
}

bool ValidationModel::matchesCurrent(const SnapshotHeader &snapshot) const
{
    return snapshot.schemaId == m_snapshot.schemaId
        && snapshot.documentId == m_snapshot.documentId
        && snapshot.revision == m_snapshot.revision
        && snapshot.resetGeneration == m_snapshot.resetGeneration;
}

bool ValidationModel::completeRun(quint64 completedRunId,
                                  const SnapshotHeader &snapshot,
                                  const QVector<ValidationEntry> &entries)
{
    const bool accepted = m_pending && completedRunId == m_runId
        && matchesCurrent(snapshot);
    if (!accepted) {
        emit runCompleted(QString::number(completedRunId), false);
        return false;
    }
    for (const auto &entry : entries) {
        if (entry.code.trimmed().isEmpty() || entry.message.trimmed().isEmpty()
            || entry.severity < Error || entry.severity > Info
            || (entry.target.isValid() && entry.target.toString().isEmpty())) {
            emit runCompleted(QString::number(completedRunId), false);
            return false;
        }
    }
    m_entries = entries;
    m_pending = false;
    notifyAll();
    emit runCompleted(QString::number(completedRunId), true);
    return true;
}

bool ValidationModel::parseEntry(const QVariantMap &map, ValidationEntry *entry)
{
    if (!stringValue(map, QStringLiteral("code"), &entry->code, true)
        || !stringValue(map, QStringLiteral("message"), &entry->message, true)
        || !stringValue(map, QStringLiteral("fieldPath"), &entry->fieldPath)
        || !stringValue(map, QStringLiteral("remedy"), &entry->remedy))
        return false;
    QString severity;
    if (!stringValue(map, QStringLiteral("severity"), &severity, true)) return false;
    if (severity == QStringLiteral("error")) entry->severity = Error;
    else if (severity == QStringLiteral("warning")) entry->severity = Warning;
    else if (severity == QStringLiteral("info")) entry->severity = Info;
    else return false;

    QString target;
    if (!stringValue(map, QStringLiteral("targetId"), &target)) return false;
    if (!target.isEmpty()) {
        entry->target = EntityId::parse(target);
        if (!entry->target.isValid()) return false;
    }
    return true;
}

bool ValidationModel::completeRun(const QString &runIdText,
                                  const QString &documentIdText,
                                  const QString &revisionText,
                                  const QString &generationText,
                                  const QVariantList &entries)
{
    const auto completedRunId = unsignedDecimal(runIdText);
    const DocumentId document = DocumentId::parse(documentIdText);
    const auto revision = RevisionId::parseDecimal(revisionText);
    const auto generation = unsignedDecimal(generationText);
    if (!completedRunId || !document.isValid() || !revision || !generation) return false;
    QVector<ValidationEntry> parsed;
    parsed.reserve(entries.size());
    for (const auto &value : entries) {
        if (value.metaType().id() != QMetaType::QVariantMap) return false;
        ValidationEntry entry;
        if (!parseEntry(value.toMap(), &entry)) return false;
        parsed.push_back(std::move(entry));
    }
    const SnapshotHeader completedSnapshot{m_snapshot.schemaId, document, *revision, *generation};
    return completeRun(*completedRunId, completedSnapshot, parsed);
}

void ValidationModel::clear()
{
    const bool changed = m_pending || !m_entries.isEmpty();
    m_pending = false;
    m_entries.clear();
    if (changed) notifyAll();
}

void ValidationModel::notifyAll()
{
    emit issuesChanged();
    emit stateChanged();
}

} // namespace QindaTK
