// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "gantt_calendar.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Hosts may replace this with a scheduling engine that validates resources,
// constraints and project-specific links. The model owns no document undo.
class GanttTransactionAdapter : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit GanttTransactionAdapter(QObject *parent = nullptr) : QObject(parent) {}
    virtual QVariantMap previewMove(const QString &taskId, const QString &startUtc,
                                    const QVariantList &tasks, GanttCalendar *calendar);
    virtual bool commitMove(const QVariantMap &preview, const QVariantList &before);
    virtual void cancelMove(const QVariantMap &preview);
    virtual QVariantList undoMove();
};

// Reference finish-to-start scheduler. A production host can subclass the
// adapter and supply richer constraints while retaining the view contract.
// AGENT-NOTE: it treats every link as FS whatever `linkTypes` says, and drops
// `finishUtc` from the rows it moves so the view falls back to the duration;
// hosts that own exact finishes (several shifts, exceptions) subclass it.
class GanttSimpleScheduleAdapter : public GanttTransactionAdapter {
    Q_OBJECT
    QML_ELEMENT
public:
    using GanttTransactionAdapter::GanttTransactionAdapter;
    QVariantMap previewMove(const QString &taskId, const QString &startUtc,
                            const QVariantList &tasks, GanttCalendar *calendar) override;
    bool commitMove(const QVariantMap &preview, const QVariantList &before) override;
    QVariantList undoMove() override;
private:
    QVector<QVariantList> m_undo;
};

// AGENT-GUARD: source and preview rows retain identical stable task IDs and
// order. A DataTable can consume get(index), and a shared SelectionModel can
// select by taskId while view delegates are recycled.
class GanttTaskModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(GanttCalendar *calendar READ calendar WRITE setCalendar NOTIFY calendarChanged)
    Q_PROPERTY(GanttTransactionAdapter *transactionAdapter READ transactionAdapter WRITE setTransactionAdapter NOTIFY transactionAdapterChanged)
    Q_PROPERTY(int count READ count NOTIFY rowsChanged)
    Q_PROPERTY(bool previewActive READ previewActive NOTIFY previewChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Role { TaskIdRole = Qt::UserRole + 1, LabelRole, StartUtcRole,
                DurationMinutesRole, ProgressRole, BaselineStartUtcRole,
                BaselineDurationMinutesRole, DependenciesRole,
                // Optional presentation fields (docs/gantt.md "Host-scheduled rows").
                FinishUtcRole, BaselineFinishUtcRole, KindRole, CriticalRole,
                InactiveRole, OutlineLevelRole, BarTextRole, LinkTypesRole };
    Q_ENUM(Role)
    explicit GanttTaskModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int count() const { return m_tasks.size(); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    GanttCalendar *calendar() const { return m_calendar; }
    void setCalendar(GanttCalendar *calendar);
    GanttTransactionAdapter *transactionAdapter() const { return m_adapter; }
    void setTransactionAdapter(GanttTransactionAdapter *adapter);
    bool previewActive() const { return !m_preview.isEmpty(); }
    QString lastError() const { return m_error; }
    Q_INVOKABLE bool replaceTasks(const QVariantList &rows);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap displayGet(int row) const;
    Q_INVOKABLE int indexOfId(const QString &taskId) const;
    Q_INVOKABLE bool previewMove(const QString &taskId, const QString &startUtc);
    Q_INVOKABLE bool commitPreview();
    Q_INVOKABLE void cancelPreview();
    Q_INVOKABLE bool undo();
signals:
    void rowsChanged();
    void previewChanged();
    void calendarChanged();
    void transactionAdapterChanged();
    void errorChanged();
    void committed();
private:
    bool validate(const QVariantList &rows, QString *error) const;
    static bool validateOptional(const QVariantMap &row, qint64 start, QString *error);
    void setError(const QString &message);
    QVariantList m_tasks;
    QHash<QString, int> m_index;
    QVariantMap m_preview;
    QPointer<GanttCalendar> m_calendar;
    QPointer<GanttTransactionAdapter> m_adapter;
    QString m_error;
    quint64 m_revision = 0;
};

} // namespace QindaTK
