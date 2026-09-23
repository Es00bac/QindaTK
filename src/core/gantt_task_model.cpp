// SPDX-License-Identifier: LGPL-3.0-or-later
#include "gantt_task_model.h"

#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace QindaTK {

QVariantMap GanttTransactionAdapter::previewMove(const QString &, const QString &,
                                                  const QVariantList &, GanttCalendar *)
{ return {{QStringLiteral("error"), QStringLiteral("Host scheduling adapter has no preview implementation")}}; }
bool GanttTransactionAdapter::commitMove(const QVariantMap &, const QVariantList &) { return false; }
void GanttTransactionAdapter::cancelMove(const QVariantMap &) {}
QVariantList GanttTransactionAdapter::undoMove() { return {}; }

QVariantMap GanttSimpleScheduleAdapter::previewMove(const QString &taskId,
    const QString &startUtc, const QVariantList &tasks, GanttCalendar *calendar)
{
    if (!calendar) return {{QStringLiteral("error"), QStringLiteral("Working calendar is required")}};
    if (calendar->snapForward(startUtc).isEmpty())
        return {{QStringLiteral("error"), calendar->lastError()}};
    QVariantList result = tasks;
    QHash<QString, int> index;
    for (int i = 0; i < tasks.size(); ++i)
        index.insert(tasks.at(i).toMap().value(QStringLiteral("taskId")).toString(), i);
    if (!index.contains(taskId))
        return {{QStringLiteral("error"), QStringLiteral("Unknown task ID")}};
    QHash<QString, int> indegree;
    QHash<QString, QStringList> successors;
    for (const QVariant &value : tasks) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("taskId")).toString();
        const QVariantList dependencies = row.value(QStringLiteral("dependencies")).toList();
        indegree.insert(id, dependencies.size());
        for (const QVariant &v : dependencies) successors[v.toString()].append(id);
    }
    QStringList ready;
    for (const QVariant &value : tasks) {
        const QString id = value.toMap().value(QStringLiteral("taskId")).toString();
        if (indegree.value(id) == 0) ready.append(id);
    }
    int visited = 0;
    for (int head = 0; head < ready.size(); ++head) {
        const QString id = ready.at(head);
        const int i = index.value(id);
        QVariantMap row = result.at(i).toMap();
        const QVariantList dependencies = row.value(QStringLiteral("dependencies")).toList();
        const QString requested = id == taskId ? startUtc : row.value(QStringLiteral("startUtc")).toString();
        const QString snapped = calendar->snapForward(requested);
        qint64 startMs = 0;
        if (!GanttCalendar::parseUtc(snapped, &startMs))
            return {{QStringLiteral("error"), calendar->lastError()}};
        for (const QVariant &v : dependencies) {
            const QVariantMap predecessor = result.at(index.value(v.toString())).toMap();
            const QString end = calendar->addWorkingMinutes(
                predecessor.value(QStringLiteral("startUtc")).toString(),
                predecessor.value(QStringLiteral("durationMinutes")).toLongLong());
            qint64 endMs = 0;
            if (!GanttCalendar::parseUtc(end, &endMs))
                return {{QStringLiteral("error"), calendar->lastError()}};
            startMs = std::max(startMs, endMs);
        }
        row.insert(QStringLiteral("startUtc"), calendar->snapForward(QString::number(startMs)));
        result[i] = row;
        ++visited;
        for (const QString &next : successors.value(id))
            if (--indegree[next] == 0) ready.append(next);
    }
    if (visited != result.size())
        return {{QStringLiteral("error"), QStringLiteral("Dependency cycle")}};
    return {{QStringLiteral("tasks"), result}, {QStringLiteral("taskId"), taskId}};
}

bool GanttSimpleScheduleAdapter::commitMove(const QVariantMap &preview, const QVariantList &before)
{
    if (!preview.contains(QStringLiteral("tasks"))) return false;
    m_undo.append(before);
    return true;
}
QVariantList GanttSimpleScheduleAdapter::undoMove()
{
    if (m_undo.isEmpty()) return {};
    return m_undo.takeLast();
}

GanttTaskModel::GanttTaskModel(QObject *parent) : QAbstractListModel(parent) {}
int GanttTaskModel::rowCount(const QModelIndex &parent) const
{ return parent.isValid() ? 0 : m_tasks.size(); }
QHash<int, QByteArray> GanttTaskModel::roleNames() const
{
    return {{TaskIdRole, "taskId"}, {LabelRole, "label"}, {StartUtcRole, "startUtc"},
        {DurationMinutesRole, "durationMinutes"}, {ProgressRole, "progress"},
        {BaselineStartUtcRole, "baselineStartUtc"},
        {BaselineDurationMinutesRole, "baselineDurationMinutes"},
        {DependenciesRole, "dependencies"}};
}
QVariant GanttTaskModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size()) return {};
    const QVariantMap row = m_tasks.at(index.row()).toMap();
    return row.value(QString::fromLatin1(roleNames().value(role)));
}
void GanttTaskModel::setCalendar(GanttCalendar *calendar)
{ if (m_calendar != calendar) { cancelPreview(); m_calendar = calendar; emit calendarChanged(); } }
void GanttTaskModel::setTransactionAdapter(GanttTransactionAdapter *adapter)
{ if (m_adapter != adapter) { cancelPreview(); m_adapter = adapter; emit transactionAdapterChanged(); } }
void GanttTaskModel::setError(const QString &message)
{ if (m_error != message) { m_error = message; emit errorChanged(); } }

bool GanttTaskModel::validate(const QVariantList &rows, QString *error) const
{
    if (rows.size() > 100000) { *error = QStringLiteral("Task count exceeds 100,000"); return false; }
    QSet<QString> ids;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("taskId")).toString();
        qint64 instant = 0;
        if (QUuid(id).isNull() || id != QUuid(id).toString(QUuid::WithoutBraces) || ids.contains(id)) {
            *error = QStringLiteral("Task IDs must be unique canonical UUID strings for shared SelectionModel identity"); return false;
        }
        if (row.value(QStringLiteral("startUtc")).metaType().id() != QMetaType::QString
            || !GanttCalendar::parseUtc(row.value(QStringLiteral("startUtc")).toString(), &instant)) {
            *error = QStringLiteral("startUtc must be an exact decimal UTC millisecond string"); return false;
        }
        bool durationOk = false;
        const QVariant duration = row.value(QStringLiteral("durationMinutes"));
        const qint64 durationValue = duration.toLongLong(&durationOk);
        if (!durationOk || duration.toDouble() != double(durationValue)
            || durationValue < 0 || durationValue > 52560000) {
            *error = QStringLiteral("durationMinutes must be a nonnegative integer within 100 years"); return false;
        }
        bool progressOk = false;
        const double progress = row.value(QStringLiteral("progress"), 0).toDouble(&progressOk);
        if (!progressOk || !std::isfinite(progress) || progress < 0 || progress > 1) {
            *error = QStringLiteral("progress must be between 0 and 1"); return false;
        }
        if (row.contains(QStringLiteral("baselineStartUtc"))
            && (row.value(QStringLiteral("baselineStartUtc")).metaType().id() != QMetaType::QString
                || !GanttCalendar::parseUtc(row.value(QStringLiteral("baselineStartUtc")).toString(), nullptr))) {
            *error = QStringLiteral("baselineStartUtc must be an exact UTC string"); return false;
        }
        if (row.contains(QStringLiteral("baselineDurationMinutes"))) {
            bool baselineOk = false;
            const qint64 baseline = row.value(QStringLiteral("baselineDurationMinutes")).toLongLong(&baselineOk);
            if (!baselineOk || row.value(QStringLiteral("baselineDurationMinutes")).toDouble() != double(baseline)
                || baseline < 0 || baseline > 52560000) {
                *error = QStringLiteral("Baseline duration must be nonnegative"); return false;
            }
        }
        if (row.contains(QStringLiteral("dependencies"))
            && row.value(QStringLiteral("dependencies")).metaType().id() != QMetaType::QVariantList) {
            *error = QStringLiteral("dependencies must be an array of task IDs"); return false;
        }
        ids.insert(id);
    }
    QHash<QString, QStringList> links;
    for (const QVariant &value : rows) {
        const QVariantMap row = value.toMap();
        const QString id = row.value(QStringLiteral("taskId")).toString();
        QSet<QString> unique;
        for (const QVariant &dep : row.value(QStringLiteral("dependencies")).toList()) {
            const QString predecessor = dep.toString();
            if (!ids.contains(predecessor) || predecessor == id || unique.contains(predecessor)) {
                *error = QStringLiteral("Dependencies must identify distinct other tasks"); return false;
            }
            unique.insert(predecessor);
            links[id].append(predecessor);
        }
    }
    QHash<QString, int> indegree;
    QHash<QString, QStringList> successors;
    for (const QString &id : ids) {
        indegree.insert(id, links.value(id).size());
        for (const QString &dep : links.value(id)) successors[dep].append(id);
    }
    QStringList ready;
    for (const QString &id : ids) if (indegree.value(id) == 0) ready.append(id);
    int visited = 0;
    for (int head = 0; head < ready.size(); ++head) {
        ++visited;
        for (const QString &next : successors.value(ready.at(head))) {
            if (--indegree[next] == 0) ready.append(next);
        }
    }
    if (visited != ids.size()) { *error = QStringLiteral("Dependency cycle"); return false; }
    return true;
}

bool GanttTaskModel::replaceTasks(const QVariantList &rows)
{
    QString error;
    if (!validate(rows, &error)) { setError(error); return false; }
    cancelPreview();
    beginResetModel(); m_tasks = rows;
    m_index.clear();
    for (int i = 0; i < m_tasks.size(); ++i)
        m_index.insert(get(i).value(QStringLiteral("taskId")).toString(), i);
    endResetModel();
    ++m_revision; setError({}); emit rowsChanged();
    return true;
}
QVariantMap GanttTaskModel::get(int row) const
{ return row >= 0 && row < m_tasks.size() ? m_tasks.at(row).toMap() : QVariantMap{}; }
QVariantMap GanttTaskModel::displayGet(int row) const
{
    const QVariantList rows = m_preview.value(QStringLiteral("tasks")).toList();
    return row >= 0 && row < m_tasks.size()
        ? (rows.isEmpty() ? get(row) : rows.at(row).toMap()) : QVariantMap{};
}
int GanttTaskModel::indexOfId(const QString &taskId) const
{ return m_index.value(taskId, -1); }
bool GanttTaskModel::previewMove(const QString &taskId, const QString &startUtc)
{
    if (!m_adapter || !m_calendar) { setError(QStringLiteral("Calendar and transaction adapter are required")); return false; }
    if (indexOfId(taskId) < 0) { setError(QStringLiteral("Unknown task ID")); return false; }
    cancelPreview();
    QVariantMap preview = m_adapter->previewMove(taskId, startUtc, m_tasks, m_calendar);
    QString error;
    const QVariantList rows = preview.value(QStringLiteral("tasks")).toList();
    if (rows.size() != m_tasks.size()) error = QStringLiteral("Preview must contain every task");
    else validate(rows, &error);
    if (!error.isEmpty()) {
        m_adapter->cancelMove(preview);
        setError(preview.value(QStringLiteral("error"), error).toString()); return false;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).toMap().value(QStringLiteral("taskId")) != get(i).value(QStringLiteral("taskId"))) {
            m_adapter->cancelMove(preview);
            setError(QStringLiteral("Preview must preserve task order and IDs")); return false;
        }
    }
    preview.insert(QStringLiteral("baseRevision"), QVariant::fromValue(m_revision));
    m_preview = preview; setError({}); emit previewChanged();
    return true;
}
bool GanttTaskModel::commitPreview()
{
    if (!previewActive() || !m_adapter || m_preview.value(QStringLiteral("baseRevision")).toULongLong() != m_revision) {
        setError(QStringLiteral("No current preview to commit")); return false;
    }
    if (!m_adapter->commitMove(m_preview, m_tasks)) {
        setError(QStringLiteral("Host rejected schedule commit")); return false;
    }
    const QVariantList rows = m_preview.value(QStringLiteral("tasks")).toList();
    m_preview.clear(); emit previewChanged();
    beginResetModel(); m_tasks = rows; endResetModel();
    ++m_revision; setError({}); emit rowsChanged(); emit committed();
    return true;
}
void GanttTaskModel::cancelPreview()
{
    if (m_preview.isEmpty()) return;
    if (m_adapter) m_adapter->cancelMove(m_preview);
    m_preview.clear(); emit previewChanged();
}
bool GanttTaskModel::undo()
{
    if (!m_adapter) return false;
    cancelPreview();
    const QVariantList rows = m_adapter->undoMove();
    if (rows.isEmpty()) return false;
    return replaceTasks(rows);
}

} // namespace QindaTK
