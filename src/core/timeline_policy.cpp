// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_policy.h"

namespace QindaTK {

TimelineEditPolicy::TimelineEditPolicy(QObject *parent) : QObject(parent) {}

void TimelineEditPolicy::setOverlapMode(OverlapMode mode)
{
    if (m_overlap == mode) return;
    m_overlap = mode;
    emit changed();
}

void TimelineEditPolicy::setRippleScope(RippleScope scope)
{
    if (m_rippleScope == scope) return;
    m_rippleScope = scope;
    emit changed();
}

void TimelineEditPolicy::setMinimumClipTicksText(const QString &ticks)
{
    const auto parsed = TimelineTime::parseDecimal(ticks);
    if (parsed) setMinimumClipTicks(*parsed);
}

bool TimelineEditPolicy::setMinimumClipTicks(qint64 ticks)
{
    if (ticks < 1) return false;
    if (m_minimumClipTicks != ticks) {
        m_minimumClipTicks = ticks;
        emit changed();
    }
    return true;
}

void TimelineEditPolicy::setAllowNegativeTime(bool allow)
{
    if (m_allowNegativeTime == allow) return;
    m_allowNegativeTime = allow;
    emit changed();
}

void TimelineEditPolicy::setAllowBeyondSource(bool allow)
{
    if (m_allowBeyondSource == allow) return;
    m_allowBeyondSource = allow;
    emit changed();
}

void TimelineEditPolicy::setRequireMatchingTrackKind(bool require)
{
    if (m_requireMatchingKind == require) return;
    m_requireMatchingKind = require;
    emit changed();
}

void TimelineEditPolicy::setMaxChangesPerEdit(int count)
{
    count = qBound(1, count, 1000000);
    if (m_maxChanges == count) return;
    m_maxChanges = count;
    emit changed();
}

QStringList TimelineEditPolicy::operations()
{
    return {QStringLiteral("move"), QStringLiteral("moveAcrossTracks"),
            QStringLiteral("trim"), QStringLiteral("split"), QStringLiteral("delete"),
            QStringLiteral("ripple"), QStringLiteral("markers"), QStringLiteral("tracks"),
            QStringLiteral("range"), QStringLiteral("insert")};
}

bool TimelineEditPolicy::isAvailable(const QString &operation) const
{
    return operations().contains(operation) && !m_unavailable.contains(operation);
}

QString TimelineEditPolicy::unavailableReason(const QString &operation) const
{
    if (!operations().contains(operation))
        return tr("Unknown timeline operation “%1”").arg(operation);
    return m_unavailable.value(operation);
}

bool TimelineEditPolicy::setUnavailable(const QString &operation, const QString &reason)
{
    if (!operations().contains(operation) || reason.trimmed().isEmpty()) return false;
    if (m_unavailable.value(operation) == reason) return true;
    m_unavailable.insert(operation, reason);
    emit changed();
    return true;
}

bool TimelineEditPolicy::setAvailable(const QString &operation)
{
    if (!operations().contains(operation)) return false;
    if (m_unavailable.remove(operation) > 0) emit changed();
    return true;
}

QVariantMap TimelineEditPolicy::capabilities() const
{
    QVariantMap result;
    for (const auto &operation : operations()) {
        result.insert(operation, QVariantMap{
            {QStringLiteral("available"), isAvailable(operation)},
            {QStringLiteral("reason"), m_unavailable.value(operation)}});
    }
    return result;
}

OperationError TimelineEditPolicy::validate(const TimelineModel &model,
                                            const TimelineChangeSet &changes) const
{
    Q_UNUSED(model);
    Q_UNUSED(changes);
    return {};
}

OperationError TimelineEditPolicy::requireOperation(const QString &operation) const
{
    if (isAvailable(operation)) return {};
    return {ErrorCode::Unsupported, unavailableReason(operation), operation, {}};
}

} // namespace QindaTK
