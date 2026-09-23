// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include "property_document.h"
#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QPointer>
#include <QThreadPool>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <atomic>
#include <memory>

namespace QindaTK {

struct JobResult {
    QVariantMap output;
    OperationError error;
    // A durable, irreversible output has been committed. Cancellation after
    // this point cannot truthfully label the deliverable as canceled.
    bool committed = false;
};
using JobProgress = std::function<void(double)>;
using JobWork = std::function<JobResult(const CancellationToken &, const JobProgress &)>;

struct JobSpec {
    QString id;
    QString label;
    int priority = 0;
    SnapshotHeader expected;
    QPointer<PropertyDocument> document;
    QString viewId;
    JobWork work;
};

// GUI-thread observable queue. Workers receive only copied JobWork inputs and
// a cancellation token; no worker callback may touch a live QObject.
class JobModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int maxConcurrent READ maxConcurrent WRITE setMaxConcurrent NOTIFY limitsChanged)
    Q_PROPERTY(int maxQueued READ maxQueued WRITE setMaxQueued NOTIFY limitsChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY changed)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    enum JobState { Queued, Running, Cancelling, Succeeded, Failed, Cancelled, Stale };
    Q_ENUM(JobState)
    enum Role { IdRole = Qt::UserRole + 1, LabelRole, StateRole, ProgressRole,
                PriorityRole, DocumentIdRole, ViewIdRole, ErrorRole, ResultRole };
    explicit JobModel(QObject *parent = nullptr);
    ~JobModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int count);
    int maxQueued() const { return m_maxQueued; }
    void setMaxQueued(int count);
    int runningCount() const { return m_running; }
    int queuedCount() const { return m_pending.size(); }
    QString lastError() const { return m_lastError; }
    QString enqueue(JobSpec spec);
    Q_INVOKABLE bool cancelJob(const QString &id);
    Q_INVOKABLE void invalidateView(const QString &viewId);
    Q_INVOKABLE void invalidateDocument(const QString &documentId);
    Q_INVOKABLE QVariantMap job(const QString &id) const;
    Q_INVOKABLE void clearFinished();
signals:
    void changed();
    void limitsChanged();
    void jobFinished(const QString &id, JobState state, const QVariantMap &result,
                     const QString &error);
private:
    struct Entry {
        JobSpec spec;
        CancellationSource cancellation;
        JobState state = Queued;
        int progress = 0;
        quint64 viewGeneration = 0;
        QVariantMap result;
        QString error;
        QFutureWatcher<JobResult> *watcher = nullptr;
        quint64 sequence = 0;
        bool invalidated = false;
        std::shared_ptr<std::atomic_int> reportedProgress =
            std::make_shared<std::atomic_int>(0);
    };
    int rowOf(const QString &id) const;
    void updateRow(int row);
    void dispatch();
    void finish(const std::shared_ptr<Entry> &entry, const JobResult &result);
    void prune();
    QVector<std::shared_ptr<Entry>> m_entries;
    QVector<std::shared_ptr<Entry>> m_pending;
    QHash<QString, quint64> m_viewGenerations;
    QThreadPool m_pool;
    QString m_lastError;
    quint64 m_sequence = 0;
    int m_running = 0;
    int m_maxConcurrent = 4;
    int m_maxQueued = 64;
    int m_maxRetained = 256;
};

} // namespace QindaTK
