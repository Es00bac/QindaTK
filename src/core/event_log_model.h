// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QVector>
#include <deque>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// GUI-thread event window. Producers may append a batch; retention and model
// notifications are applied once per batch, never once per event.
class EventLogModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int maxEntries READ maxEntries WRITE setMaxEntries NOTIFY limitsChanged)
    Q_PROPERTY(int retainedCount READ retainedCount NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int lastRejectedCount READ lastRejectedCount NOTIFY changed)
    Q_PROPERTY(QString selectedEventId READ selectedEventId NOTIFY selectionChanged)
    Q_PROPERTY(QString severityFilter READ severityFilter WRITE setSeverityFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString sourceFilter READ sourceFilter WRITE setSourceFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString textFilter READ textFilter WRITE setTextFilter NOTIFY filtersChanged)
    Q_PROPERTY(QDateTime fromUtc READ fromUtc WRITE setFromUtc NOTIFY filtersChanged)
    Q_PROPERTY(QDateTime toUtc READ toUtc WRITE setToUtc NOTIFY filtersChanged)
public:
    enum Role { EventIdRole = Qt::UserRole + 1, TimestampRole, SeverityRole,
                SourceRole, MessageRole, DetailsRole };
    Q_ENUM(Role)
    explicit EventLogModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int count() const { return m_visible.size(); }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int maxEntries() const { return m_maxEntries; }
    int retainedCount() const { return m_events.size(); }
    int lastRejectedCount() const { return m_lastRejectedCount; }
    QString selectedEventId() const { return m_selectedId; }
    QString severityFilter() const { return m_severity; }
    QString sourceFilter() const { return m_source; }
    QString textFilter() const { return m_text; }
    QDateTime fromUtc() const { return m_fromUtc; }
    QDateTime toUtc() const { return m_toUtc; }
    void setMaxEntries(int maximum);
    void setSeverityFilter(const QString &filter);
    void setSourceFilter(const QString &filter);
    void setTextFilter(const QString &filter);
    void setFromUtc(const QDateTime &value);
    void setToUtc(const QDateTime &value);
    Q_INVOKABLE int appendEvent(const QVariantMap &event);
    Q_INVOKABLE int appendEvents(const QVariantList &events);
    Q_INVOKABLE bool selectEvent(const QString &eventId);
    Q_INVOKABLE bool clearSelection();
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap eventAt(int row) const;
    Q_INVOKABLE QVariantMap eventById(const QString &eventId) const;
    Q_INVOKABLE int indexOfEventId(const QString &eventId) const;
    Q_INVOKABLE QString exportCsv(const QStringList &eventIds = {}) const;
signals:
    void changed();
    void filtersChanged();
    void limitsChanged();
    void selectionChanged();
    void eventsAboutToAppend();
    void eventsAppended(int count);
    void eventsRejected(int count);
private:
    struct Event {
        QString id;
        QDateTime timestamp;
        QString severity;
        QString source;
        QString message;
        QString details;
        int payloadBytes = 0;
    };
    bool matches(const Event &event) const;
    void rebuildVisible();
    void populateVisible();
    const Event *eventForRow(int row) const;
    const Event *eventForId(const QString &id) const;
    static QString csvField(const QString &value);
    static bool boundedText(const QVariant &value, int fieldLimit, int &byteCount, QString &result);
    void evictFront(int count);
    std::deque<Event> m_events;
    QVector<QString> m_visible;
    QHash<QString, int> m_visibleRows;
    QHash<QString, const Event *> m_byId;
    QString m_selectedId;
    QString m_severity;
    QString m_source;
    QString m_text;
    QDateTime m_fromUtc;
    QDateTime m_toUtc;
    quint64 m_nextId = 1;
    int m_maxEntries = 5000;
    quint64 m_retainedBytes = 0;
    int m_lastRejectedCount = 0;
};

} // namespace QindaTK
