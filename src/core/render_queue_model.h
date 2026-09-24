// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "job_model.h"
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

namespace QindaTK {

struct RenderProduct {
    QJsonArray files; // {file, ...provider metadata}; queue adds size and sha256.
    QString error;
};
using RenderProvider = std::function<RenderProduct(const QJsonObject &snapshot,
    const QString &stageDirectory, const CancellationToken &, const JobProgress &)>;

// AGENT-CONTRACT: hosts register providers before opening a journal. Providers
// receive only immutable JSON and a private stage directory, never live QObjects.
class RenderQueueModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString storagePath READ storagePath NOTIFY changed)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(int maxResourceSlots READ maxResourceSlots WRITE setMaxResourceSlots NOTIFY changed)
public:
    enum Role { IdRole = Qt::UserRole + 1, LabelRole, StateRole, ProgressRole,
                ErrorRole, ResultRole, OutputPathRole, ProviderRole, FormatRole,
                SourceIdRole, DependenciesRole, AttemptRole };
    explicit RenderQueueModel(QObject *parent = nullptr);
    ~RenderQueueModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString storagePath() const { return m_storagePath; }
    QVariantList jobs() const;
    QString lastError() const { return m_lastError; }
    int maxResourceSlots() const { return m_maxResourceSlots; }
    void setMaxResourceSlots(int value);
    void registerProvider(const QString &id, const QString &version, const QStringList &formats,
                          RenderProvider provider);
    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE QString enqueueJob(const QVariantMap &snapshot);
    Q_INVOKABLE bool cancelJob(const QString &id);
    Q_INVOKABLE bool retryJob(const QString &id);
    Q_INVOKABLE QVariantMap job(const QString &id) const;
    Q_INVOKABLE bool verifyOutput(const QString &id);
    Q_INVOKABLE void start();
    static RenderProduct renderCaptionPngSequence(const QJsonObject &snapshot,
        const QString &stageDirectory, const CancellationToken &, const JobProgress &);
signals:
    void changed();
    void jobFinished(const QString &id, const QString &state);
private:
    struct ProviderEntry { QString version; QStringList formats; RenderProvider work; };
    struct Entry {
        QJsonObject snapshot;
        QString state = QStringLiteral("queued");
        QString error;
        QJsonObject result;
        int attempt = 0;
        int progress = 0;
    };
    int indexOf(const QString &id) const;
    bool persist();
    bool validate(const QJsonObject &snapshot, QString *error) const;
    void pump();
    void finish(const QString &id, JobModel::JobState state, const QVariantMap &result, const QString &error);
    void update(int row);
    QString stagePath(const Entry &entry) const;
    QString m_storagePath;
    QString m_lastError;
    QVector<Entry> m_entries;
    QHash<QString, ProviderEntry> m_providers;
    std::unique_ptr<JobModel> m_jobs;
    int m_maxResourceSlots = 4;
    int m_usedSlots = 0;
    bool m_open = false;
};

} // namespace QindaTK
