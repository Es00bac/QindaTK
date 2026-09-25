// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <functional>
#include <memory>

namespace QindaTK {

// A bounded trace snapshot. Raw records carry exact decimal tick strings and
// explicit clock domains; conversion, validation and sorting run off-thread.
// Files (this index's export format or Chrome Trace Event JSON) are read and
// parsed off-thread too; see trace_import.cpp.
class TraceIndex : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int rejectedCount READ rejectedCount NOTIFY changed)
    Q_PROPERTY(int truncatedCount READ truncatedCount NOTIFY changed)
    Q_PROPERTY(QStringList threads READ threads NOTIFY changed)
    Q_PROPERTY(QString startNs READ startNs NOTIFY changed)
    Q_PROPERTY(QString endNs READ endNs NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit TraceIndex(QObject *parent = nullptr) : QObject(parent) {}
    ~TraceIndex() override;
    bool busy() const { return m_busy; }
    int count() const { return m_records.size(); }
    int rejectedCount() const { return m_rejected; }
    int truncatedCount() const { return m_truncated; }
    QStringList threads() const { return m_threads; }
    QString startNs() const { return m_records.isEmpty() ? QString{} : QString::number(m_start); }
    QString endNs() const { return m_records.isEmpty() ? QString{} : QString::number(m_end); }
    QString lastError() const { return m_error; }

    Q_INVOKABLE bool setClockDomains(const QVariantList &domains);
    Q_INVOKABLE QString ingestAsync(const QVariantList &records);
    // JSON text or a local file (path or file: URL) in this index's export
    // format or Chrome Trace Event format. Clock domains come from the data.
    Q_INVOKABLE QString ingestJsonAsync(const QString &json);
    Q_INVOKABLE QString ingestFileAsync(const QString &pathOrUrl);
    // Stops the running analysis; the current snapshot stays.
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap record(const QString &id) const;
    Q_INVOKABLE QVariantList childrenOf(const QString &spanId) const;
    Q_INVOKABLE QStringList ancestorsOf(const QString &recordId) const;
    Q_INVOKABLE QVariantList queryWindow(double fromFraction, double toFraction,
                                         const QString &threadId = {},
                                         const QString &textFilter = {},
                                         int limit = 500) const;
    Q_INVOKABLE QVariantList queryRangeNs(const QString &fromNs, const QString &toNs,
                                          const QString &threadId = {},
                                          const QString &textFilter = {},
                                          int limit = 500) const;
    // Thread tracks in display order: [{threadId, firstRow, lanes}].
    Q_INVOKABLE QVariantList tracks(const QString &threadId = {}) const;
    // Aggregated marks for a time window (fractions of the snapshot) drawn
    // into `columns` pixel columns; see trace_layout.cpp for the contract.
    Q_INVOKABLE QVariantMap layoutWindow(double fromFraction, double toFraction, int columns,
                                         const QString &threadId = {},
                                         const QString &textFilter = {},
                                         int firstRow = 0, int rowCount = 64) const;
    // The record drawn at pixel `column` of layout row `row` (the longest
    // one covering that pixel), or "" when none is there.
    Q_INVOKABLE QString recordAt(double fromFraction, double toFraction, int columns,
                                 int row, int column, const QString &threadId = {},
                                 const QString &textFilter = {}) const;
    // Synchronous export, for small snapshots and tests. Views use
    // exportJsonAsync, which serializes a copy of the snapshot on a worker.
    Q_INVOKABLE QString exportJson() const;
    Q_INVOKABLE QString exportJsonAsync();

signals:
    void changed();
    void analysisReady(const QString &requestId, int retainedCount);
    void analysisDiscarded(const QString &requestId);
    void exportReady(const QString &requestId, const QString &json);

private:
    struct ClockDomain {
        QString id;
        qint64 nanosPerTick = 0;
        qint64 offsetNs = 0;
    };
    struct Record {
        QString id;
        QString kind;
        QString name;
        QString category;
        QString threadId;
        QString parentId;
        QString clockDomain;
        qint64 startTicks = 0;
        qint64 endTicks = 0;
        qint64 startNs = 0;
        qint64 endNs = 0;
        int lane = 0;  // row inside the thread's track; see assignLanes
    };
    struct Analysis {
        QVector<Record> records;
        QStringList threads;
        QHash<QString, int> lanes;  // thread -> lane count
        QString error;
        QHash<QString, ClockDomain> domains;
        bool replaceDomains = false;
        bool cancelled = false;
        int rejected = 0;
        int truncated = 0;
        qint64 start = 0;
        qint64 end = 0;
    };
    using CancelFlag = std::shared_ptr<std::atomic_bool>;
    static Analysis analyze(const QVariantList &input,
                            const QHash<QString, ClockDomain> &domains,
                            const CancelFlag &cancel);
    static Analysis analyzeJson(const QByteArray &bytes, const CancelFlag &cancel);
    static Analysis analyzeFile(const QString &path, const CancelFlag &cancel);
    static Analysis chromeTrace(const QVariantList &events, const CancelFlag &cancel);
    static bool assignLanes(Analysis *result, const CancelFlag &cancel);
    static QString serialize(const QVector<Record> &records,
                             const QHash<QString, ClockDomain> &domains,
                             int truncated, int rejected);
    static bool parseDomains(const QVariantList &domains, QHash<QString, ClockDomain> *out);
    QString startJob(std::function<Analysis(const CancelFlag &)> job);
    static QVariantMap recordMap(const Record &record);
    static bool decimal(const QVariant &value, qint64 *out);
    static bool convert(qint64 ticks, const ClockDomain &domain, qint64 *ns);
    void publish(Analysis result, quint64 generation);
    QHash<QString, ClockDomain> m_domains;
    QVector<Record> m_records;
    QHash<QString, int> m_byId;
    QStringList m_threads;
    QHash<QString, int> m_lanes;
    QString m_error;
    qint64 m_start = 0;
    qint64 m_end = 0;
    CancelFlag m_cancel;
    quint64 m_generation = 0;
    quint64 m_exportSerial = 0;
    int m_rejected = 0;
    int m_truncated = 0;
    bool m_busy = false;
};

} // namespace QindaTK
