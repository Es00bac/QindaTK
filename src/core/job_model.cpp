// SPDX-License-Identifier: LGPL-3.0-or-later
#include "job_model.h"
#include <QPromise>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
bool terminal(JobModel::JobState state)
{
    return state == JobModel::Succeeded || state == JobModel::Failed
        || state == JobModel::Cancelled || state == JobModel::Stale;
}

QString stateName(JobModel::JobState state)
{
    switch (state) {
    case JobModel::Queued: return QStringLiteral("queued");
    case JobModel::Running: return QStringLiteral("running");
    case JobModel::Cancelling: return QStringLiteral("cancelling");
    case JobModel::Succeeded: return QStringLiteral("succeeded");
    case JobModel::Failed: return QStringLiteral("failed");
    case JobModel::Cancelled: return QStringLiteral("cancelled");
    case JobModel::Stale: return QStringLiteral("stale");
    }
    return {};
}

bool sameHeader(const SnapshotHeader &a, const SnapshotHeader &b)
{
    return a.schemaId == b.schemaId && a.documentId == b.documentId
        && a.revision == b.revision && a.resetGeneration == b.resetGeneration;
}
} // namespace

JobModel::JobModel(QObject *parent) : QAbstractListModel(parent)
{
    m_pool.setMaxThreadCount(m_maxConcurrent);
    m_pool.setExpiryTimeout(30000);
}

JobModel::~JobModel()
{
    for (const auto &entry : m_entries) entry->cancellation.cancel();
    m_pool.waitForDone();
}

int JobModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QHash<int, QByteArray> JobModel::roleNames() const
{
    return {{IdRole, "jobId"}, {LabelRole, "label"}, {StateRole, "jobState"},
            {ProgressRole, "progress"}, {PriorityRole, "priority"},
            {DocumentIdRole, "documentId"}, {ViewIdRole, "viewId"},
            {ErrorRole, "error"}, {ResultRole, "result"}};
}

QVariant JobModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const Entry &entry = *m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole: return entry.spec.label;
    case IdRole: return entry.spec.id;
    case LabelRole: return entry.spec.label;
    case StateRole: return stateName(entry.state);
    case ProgressRole: return entry.progress;
    case PriorityRole: return entry.spec.priority;
    case DocumentIdRole: return entry.spec.expected.documentId.isValid()
        ? entry.spec.expected.documentId.toString() : QString{};
    case ViewIdRole: return entry.spec.viewId;
    case ErrorRole: return entry.error;
    case ResultRole: return entry.result;
    default: return {};
    }
}

int JobModel::rowOf(const QString &id) const
{
    for (int row = 0; row < m_entries.size(); ++row)
        if (m_entries[row]->spec.id == id) return row;
    return -1;
}

void JobModel::updateRow(int row)
{
    if (row >= 0 && row < m_entries.size()) {
        const QModelIndex at = index(row);
        emit dataChanged(at, at);
    }
    emit changed();
}

void JobModel::setMaxConcurrent(int count)
{
    if (count < 1 || count > 16 || count == m_maxConcurrent) return;
    m_maxConcurrent = count;
    m_pool.setMaxThreadCount(count);
    emit limitsChanged();
    dispatch();
}

void JobModel::setMaxQueued(int count)
{
    if (count < 1 || count > 4096 || count == m_maxQueued) return;
    m_maxQueued = count;
    emit limitsChanged();
}

QString JobModel::enqueue(JobSpec spec)
{
    if (QThread::currentThread() != thread() || !spec.work) {
        m_lastError = QStringLiteral("A GUI-thread call and worker function are required");
        emit changed();
        return {};
    }
    if (spec.id.isEmpty()) spec.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (rowOf(spec.id) >= 0 || m_pending.size() >= m_maxQueued
        || (spec.expected.documentId.isValid() && !spec.document)
        || (spec.document && (!spec.expected.documentId.isValid()
                              || !sameHeader(spec.document->header(), spec.expected)))) {
        m_lastError = QStringLiteral("Duplicate, saturated or invalid job context");
        emit changed();
        return {};
    }
    auto entry = std::make_shared<Entry>();
    entry->spec = std::move(spec);
    entry->sequence = ++m_sequence;
    entry->viewGeneration = m_viewGenerations.value(entry->spec.viewId);
    const int row = m_entries.size();
    beginInsertRows({}, row, row);
    m_entries.append(entry);
    endInsertRows();
    m_pending.append(entry);
    m_lastError.clear();
    emit changed();
    dispatch();
    return entry->spec.id;
}

void JobModel::dispatch()
{
    if (QThread::currentThread() != thread()) return;
    while (m_running < m_maxConcurrent && !m_pending.isEmpty()) {
        auto best = std::max_element(m_pending.begin(), m_pending.end(),
            [](const auto &a, const auto &b) {
                return a->spec.priority == b->spec.priority
                    ? a->sequence > b->sequence : a->spec.priority < b->spec.priority;
            });
        const auto entry = *best;
        m_pending.erase(best);
        entry->state = Running;
        ++m_running;
        updateRow(rowOf(entry->spec.id));
        auto *watcher = new QFutureWatcher<JobResult>(this);
        entry->watcher = watcher;
        connect(watcher, &QFutureWatcher<JobResult>::progressValueChanged, this,
                [this, entry](int value) {
            if (entry->state == Running || entry->state == Cancelling) {
                entry->progress = value;
                updateRow(rowOf(entry->spec.id));
            }
        });
        connect(watcher, &QFutureWatcher<JobResult>::finished, this,
                [this, entry, watcher]() {
            JobResult result;
            try {
                result = watcher->result();
            } catch (const std::exception &exception) {
                result.error = {ErrorCode::BackendFailure,
                    QString::fromUtf8(exception.what()), {}, {}};
            } catch (...) {
                result.error = {ErrorCode::BackendFailure,
                    QStringLiteral("Unknown worker failure"), {}, {}};
            }
            entry->watcher = nullptr;
            entry->progress = std::max(entry->progress,
                entry->reportedProgress->load(std::memory_order_acquire));
            watcher->deleteLater();
            --m_running;
            finish(entry, result);
            dispatch();
        });
        const JobWork work = entry->spec.work;
        const CancellationToken token = entry->cancellation.token();
        const auto reportedProgress = entry->reportedProgress;
        watcher->setFuture(QtConcurrent::run(&m_pool,
            [work, token, reportedProgress](QPromise<JobResult> &promise) {
                promise.setProgressRange(0, 100);
                int last = -1;
                const JobProgress report = [&promise, &last, reportedProgress](double fraction) {
                    if (!std::isfinite(fraction)) return;
                    const int value = std::clamp(int(std::lround(fraction * 100.0)), 0, 100);
                    if (value != last) {
                        last = value;
                        reportedProgress->store(value, std::memory_order_release);
                        promise.setProgressValue(value);
                    }
                };
                try {
                    promise.addResult(work(token, report));
                } catch (const std::exception &exception) {
                    promise.addResult({{}, {ErrorCode::BackendFailure,
                        QString::fromUtf8(exception.what()), {}, {}}});
                } catch (...) {
                    promise.addResult({{}, {ErrorCode::BackendFailure,
                        QStringLiteral("Unknown worker failure"), {}, {}}});
                }
            }));
    }
}

void JobModel::finish(const std::shared_ptr<Entry> &entry, const JobResult &result)
{
    if (entry->invalidated
        || (entry->spec.expected.documentId.isValid()
            && (!entry->spec.document
                   || !sameHeader(entry->spec.document->header(), entry->spec.expected)))) {
        entry->state = Stale;
        entry->error = QStringLiteral("Document closed or revision changed");
    } else if (!entry->spec.viewId.isEmpty()
               && m_viewGenerations.value(entry->spec.viewId) != entry->viewGeneration) {
        entry->state = Stale;
        entry->error = QStringLiteral("View generation changed");
    } else if (entry->cancellation.token().isCancelled()) {
        entry->state = Cancelled;
        entry->error = QStringLiteral("Cancelled");
    } else if (result.error) {
        entry->state = Failed;
        entry->error = result.error.message;
    } else {
        entry->state = Succeeded;
        entry->progress = 100;
        entry->result = result.output;
    }
    updateRow(rowOf(entry->spec.id));
    emit jobFinished(entry->spec.id, entry->state, entry->result, entry->error);
    prune();
}

bool JobModel::cancelJob(const QString &id)
{
    if (QThread::currentThread() != thread()) return false;
    const int row = rowOf(id);
    if (row < 0) return false;
    const auto entry = m_entries.at(row);
    if (terminal(entry->state)) return false;
    entry->cancellation.cancel();
    if (entry->state == Queued) {
        m_pending.removeOne(entry);
        entry->state = entry->invalidated ? Stale : Cancelled;
        entry->error = entry->invalidated ? QStringLiteral("Context invalidated")
                                         : QStringLiteral("Cancelled");
        updateRow(row);
        emit jobFinished(id, entry->state, {}, entry->error);
        prune();
    } else {
        entry->state = Cancelling;
        updateRow(row);
    }
    return true;
}

void JobModel::invalidateView(const QString &viewId)
{
    if (QThread::currentThread() != thread() || viewId.isEmpty()) return;
    ++m_viewGenerations[viewId];
    QStringList ids;
    for (const auto &entry : m_entries)
        if (entry->spec.viewId == viewId && !terminal(entry->state)) {
            entry->invalidated = true;
            ids.append(entry->spec.id);
        }
    for (const QString &id : ids) cancelJob(id);
}

void JobModel::invalidateDocument(const QString &documentId)
{
    if (QThread::currentThread() != thread() || documentId.isEmpty()) return;
    QStringList ids;
    for (const auto &entry : m_entries)
        if (entry->spec.expected.documentId.toString() == documentId
            && !terminal(entry->state)) {
            entry->invalidated = true;
            ids.append(entry->spec.id);
        }
    for (const QString &id : ids) cancelJob(id);
}

QVariantMap JobModel::job(const QString &id) const
{
    const int row = rowOf(id);
    if (row < 0) return {};
    const Entry &entry = *m_entries.at(row);
    return {{QStringLiteral("id"), entry.spec.id},
            {QStringLiteral("label"), entry.spec.label},
            {QStringLiteral("state"), stateName(entry.state)},
            {QStringLiteral("progress"), entry.progress},
            {QStringLiteral("priority"), entry.spec.priority},
            {QStringLiteral("documentId"), entry.spec.expected.documentId.isValid()
                ? entry.spec.expected.documentId.toString() : QString{}},
            {QStringLiteral("viewId"), entry.spec.viewId},
            {QStringLiteral("error"), entry.error},
            {QStringLiteral("result"), entry.result}};
}

void JobModel::clearFinished()
{
    if (QThread::currentThread() != thread()) return;
    for (int row = m_entries.size() - 1; row >= 0; --row) {
        if (!terminal(m_entries.at(row)->state)) continue;
        beginRemoveRows({}, row, row);
        m_entries.removeAt(row);
        endRemoveRows();
    }
    emit changed();
}

void JobModel::prune()
{
    while (m_entries.size() > m_maxRetained) {
        int row = -1;
        for (int i = 0; i < m_entries.size(); ++i)
            if (terminal(m_entries.at(i)->state)) { row = i; break; }
        if (row < 0) break;
        beginRemoveRows({}, row, row);
        m_entries.removeAt(row);
        endRemoveRows();
    }
}

} // namespace QindaTK
