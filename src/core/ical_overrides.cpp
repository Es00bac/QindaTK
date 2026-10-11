// SPDX-License-Identifier: LGPL-3.0-or-later
// iCalendar stamps, component blocks and overridden recurrence instances
// (RFC 5545 RECURRENCE-ID) for ical_codec.cpp.
#include "ical_codec_p.h"
#include "recurrence_service.h"

#include <QMap>
#include <QRegularExpression>
#include <QSet>

namespace QindaTK::ICal::detail {
namespace {
struct OverrideBlock {
    int begin = -1, end = -1;
    QString local;                              // canonical local start in the master zone
    bool cancelled = false;
};

QString canonicalIn(const Stamp &stamp, const QString &zone)
{
    if (stamp.allDay) return stamp.date.toString(Qt::ISODate);
    return stamp.zoned().toTimeZone(QTimeZone(zone.toUtf8())).toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
}

// Every VEVENT of `uid` that carries a RECURRENCE-ID, keyed in `zone`.
QList<OverrideBlock> overrideBlocks(const QList<Property> &lines, const QString &uid, const QString &zone)
{
    QList<OverrideBlock> blocks;
    int depth = 0;
    OverrideBlock current;
    QString currentUid;
    bool hasId = false;
    for (int i = 0; i < lines.size(); ++i) {
        const Property &line = lines.at(i);
        if (line.name == QLatin1String("BEGIN")) {
            ++depth;
            if (depth == 2 && line.value.toUpper() == QLatin1String("VEVENT")) {
                current = {};
                current.begin = i;
                currentUid.clear();
                hasId = false;
            }
        } else if (line.name == QLatin1String("END")) {
            if (depth == 2 && current.begin >= 0 && line.value.toUpper() == QLatin1String("VEVENT")) {
                current.end = i;
                if (hasId && currentUid == uid && !current.local.isEmpty()) blocks.append(current);
                current = {};
            }
            --depth;
        } else if (depth == 2 && current.begin >= 0) {
            if (line.name == QLatin1String("UID")) currentUid = unescapeText(line.value);
            else if (line.name == QLatin1String("STATUS")) current.cancelled = line.value.trimmed().toUpper() == QLatin1String("CANCELLED");
            else if (line.name == QLatin1String("RECURRENCE-ID")) {
                hasId = true;
                const Stamp stamp = parseStampValue(line.value, line, zone);
                if (stamp.ok) current.local = canonicalIn(stamp, zone);
            }
        }
    }
    return blocks;
}

// DTSTART/DTEND of one override as stored local text in the master zone.
QVariantMap overrideFields(const QList<Property> &props, const QString &local, const QString &zone,
                           bool allDay, qint64 span)
{
    QVariantMap entry{{QStringLiteral("recurrenceId"), local}};
    const QTimeZone tz(zone.toUtf8());
    QDateTime start, end;
    bool startAllDay = allDay;
    for (const Property &p : props) {
        if (p.name == QLatin1String("DTSTART")) {
            const Stamp stamp = parseStampValue(p.value, p, zone);
            if (!stamp.ok) continue;
            startAllDay = stamp.allDay;
            start = stamp.allDay ? QDateTime(stamp.date, QTime(0, 0), tz) : stamp.zoned().toTimeZone(tz);
        } else if (p.name == QLatin1String("DTEND")) {
            const Stamp stamp = parseStampValue(p.value, p, zone);
            if (stamp.ok) end = stamp.allDay ? QDateTime(stamp.date, QTime(0, 0), tz) : stamp.zoned().toTimeZone(tz);
        } else if (p.name == QLatin1String("DURATION") && start.isValid() && !end.isValid()) {
            const qint64 seconds = durationSeconds(p.value);
            if (seconds >= 0) end = start.addSecs(seconds);
        } else if (p.name == QLatin1String("SUMMARY")) {
            entry.insert(QStringLiteral("title"), unescapeText(p.value));
        } else if (p.name == QLatin1String("DESCRIPTION")) {
            entry.insert(QStringLiteral("description"), unescapeText(p.value));
        } else if (p.name == QLatin1String("LOCATION")) {
            entry.insert(QStringLiteral("location"), unescapeText(p.value));
        }
    }
    if (!start.isValid()) return entry;
    if (!end.isValid()) end = startAllDay ? start.addDays(qMax<qint64>(1, span / 86400)) : start.addSecs(span);
    entry.insert(QStringLiteral("startLocal"), localText(start.date(), start.time(), startAllDay));
    entry.insert(QStringLiteral("endLocal"), localText(end.date(), end.time(), startAllDay));
    return entry;
}

const QStringList &overrideManaged()
{
    static const QStringList names{QStringLiteral("UID"), QStringLiteral("DTSTAMP"), QStringLiteral("RECURRENCE-ID"),
        QStringLiteral("SUMMARY"), QStringLiteral("DTSTART"), QStringLiteral("DTEND"), QStringLiteral("DURATION"),
        QStringLiteral("DESCRIPTION"), QStringLiteral("LOCATION")};
    return names;
}

QStringList overrideLines(const QString &uid, const QVariantMap &record, const QVariantMap &entry)
{
    const QString zone = record.value(QStringLiteral("zone")).toString().isEmpty()
        ? QStringLiteral("UTC") : record.value(QStringLiteral("zone")).toString();
    const QString masterStart = record.value(QStringLiteral("startLocal")).toString();
    const bool allDay = record.value(QStringLiteral("allDay")).toBool() || masterStart.trimmed().size() == 10;
    const qint64 span = RecurrenceService::spanSeconds(masterStart, record.value(QStringLiteral("endLocal")).toString(), zone);
    const QString local = RecurrenceService::canonicalLocal(entry.value(QStringLiteral("recurrenceId")).toString());
    QDate date;
    QTime time;
    if (!parseLocal(local, &date, &time)) return {};
    auto text = [&](const char *name) {
        const QString field = QString::fromLatin1(name);
        return entry.contains(field) ? entry.value(field).toString() : record.value(field).toString();
    };
    QStringList out{QStringLiteral("UID:") + escapeText(uid),
                    QStringLiteral("DTSTAMP:") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")),
                    stampLine(QStringLiteral("RECURRENCE-ID"), date, time, zone, allDay),
                    QStringLiteral("SUMMARY:") + escapeText(text("title"))};
    if (!text("description").isEmpty()) out.append(QStringLiteral("DESCRIPTION:") + escapeText(text("description")));
    if (!text("location").isEmpty()) out.append(QStringLiteral("LOCATION:") + escapeText(text("location")));
    QDate startDate = date, endDate;
    QTime startTime = time, endTime;
    if (entry.contains(QStringLiteral("startLocal"))) parseLocal(entry.value(QStringLiteral("startLocal")).toString(), &startDate, &startTime);
    if (!entry.contains(QStringLiteral("endLocal")) || !parseLocal(entry.value(QStringLiteral("endLocal")).toString(), &endDate, &endTime)) {
        const QDateTime end = allDay ? QDateTime(startDate.addDays(qMax<qint64>(1, span / 86400)), QTime(0, 0))
            : QDateTime(startDate, startTime, QTimeZone(zone.toUtf8()), QDateTime::TransitionResolution::PreferBefore)
                  .addSecs(qMax<qint64>(0, span)).toTimeZone(QTimeZone(zone.toUtf8()));
        endDate = end.date();
        endTime = end.time();
    }
    out.append(stampLine(QStringLiteral("DTSTART"), startDate, startTime, zone, allDay));
    out.append(stampLine(QStringLiteral("DTEND"), endDate, endTime, zone, allDay));
    return out;
}

void appendBlock(QByteArray *out, const QStringList &lines)
{
    *out += foldLine(QStringLiteral("BEGIN:VEVENT"));
    for (const QString &line : lines) *out += foldLine(line);
    *out += foldLine(QStringLiteral("END:VEVENT"));
}
} // namespace

const QStringList &managed(const QString &component)
{
    static const QStringList event{QStringLiteral("UID"), QStringLiteral("DTSTAMP"), QStringLiteral("SUMMARY"),
        QStringLiteral("DTSTART"), QStringLiteral("DTEND"), QStringLiteral("DURATION"), QStringLiteral("RRULE"),
        QStringLiteral("EXDATE"), QStringLiteral("DESCRIPTION"), QStringLiteral("LOCATION"), QStringLiteral("CATEGORIES")};
    static const QStringList task{QStringLiteral("UID"), QStringLiteral("DTSTAMP"), QStringLiteral("SUMMARY"),
        QStringLiteral("STATUS"), QStringLiteral("X-QINDATK-STATUS"), QStringLiteral("X-QINDATK-ORDER"),
        QStringLiteral("DUE"), QStringLiteral("PRIORITY"), QStringLiteral("X-QINDATK-ASSIGNEE"),
        QStringLiteral("DESCRIPTION"), QStringLiteral("CATEGORIES")};
    return component == QLatin1String("VTODO") ? task : event;
}

QString resolveZone(const QString &tzid)
{
    if (QTimeZone(tzid.toUtf8()).isValid()) return tzid;
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(tzid.toUtf8());
    if (!iana.isEmpty()) return QString::fromUtf8(iana);
    // Vendor-prefixed IDs such as "/mozilla.org/20050126_1/America/New_York"
    // or "/citadel.org/20250101_1/Europe/Berlin": the longest IANA suffix.
    const QStringList parts = tzid.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QString candidate = parts.mid(i).join(QLatin1Char('/'));
        if (QTimeZone(candidate.toUtf8()).isValid()) return candidate;
    }
    return {};
}

Stamp parseStampValue(QString value, const Property &property, const QString &defaultZone)
{
    Stamp stamp;
    value = value.trimmed();
    stamp.zone = defaultZone;
    if (property.param(QStringLiteral("VALUE")) == QLatin1String("DATE") || value.size() == 8) {
        stamp.allDay = true;
        stamp.date = QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd"));
        stamp.ok = stamp.date.isValid();
        return stamp;
    }
    const bool utc = value.endsWith(QLatin1Char('Z'));
    if (utc) value.chop(1);
    if (value.size() != 15 || value.at(8) != QLatin1Char('T')) return stamp;
    stamp.date = QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd"));
    stamp.time = QTime::fromString(value.mid(9), QStringLiteral("HHmmss"));
    if (utc) stamp.zone = QStringLiteral("UTC");
    else if (!property.param(QStringLiteral("TZID")).isEmpty()) stamp.zone = resolveZone(property.param(QStringLiteral("TZID")));
    stamp.ok = stamp.date.isValid() && stamp.time.isValid() && !stamp.zone.isEmpty();
    return stamp;
}

QString localText(const QDate &date, const QTime &time, bool allDay)
{
    if (allDay) return date.toString(Qt::ISODate);
    return date.toString(Qt::ISODate) + QLatin1Char(' ')
        + time.toString(time.second() ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
}

bool parseLocal(const QString &text, QDate *date, QTime *time)
{
    *date = QDate::fromString(text.left(10), Qt::ISODate);
    *time = QTime(0, 0);
    if (text.size() > 10) {
        const QString clock = text.mid(11);
        *time = QTime::fromString(clock.left(8), clock.size() >= 8 ? QStringLiteral("HH:mm:ss") : QStringLiteral("HH:mm"));
    }
    return date->isValid() && time->isValid();
}

qint64 durationSeconds(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^P(?:(\\d+)W)?(?:(\\d+)D)?(?:T(?:(\\d+)H)?(?:(\\d+)M)?(?:(\\d+)S)?)?$"));
    const QRegularExpressionMatch m = pattern.match(value.trimmed());
    if (!m.hasMatch()) return -1;
    return m.captured(1).toLongLong() * 604800 + m.captured(2).toLongLong() * 86400
        + m.captured(3).toLongLong() * 3600 + m.captured(4).toLongLong() * 60 + m.captured(5).toLongLong();
}

QString stampLine(const QString &name, const QDate &date, const QTime &time, const QString &zone, bool allDay)
{
    if (allDay) return name + QStringLiteral(";VALUE=DATE:") + date.toString(QStringLiteral("yyyyMMdd"));
    const QString clock = date.toString(QStringLiteral("yyyyMMdd")) + QLatin1Char('T') + time.toString(QStringLiteral("HHmmss"));
    if (zone.isEmpty() || zone == QLatin1String("UTC") || zone == QLatin1String("Etc/UTC"))
        return name + QLatin1Char(':') + clock + QLatin1Char('Z');
    return name + QStringLiteral(";TZID=") + zone + QLatin1Char(':') + clock;
}

Block findMaster(const QList<Property> &lines, const QString &component)
{
    Block master;
    int depth = 0, begin = -1;
    bool recurrenceId = false;
    for (int i = 0; i < lines.size(); ++i) {
        const Property &line = lines.at(i);
        if (line.name == QLatin1String("BEGIN")) {
            ++depth;
            if (depth == 2 && line.value.toUpper() == component) { begin = i; recurrenceId = false; }
        } else if (line.name == QLatin1String("END")) {
            if (depth == 2 && begin >= 0 && line.value.toUpper() == component) {
                if (recurrenceId) ++master.overrides;
                else if (master.begin < 0) { master.begin = begin; master.end = i; }
                begin = -1;
            }
            --depth;
        } else if (depth == 2 && begin >= 0 && line.name == QLatin1String("RECURRENCE-ID")) {
            recurrenceId = true;
        }
    }
    return master;
}

void readOverrides(const QList<Property> &lines, const QString &uid, QVariantMap *fields)
{
    const QString zone = fields->value(QStringLiteral("zone")).toString();
    const bool allDay = fields->value(QStringLiteral("allDay")).toBool();
    const qint64 span = RecurrenceService::spanSeconds(fields->value(QStringLiteral("startLocal")).toString(),
                                                       fields->value(QStringLiteral("endLocal")).toString(), zone);
    QVariantList overrides;
    QVariantList exceptions = fields->value(QStringLiteral("exceptions")).toList();
    for (const OverrideBlock &block : overrideBlocks(lines, uid, zone)) {
        if (block.cancelled) {
            if (!exceptions.contains(block.local)) exceptions.append(block.local);
            continue;
        }
        QList<Property> props;
        int depth = 0;
        for (int i = block.begin + 1; i < block.end; ++i) {
            if (lines.at(i).name == QLatin1String("BEGIN")) ++depth;
            else if (lines.at(i).name == QLatin1String("END")) --depth;
            else if (depth == 0) props.append(lines.at(i));
        }
        overrides.append(overrideFields(props, block.local, zone, allDay, qMax<qint64>(0, span)));
    }
    fields->insert(QStringLiteral("exceptions"), exceptions);
    fields->insert(QStringLiteral("overrides"), overrides);
}

QByteArray writeCalendar(const QString &component, const QString &uid, const QVariantMap &record,
                         const QByteArray &base, const QStringList &masterLines)
{
    const bool events = component == QLatin1String("VEVENT");
    const QString zone = record.value(QStringLiteral("zone")).toString().isEmpty()
        ? QStringLiteral("UTC") : record.value(QStringLiteral("zone")).toString();
    QMap<QString, QVariantMap> wanted;
    if (events) {
        for (const QVariant &value : record.value(QStringLiteral("overrides")).toList()) {
            const QString local = RecurrenceService::canonicalLocal(value.toMap().value(QStringLiteral("recurrenceId")).toString());
            if (!local.isEmpty()) wanted.insert(local, value.toMap());
        }
    }
    QSet<QString> excluded;
    for (const QVariant &value : record.value(QStringLiteral("exceptions")).toList())
        excluded.insert(RecurrenceService::canonicalLocal(value.toString().section(QLatin1Char('/'), -1)));
    const QList<Property> lines = parseLines(base);
    const Block master = findMaster(lines, component);
    QByteArray out;
    if (master.begin < 0) {
        out = foldLine(QStringLiteral("BEGIN:VCALENDAR")) + foldLine(QStringLiteral("VERSION:2.0"))
            + foldLine(QStringLiteral("PRODID:-//QindaTK//Productivity//EN")) + foldLine(QStringLiteral("BEGIN:") + component);
        for (const QString &line : masterLines) out += foldLine(line);
        out += foldLine(QStringLiteral("END:") + component);
        for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) appendBlock(&out, overrideLines(uid, record, it.value()));
        return out + foldLine(QStringLiteral("END:VCALENDAR"));
    }
    const QList<OverrideBlock> blocks = events ? overrideBlocks(lines, uid, zone) : QList<OverrideBlock>();
    QSet<QString> written;
    int depth = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const Property &line = lines.at(i);
        const OverrideBlock *block = nullptr;
        for (const OverrideBlock &candidate : blocks)
            if (i >= candidate.begin && i <= candidate.end) block = &candidate;
        if (line.name == QLatin1String("BEGIN")) ++depth;
        if (block) {
            // AGENT-GUARD: an override the record no longer lists was restored
            // or excluded locally; dropping its component is what sends that
            // change. A cancelled instance survives only while it is excluded.
            const bool keep = block->cancelled ? excluded.contains(block->local) : wanted.contains(block->local);
            if (keep) {
                const bool nested = depth > 2 || (depth == 2 && line.name == QLatin1String("END") && i != block->end);
                if (block->cancelled || nested || i == block->begin || i == block->end
                    || !overrideManaged().contains(line.name))
                    out += foldLine(line.raw);
                if (i == block->begin && !block->cancelled) {
                    for (const QString &generated : overrideLines(uid, record, wanted.value(block->local))) out += foldLine(generated);
                    written.insert(block->local);
                }
            }
        } else {
            const bool inside = i > master.begin && i < master.end;
            const bool managedLine = inside && depth == 2 && line.name != QLatin1String("BEGIN")
                && line.name != QLatin1String("END") && managed(component).contains(line.name);
            if (line.name == QLatin1String("END") && depth == 1) {
                for (auto it = wanted.cbegin(); it != wanted.cend(); ++it)
                    if (!written.contains(it.key())) appendBlock(&out, overrideLines(uid, record, it.value()));
            }
            if (!managedLine) out += foldLine(line.raw);
            if (i == master.begin) for (const QString &generated : masterLines) out += foldLine(generated);
        }
        if (line.name == QLatin1String("END")) --depth;
    }
    return out;
}

} // namespace QindaTK::ICal::detail
