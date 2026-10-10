// SPDX-License-Identifier: LGPL-3.0-or-later
// GanttTaskModel: validation of the planning presentation fields
// (docs/gantt.md "Planning rows", D-312/D-313). Like the D-303 fields they
// change drawing only; the host's engine computes them. One bad value
// rejects the whole replacement and the error names the field.
#include "gantt_task_model.h"

#include <QMetaType>

namespace QindaTK {
namespace {
constexpr int MaximumSegments = 256;
constexpr qint64 MaximumMinutes = 52560000; // 100 years, as durationMinutes

bool exactUtc(const QVariant &value, qint64 *out)
{
    return value.metaType().id() == QMetaType::QString && GanttCalendar::parseUtc(value.toString(), out);
}
} // namespace

bool GanttTaskModel::validatePlanning(const QVariantMap &row, qint64 start, QString *error)
{
    const auto instant = [&](const char *key, qint64 notBefore, bool bounded) {
        const QString name = QString::fromLatin1(key);
        if (!row.contains(name)) return true;
        qint64 value = 0;
        if (!exactUtc(row.value(name), &value) || (bounded && value < notBefore)) {
            *error = bounded ? QStringLiteral("%1 must be an exact UTC string not before its start").arg(name)
                             : QStringLiteral("%1 must be an exact UTC string").arg(name);
            return false;
        }
        return true;
    };
    // Slack ends and the progress point lie on or after the task start; a
    // deadline may fall anywhere (a missed one lies before the finish).
    for (const char *key : {"slackFinishUtc", "freeSlackFinishUtc", "progressUtc"})
        if (!instant(key, start, true)) return false;
    if (!instant("deadlineUtc", 0, false)) return false;

    if (row.contains(QStringLiteral("collapsed"))
        && row.value(QStringLiteral("collapsed")).metaType().id() != QMetaType::Bool) {
        *error = QStringLiteral("collapsed must be a boolean"); return false;
    }
    if (row.contains(QStringLiteral("summaryText"))
        && row.value(QStringLiteral("summaryText")).metaType().id() != QMetaType::QString) {
        *error = QStringLiteral("summaryText must be a string"); return false;
    }

    // The second baseline lane: a start, and a finish or a duration.
    if (!instant("baseline2StartUtc", 0, false)) return false;
    if (row.contains(QStringLiteral("baseline2FinishUtc"))) {
        qint64 laneStart = start;
        if (!exactUtc(row.value(QStringLiteral("baseline2StartUtc")), &laneStart)) {
            *error = QStringLiteral("baseline2FinishUtc needs baseline2StartUtc"); return false;
        }
        if (!instant("baseline2FinishUtc", laneStart, true)) return false;
    }
    if (row.contains(QStringLiteral("baseline2DurationMinutes"))) {
        const QVariant raw = row.value(QStringLiteral("baseline2DurationMinutes"));
        bool ok = false;
        const qint64 minutes = raw.toLongLong(&ok);
        if (!ok || raw.toDouble() != double(minutes) || minutes < 0 || minutes > MaximumMinutes) {
            *error = QStringLiteral("baseline2DurationMinutes must be a nonnegative integer"); return false;
        }
    }

    // Split pieces: sorted, non-overlapping {startUtc, finishUtc}, from the start on.
    if (row.contains(QStringLiteral("segments"))) {
        const QVariant raw = row.value(QStringLiteral("segments"));
        const QVariantList pieces = raw.toList();
        if (raw.metaType().id() != QMetaType::QVariantList || pieces.isEmpty() || pieces.size() > MaximumSegments) {
            *error = QStringLiteral("segments must be an array of 1 to %1 pieces").arg(MaximumSegments);
            return false;
        }
        qint64 previous = start;
        for (const QVariant &entry : pieces) {
            const QVariantMap piece = entry.toMap();
            qint64 from = 0;
            qint64 to = 0;
            if (entry.metaType().id() != QMetaType::QVariantMap
                || !exactUtc(piece.value(QStringLiteral("startUtc")), &from)
                || !exactUtc(piece.value(QStringLiteral("finishUtc")), &to) || to < from || from < previous) {
                *error = QStringLiteral("segments must be ordered, non-overlapping {startUtc, finishUtc} "
                                        "pieces from the task start");
                return false;
            }
            previous = to;
        }
    }
    return true;
}

} // namespace QindaTK
