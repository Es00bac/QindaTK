// SPDX-License-Identifier: LGPL-3.0-or-later
#include "event_log_model.h"

#include <QMetaType>
#include <QSet>
#include <utility>

namespace QindaTK {
namespace {
constexpr int MaxEventBytes = 16384;
constexpr int MaxIdBytes = 128;
constexpr int MaxSeverityBytes = 32;
constexpr int MaxSourceBytes = 1024;
constexpr int MaxFieldBytes = 8192;
constexpr quint64 MaxRetainedBytes = 64ULL * 1024 * 1024;
constexpr quint64 MaxStagingBytes = 16ULL * 1024 * 1024;
}

EventLogModel::EventLogModel(QObject *parent) : QAbstractListModel(parent) {}

int EventLogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

QVariant EventLogModel::data(const QModelIndex &index, int role) const
{
    const Event *event = eventForRow(index.row());
    if (!event || !index.isValid()) return {};
    switch (role) {
    case EventIdRole: return event->id;
    case TimestampRole: return event->timestamp.toString(Qt::ISODateWithMs);
    case SeverityRole: return event->severity;
    case SourceRole: return event->source;
    case MessageRole: return event->message;
    case DetailsRole: return event->details;
    default: return {};
    }
}

QHash<int, QByteArray> EventLogModel::roleNames() const
{
    return {{EventIdRole, "eventId"}, {TimestampRole, "timestamp"},
            {SeverityRole, "severity"}, {SourceRole, "source"},
            {MessageRole, "message"}, {DetailsRole, "details"}};
}

void EventLogModel::setMaxEntries(int maximum)
{
    maximum = qBound(1, maximum, 100000);
    if (m_maxEntries == maximum) return;
    m_maxEntries = maximum;
    const int excess = qMax(0, int(m_events.size()) - m_maxEntries);
    if (excess) {
        beginResetModel();
        evictFront(excess);
        populateVisible();
        endResetModel();
        if (!m_selectedId.isEmpty() && !eventForId(m_selectedId)) {
            m_selectedId.clear(); emit selectionChanged();
        }
        emit changed();
    }
    emit limitsChanged();
}

void EventLogModel::setSeverityFilter(const QString &filter)
{
    if (m_severity == filter) return;
    m_severity = filter.trimmed(); rebuildVisible(); emit filtersChanged();
}

void EventLogModel::setSourceFilter(const QString &filter)
{
    if (m_source == filter) return;
    m_source = filter.trimmed(); rebuildVisible(); emit filtersChanged();
}

void EventLogModel::setTextFilter(const QString &filter)
{
    if (m_text == filter) return;
    m_text = filter.trimmed(); rebuildVisible(); emit filtersChanged();
}

void EventLogModel::setFromUtc(const QDateTime &value)
{
    const QDateTime utc = value.isValid() ? value.toUTC() : QDateTime();
    if (m_fromUtc == utc) return;
    m_fromUtc = utc; rebuildVisible(); emit filtersChanged();
}

void EventLogModel::setToUtc(const QDateTime &value)
{
    const QDateTime utc = value.isValid() ? value.toUTC() : QDateTime();
    if (m_toUtc == utc) return;
    m_toUtc = utc; rebuildVisible(); emit filtersChanged();
}

int EventLogModel::appendEvent(const QVariantMap &event)
{
    return appendEvents({event});
}

int EventLogModel::appendEvents(const QVariantList &values)
{
    // Keep staging bounded by the retained window. Oversized rows are rejected
    // as whole records; callers can observe the returned accepted count.
    std::deque<Event> staged;
    QSet<QString> stagedIds;
    int rejected = 0;
    quint64 stagedBytes = 0;
    for (const QVariant &value : values) {
        const QVariantMap map = value.toMap();
        if (map.isEmpty()) { ++rejected; continue; }
        Event event;
        int bytes = 0;
        const QVariant idValue = map.value(QStringLiteral("eventId"));
        if (!boundedText(idValue, MaxIdBytes, bytes, event.id)) { ++rejected; continue; }
        const QVariant severityValue = map.value(QStringLiteral("severity"), QStringLiteral("info"));
        if (!boundedText(severityValue, MaxSeverityBytes, bytes, event.severity)) { ++rejected; continue; }
        if (!boundedText(map.value(QStringLiteral("source")), MaxSourceBytes, bytes, event.source)) { ++rejected; continue; }
        if (!boundedText(map.value(QStringLiteral("message")), MaxFieldBytes, bytes, event.message)) { ++rejected; continue; }
        const QVariant detailValue = map.value(QStringLiteral("details"));
        if (!detailValue.isValid() || detailValue.metaType().id() == QMetaType::QString) {
        if (!boundedText(detailValue, MaxFieldBytes, bytes, event.details)) { ++rejected; continue; }
        } else {
            ++rejected; continue; // structured details must be serialized explicitly by the producer
        }
        if (bytes > MaxEventBytes) { ++rejected; continue; }
        const QVariant time = map.value(QStringLiteral("timestamp"));
        if (!time.isValid() || time.isNull()) {
            event.timestamp = QDateTime::currentDateTimeUtc();
        } else if (time.metaType().id() == QMetaType::QDateTime) {
            event.timestamp = time.toDateTime().toUTC();
        } else {
            const QString timeText = time.toString();
            if (timeText.size() > 64) { ++rejected; continue; }
            event.timestamp = QDateTime::fromString(timeText, Qt::ISODateWithMs).toUTC();
            if (!event.timestamp.isValid())
                event.timestamp = QDateTime::fromString(timeText, Qt::ISODate).toUTC();
            if (!event.timestamp.isValid()) { ++rejected; continue; }
        }
        if (!event.timestamp.isValid()) { ++rejected; continue; }
        event.payloadBytes = bytes;
        if (event.id.isEmpty() || m_byId.contains(event.id) || stagedIds.contains(event.id)) {
            do { event.id = QStringLiteral("event-%1").arg(m_nextId++); }
            while (m_byId.contains(event.id) || stagedIds.contains(event.id));
        }
        stagedIds.insert(event.id);
        stagedBytes += quint64(event.payloadBytes);
        staged.push_back(std::move(event));
        while (staged.size() > size_t(m_maxEntries) || stagedBytes > MaxStagingBytes) {
            stagedIds.remove(staged.front().id);
            stagedBytes -= quint64(staged.front().payloadBytes);
            staged.pop_front();
        }
    }
    const bool rejectionCountChanged = m_lastRejectedCount != rejected;
    m_lastRejectedCount = rejected;
    if (staged.empty()) {
        if (rejectionCountChanged) emit changed();
        if (rejected) emit eventsRejected(rejected);
        return 0;
    }
    emit eventsAboutToAppend();
    int evict = 0;
    quint64 retainedAfterEviction = m_retainedBytes;
    while (size_t(int(m_events.size()) - evict) + staged.size() > size_t(m_maxEntries)
           || retainedAfterEviction + stagedBytes > MaxRetainedBytes) {
        if (evict >= int(m_events.size())) break;
        retainedAfterEviction -= quint64(m_events.at(size_t(evict)).payloadBytes);
        ++evict;
    }
    if (evict) {
        beginResetModel();
        evictFront(evict);
        for (Event &event : staged) { m_retainedBytes += quint64(event.payloadBytes); m_events.push_back(std::move(event)); }
        for (auto it = m_events.end() - std::ptrdiff_t(staged.size()); it != m_events.end(); ++it)
            m_byId.insert(it->id, &*it);
        populateVisible();
        endResetModel();
        if (!m_selectedId.isEmpty() && !eventForId(m_selectedId)) {
            m_selectedId.clear(); emit selectionChanged();
        }
    } else {
        QVector<QString> matchesNow;
        matchesNow.reserve(staged.size());
        for (const Event &event : std::as_const(staged))
            if (matches(event)) matchesNow.push_back(event.id);
        if (!matchesNow.isEmpty())
            beginInsertRows({}, m_visible.size(), m_visible.size() + matchesNow.size() - 1);
        for (Event &event : staged) { m_retainedBytes += quint64(event.payloadBytes); m_events.push_back(std::move(event)); }
        for (auto it = m_events.end() - std::ptrdiff_t(staged.size()); it != m_events.end(); ++it)
            m_byId.insert(it->id, &*it);
        for (const QString &id : std::as_const(matchesNow)) {
            m_visibleRows.insert(id, m_visible.size());
            m_visible.push_back(id);
        }
        if (!matchesNow.isEmpty()) endInsertRows();
    }
    emit changed();
    const int retainedFromBatch = int(staged.size());
    if (rejected) emit eventsRejected(rejected);
    emit eventsAppended(retainedFromBatch);
    return retainedFromBatch;
}

bool EventLogModel::selectEvent(const QString &eventId)
{
    if (!eventId.isEmpty() && !eventForId(eventId)) return false;
    if (m_selectedId == eventId) return true;
    m_selectedId = eventId; emit selectionChanged(); return true;
}

bool EventLogModel::clearSelection()
{
    if (m_selectedId.isEmpty()) return false;
    m_selectedId.clear(); emit selectionChanged(); return true;
}

void EventLogModel::clear()
{
    if (m_events.empty() && m_selectedId.isEmpty() && m_lastRejectedCount == 0) return;
    beginResetModel(); m_events.clear(); m_visible.clear(); m_visibleRows.clear(); m_byId.clear();
    m_retainedBytes = 0; m_lastRejectedCount = 0; endResetModel();
    m_selectedId.clear(); emit selectionChanged(); emit changed();
}

QVariantMap EventLogModel::eventAt(int row) const
{
    const Event *event = eventForRow(row);
    if (!event) return {};
    return {{QStringLiteral("eventId"), event->id},
            {QStringLiteral("timestamp"), event->timestamp},
            {QStringLiteral("severity"), event->severity},
            {QStringLiteral("source"), event->source},
            {QStringLiteral("message"), event->message},
            {QStringLiteral("details"), event->details}};
}

QVariantMap EventLogModel::eventById(const QString &eventId) const
{
    const Event *event = eventForId(eventId);
    if (!event) return {};
    return {{QStringLiteral("eventId"), event->id},
            {QStringLiteral("timestamp"), event->timestamp},
            {QStringLiteral("severity"), event->severity},
            {QStringLiteral("source"), event->source},
            {QStringLiteral("message"), event->message},
            {QStringLiteral("details"), event->details}};
}

int EventLogModel::indexOfEventId(const QString &eventId) const
{
    return m_visibleRows.value(eventId, -1);
}

QString EventLogModel::exportCsv(const QStringList &eventIds) const
{
    QString output = QStringLiteral("eventId,timestamp,severity,source,message,details\r\n");
    const QSet<QString> requested(eventIds.cbegin(), eventIds.cend());
    for (const Event &event : m_events) {
        if (eventIds.isEmpty() ? !matches(event) : !requested.contains(event.id)) continue;
        output += csvField(event.id) + QLatin1Char(',')
            + csvField(event.timestamp.toString(Qt::ISODateWithMs)) + QLatin1Char(',')
            + csvField(event.severity) + QLatin1Char(',') + csvField(event.source) + QLatin1Char(',')
            + csvField(event.message) + QLatin1Char(',') + csvField(event.details) + QStringLiteral("\r\n");
    }
    return output;
}

bool EventLogModel::matches(const Event &event) const
{
    if (!m_severity.isEmpty() && m_severity.compare(QStringLiteral("all"), Qt::CaseInsensitive)
            && event.severity.compare(m_severity, Qt::CaseInsensitive)) return false;
    if (!m_source.isEmpty() && !event.source.contains(m_source, Qt::CaseInsensitive)) return false;
    if (!m_text.isEmpty() && !event.message.contains(m_text, Qt::CaseInsensitive)
            && !event.details.contains(m_text, Qt::CaseInsensitive)) return false;
    if (m_fromUtc.isValid() && event.timestamp < m_fromUtc) return false;
    if (m_toUtc.isValid() && event.timestamp > m_toUtc) return false;
    return true;
}

void EventLogModel::rebuildVisible()
{
    beginResetModel(); populateVisible(); endResetModel(); emit changed();
}

void EventLogModel::populateVisible()
{
    m_visible.clear(); m_visibleRows.clear(); m_visible.reserve(int(m_events.size()));
    for (const Event &event : m_events) {
        if (!matches(event)) continue;
        m_visibleRows.insert(event.id, m_visible.size());
        m_visible.push_back(event.id);
    }
}

void EventLogModel::evictFront(int count)
{
    while (count-- > 0 && !m_events.empty()) {
        m_retainedBytes -= quint64(m_events.front().payloadBytes);
        m_byId.remove(m_events.front().id);
        m_events.pop_front();
    }
}

const EventLogModel::Event *EventLogModel::eventForRow(int row) const
{
    if (row < 0 || row >= m_visible.size()) return nullptr;
    return m_byId.value(m_visible.at(row), nullptr);
}

const EventLogModel::Event *EventLogModel::eventForId(const QString &id) const
{
    return m_byId.value(id, nullptr);
}

bool EventLogModel::boundedText(const QVariant &value, int fieldLimit,
                                int &byteCount, QString &result)
{
    if (!value.isValid() || value.isNull()) { result.clear(); return true; }
    if (value.metaType().id() != QMetaType::QString) return false;
    result = value.toString();
    if (result.size() > fieldLimit) return false; // bound UTF-8 allocation before conversion
    const int size = result.toUtf8().size();
    if (size > fieldLimit) return false;
    byteCount += size;
    return byteCount <= MaxEventBytes;
}

QString EventLogModel::csvField(const QString &value)
{
    QString escaped = value; escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (value.contains(QLatin1Char(',')) || value.contains(QLatin1Char('"'))
            || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r')))
        return QLatin1Char('"') + escaped + QLatin1Char('"');
    return value;
}

} // namespace QindaTK
