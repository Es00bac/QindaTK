// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ical_codec.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

namespace QindaTK::ICal {
namespace {
const QStringList &eventFields()
{
    static const QStringList fields{QStringLiteral("title"), QStringLiteral("startLocal"), QStringLiteral("endLocal"),
        QStringLiteral("zone"), QStringLiteral("allDay"), QStringLiteral("recurrenceRule"),
        QStringLiteral("recurrenceRaw"), QStringLiteral("exceptions"), QStringLiteral("description"),
        QStringLiteral("location")};
    return fields;
}
const QStringList &taskFields()
{
    static const QStringList fields{QStringLiteral("title"), QStringLiteral("status"), QStringLiteral("order"),
        QStringLiteral("dueDate"), QStringLiteral("priority"), QStringLiteral("assignee"), QStringLiteral("description")};
    return fields;
}
const QStringList &managed(const QString &component)
{
    static const QStringList event{QStringLiteral("UID"), QStringLiteral("DTSTAMP"), QStringLiteral("SUMMARY"),
        QStringLiteral("DTSTART"), QStringLiteral("DTEND"), QStringLiteral("DURATION"), QStringLiteral("RRULE"),
        QStringLiteral("EXDATE"), QStringLiteral("DESCRIPTION"), QStringLiteral("LOCATION")};
    static const QStringList task{QStringLiteral("UID"), QStringLiteral("DTSTAMP"), QStringLiteral("SUMMARY"),
        QStringLiteral("STATUS"), QStringLiteral("X-QINDATK-STATUS"), QStringLiteral("X-QINDATK-ORDER"),
        QStringLiteral("DUE"), QStringLiteral("PRIORITY"), QStringLiteral("X-QINDATK-ASSIGNEE"),
        QStringLiteral("DESCRIPTION")};
    return component == QLatin1String("VTODO") ? task : event;
}
const QStringList &dayCodes()
{
    static const QStringList codes{QStringLiteral("MO"), QStringLiteral("TU"), QStringLiteral("WE"),
        QStringLiteral("TH"), QStringLiteral("FR"), QStringLiteral("SA"), QStringLiteral("SU")};
    return codes;
}

QString resolveZone(const QString &tzid)
{
    if (QTimeZone(tzid.toUtf8()).isValid()) return tzid;
    const QByteArray iana = QTimeZone::windowsIdToDefaultIanaId(tzid.toUtf8());
    return iana.isEmpty() ? QString() : QString::fromUtf8(iana);
}

struct Stamp {
    bool ok = false, allDay = false;
    QDate date;
    QTime time;
    QString zone;
    QDateTime zoned() const
    {
        return QDateTime(date, allDay ? QTime(0, 0) : time, QTimeZone(zone.toUtf8()),
                         QDateTime::TransitionResolution::PreferBefore);
    }
};

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

// RRULE → RecurrenceService rule, or an invalid QVariant when unsupported.
QVariant parseRule(const QString &rrule, const Stamp &start)
{
    QVariantMap rule{{QStringLiteral("interval"), 1}, {QStringLiteral("endMode"), QStringLiteral("never")}};
    for (const QString &part : rrule.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString key = part.section(QLatin1Char('='), 0, 0).toUpper();
        const QString value = part.section(QLatin1Char('='), 1).toUpper();
        if (key == QLatin1String("FREQ")) {
            static const QStringList supported{QStringLiteral("DAILY"), QStringLiteral("WEEKLY"),
                                               QStringLiteral("MONTHLY"), QStringLiteral("YEARLY")};
            if (!supported.contains(value)) return {};
            rule.insert(QStringLiteral("frequency"), value.toLower());
        } else if (key == QLatin1String("INTERVAL")) {
            if (value.toInt() < 1) return {};
            rule.insert(QStringLiteral("interval"), value.toInt());
        } else if (key == QLatin1String("COUNT")) {
            if (value.toInt() < 1) return {};
            rule.insert(QStringLiteral("endMode"), QStringLiteral("count"));
            rule.insert(QStringLiteral("count"), value.toInt());
        } else if (key == QLatin1String("UNTIL")) {
            Property bare;
            const Stamp until = parseStampValue(value, bare, start.zone);
            if (!until.ok) return {};
            const QDate day = until.allDay ? until.date : until.zoned().toTimeZone(QTimeZone(start.zone.toUtf8())).date();
            rule.insert(QStringLiteral("endMode"), QStringLiteral("until"));
            rule.insert(QStringLiteral("until"), day.toString(Qt::ISODate));
        } else if (key == QLatin1String("BYDAY")) {
            QVariantList weekdays;
            for (const QString &code : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                const int day = dayCodes().indexOf(code);
                if (day < 0) return {};              // ordinal weekdays such as 1MO
                weekdays.append(day + 1);
            }
            rule.insert(QStringLiteral("weekdays"), weekdays);
        } else if (key != QLatin1String("WKST")) {
            return {};                               // BYMONTHDAY, BYSETPOS, ...
        }
    }
    const QString frequency = rule.value(QStringLiteral("frequency")).toString();
    if (frequency.isEmpty() || (rule.contains(QStringLiteral("weekdays")) && frequency != QLatin1String("weekly")))
        return {};
    return rule;
}

QString ruleText(const QVariantMap &rule, const QString &zone, bool allDay)
{
    QString text = QStringLiteral("FREQ=") + rule.value(QStringLiteral("frequency")).toString().toUpper();
    const int interval = rule.value(QStringLiteral("interval"), 1).toInt();
    if (interval > 1) text += QStringLiteral(";INTERVAL=%1").arg(interval);
    QStringList days;
    for (const QVariant &day : rule.value(QStringLiteral("weekdays")).toList())
        if (day.toInt() >= 1 && day.toInt() <= 7) days.append(dayCodes().at(day.toInt() - 1));
    if (!days.isEmpty()) text += QStringLiteral(";BYDAY=") + days.join(QLatin1Char(','));
    const QString endMode = rule.value(QStringLiteral("endMode")).toString();
    if (endMode == QLatin1String("count")) {
        text += QStringLiteral(";COUNT=%1").arg(rule.value(QStringLiteral("count")).toInt());
    } else if (endMode == QLatin1String("until")) {
        const QDate until = QDate::fromString(rule.value(QStringLiteral("until")).toString(), Qt::ISODate);
        if (allDay) {
            text += QStringLiteral(";UNTIL=") + until.toString(QStringLiteral("yyyyMMdd"));
        } else {
            // RFC 5545: UNTIL is UTC when DTSTART has a zone; the last local day ends 23:59:59.
            const QDateTime last(until, QTime(23, 59, 59), QTimeZone(zone.toUtf8()),
                                 QDateTime::TransitionResolution::PreferBefore);
            text += QStringLiteral(";UNTIL=") + last.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
        }
    }
    return text;
}

QString stampLine(const QString &name, const QDate &date, const QTime &time, const QString &zone, bool allDay)
{
    if (allDay) return name + QStringLiteral(";VALUE=DATE:") + date.toString(QStringLiteral("yyyyMMdd"));
    const QString clock = date.toString(QStringLiteral("yyyyMMdd")) + QLatin1Char('T') + time.toString(QStringLiteral("HHmmss"));
    if (zone.isEmpty() || zone == QLatin1String("UTC") || zone == QLatin1String("Etc/UTC"))
        return name + QLatin1Char(':') + clock + QLatin1Char('Z');
    return name + QStringLiteral(";TZID=") + zone + QLatin1Char(':') + clock;
}

struct Block {
    int begin = -1, end = -1, overrides = 0;
};

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

QStringList recordLines(const QString &component, const QString &uid, const QVariantMap &record)
{
    auto text = [&record](const char *key) { return record.value(QString::fromLatin1(key)).toString(); };
    QStringList out{QStringLiteral("UID:") + escapeText(uid),
                    QStringLiteral("DTSTAMP:") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'")),
                    QStringLiteral("SUMMARY:") + escapeText(text("title"))};
    if (!text("description").isEmpty()) out.append(QStringLiteral("DESCRIPTION:") + escapeText(text("description")));
    if (component == QLatin1String("VTODO")) {
        static const QMap<QString, QString> standard{{QStringLiteral("todo"), QStringLiteral("NEEDS-ACTION")},
            {QStringLiteral("doing"), QStringLiteral("IN-PROCESS")}, {QStringLiteral("done"), QStringLiteral("COMPLETED")},
            {QStringLiteral("cancelled"), QStringLiteral("CANCELLED")}};
        const QString status = text("status");
        out.append(QStringLiteral("STATUS:") + standard.value(status, QStringLiteral("NEEDS-ACTION")));
        if (!standard.contains(status) && !status.isEmpty()) out.append(QStringLiteral("X-QINDATK-STATUS:") + escapeText(status));
        out.append(QStringLiteral("X-QINDATK-ORDER:%1").arg(record.value(QStringLiteral("order")).toInt()));
        const QDate due = QDate::fromString(text("dueDate").left(10), Qt::ISODate);
        if (due.isValid()) out.append(QStringLiteral("DUE;VALUE=DATE:") + due.toString(QStringLiteral("yyyyMMdd")));
        const QString priority = text("priority").toLower();
        if (priority == QLatin1String("high")) out.append(QStringLiteral("PRIORITY:1"));
        else if (priority == QLatin1String("normal")) out.append(QStringLiteral("PRIORITY:5"));
        else if (priority == QLatin1String("low")) out.append(QStringLiteral("PRIORITY:9"));
        if (!text("assignee").isEmpty()) out.append(QStringLiteral("X-QINDATK-ASSIGNEE:") + escapeText(text("assignee")));
        return out;
    }
    const bool allDay = record.value(QStringLiteral("allDay")).toBool();
    const QString zone = text("zone").isEmpty() ? QStringLiteral("UTC") : text("zone");
    QDate date;
    QTime time;
    if (parseLocal(text("startLocal"), &date, &time)) out.append(stampLine(QStringLiteral("DTSTART"), date, time, zone, allDay));
    if (parseLocal(text("endLocal"), &date, &time)) out.append(stampLine(QStringLiteral("DTEND"), date, time, zone, allDay));
    if (!text("location").isEmpty()) out.append(QStringLiteral("LOCATION:") + escapeText(text("location")));
    const QVariantMap rule = record.value(QStringLiteral("recurrenceRule")).toMap();
    if (!rule.value(QStringLiteral("frequency")).toString().isEmpty())
        out.append(QStringLiteral("RRULE:") + ruleText(rule, zone, allDay));
    else if (!text("recurrenceRaw").isEmpty())
        out.append(QStringLiteral("RRULE:") + text("recurrenceRaw"));
    static const QRegularExpression wall(QStringLiteral("(\\d{4}-\\d{2}-\\d{2}[T ]\\d{2}:\\d{2}(:\\d{2})?)$"));
    for (const QVariant &value : record.value(QStringLiteral("exceptions")).toList()) {
        const QRegularExpressionMatch match = wall.match(value.toString());
        if (match.hasMatch() && parseLocal(match.captured(1), &date, &time))
            out.append(stampLine(QStringLiteral("EXDATE"), date, time, zone, allDay));
    }
    return out;
}
} // namespace

QString Property::param(const QString &key) const
{
    for (const auto &[name, value] : params)
        if (name == key) return value;
    return {};
}

QList<Property> parseLines(const QByteArray &text)
{
    QList<QByteArray> unfolded;
    for (QByteArray line : text.split('\n')) {
        if (line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty() && (line.at(0) == ' ' || line.at(0) == '\t') && !unfolded.isEmpty())
            unfolded.last() += line.mid(1);
        else if (!line.isEmpty())
            unfolded.append(line);
    }
    QList<Property> out;
    for (const QByteArray &bytes : std::as_const(unfolded)) {
        const QString line = QString::fromUtf8(bytes);
        Property property;
        property.raw = line;
        qsizetype pos = 0;
        while (pos < line.size() && line.at(pos) != QLatin1Char(';') && line.at(pos) != QLatin1Char(':')) ++pos;
        property.name = line.left(pos).trimmed().toUpper();
        while (pos < line.size() && line.at(pos) == QLatin1Char(';')) {
            const qsizetype eq = line.indexOf(QLatin1Char('='), pos);
            if (eq < 0) break;
            const QString key = line.mid(pos + 1, eq - pos - 1).toUpper();
            pos = eq + 1;
            QString value;
            bool quoted = false;
            while (pos < line.size()) {
                const QChar c = line.at(pos);
                if (c == QLatin1Char('"')) { quoted = !quoted; ++pos; continue; }
                if (!quoted && (c == QLatin1Char(';') || c == QLatin1Char(':'))) break;
                value += c;
                ++pos;
            }
            property.params.append({key, value});
        }
        property.value = pos < line.size() ? line.mid(pos + 1) : QString();
        if (!property.name.isEmpty()) out.append(property);
    }
    return out;
}

QByteArray foldLine(const QString &line)
{
    const QByteArray bytes = line.toUtf8();
    QByteArray out;
    qsizetype pos = 0;
    bool first = true;
    while (pos < bytes.size()) {
        qsizetype take = std::min<qsizetype>(first ? 75 : 74, bytes.size() - pos);
        // Never split a UTF-8 sequence: back off over continuation bytes.
        while (take > 1 && pos + take < bytes.size() && (static_cast<unsigned char>(bytes.at(pos + take)) & 0xC0) == 0x80)
            --take;
        out += (first ? QByteArray() : QByteArrayLiteral(" ")) + bytes.mid(pos, take) + "\r\n";
        pos += take;
        first = false;
    }
    return out.isEmpty() ? QByteArrayLiteral("\r\n") : out;
}

QString escapeText(const QString &text)
{
    QString out;
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    for (const QChar c : std::as_const(normalized)) {
        if (c == QLatin1Char('\\')) out += QLatin1String("\\\\");
        else if (c == QLatin1Char(';')) out += QLatin1String("\\;");
        else if (c == QLatin1Char(',')) out += QLatin1String("\\,");
        else if (c == QLatin1Char('\n')) out += QLatin1String("\\n");
        else if (c != QLatin1Char('\r')) out += c;
    }
    return out;
}

QString unescapeText(const QString &text)
{
    QString out;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c != QLatin1Char('\\') || i + 1 >= text.size()) { out += c; continue; }
        const QChar next = text.at(++i);
        out += (next == QLatin1Char('n') || next == QLatin1Char('N')) ? QChar(QLatin1Char('\n')) : next;
    }
    return out;
}

Mapped toRecord(const QByteArray &ics, const QString &defaultZone)
{
    Mapped mapped;
    const QList<Property> lines = parseLines(ics);
    Block block = findMaster(lines, QStringLiteral("VEVENT"));
    mapped.component = QStringLiteral("VEVENT");
    if (block.begin < 0) {
        block = findMaster(lines, QStringLiteral("VTODO"));
        mapped.component = QStringLiteral("VTODO");
    }
    if (block.begin < 0) {
        mapped.error = QStringLiteral("No VEVENT or VTODO master component");
        return mapped;
    }
    QList<Property> props;
    int depth = 0;
    for (int i = block.begin + 1; i < block.end; ++i) {
        if (lines.at(i).name == QLatin1String("BEGIN")) ++depth;
        else if (lines.at(i).name == QLatin1String("END")) --depth;
        else if (depth == 0) props.append(lines.at(i));
    }
    auto first = [&props](const char *name) -> const Property * {
        for (const Property &p : props) if (p.name == QLatin1String(name)) return &p;
        return nullptr;
    };
    auto text = [&first](const char *name) { const Property *p = first(name); return p ? unescapeText(p->value) : QString(); };
    mapped.uid = text("UID");
    if (mapped.uid.isEmpty()) {
        mapped.error = QStringLiteral("Component has no UID");
        return mapped;
    }
    QVariantMap &f = mapped.fields;
    f.insert(QStringLiteral("title"), text("SUMMARY"));
    f.insert(QStringLiteral("description"), text("DESCRIPTION"));
    if (mapped.component == QLatin1String("VTODO")) {
        static const QMap<QString, QString> statuses{{QStringLiteral("NEEDS-ACTION"), QStringLiteral("todo")},
            {QStringLiteral("IN-PROCESS"), QStringLiteral("doing")}, {QStringLiteral("COMPLETED"), QStringLiteral("done")},
            {QStringLiteral("CANCELLED"), QStringLiteral("cancelled")}};
        const QString custom = text("X-QINDATK-STATUS");
        f.insert(QStringLiteral("status"), custom.isEmpty() ? statuses.value(text("STATUS").toUpper(), QStringLiteral("todo")) : custom);
        f.insert(QStringLiteral("order"), text("X-QINDATK-ORDER").toInt());
        const Property *due = first("DUE");
        const Stamp dueStamp = due ? parseStampValue(due->value, *due, defaultZone) : Stamp();
        f.insert(QStringLiteral("dueDate"), dueStamp.ok ? dueStamp.date.toString(Qt::ISODate) : QString());
        const int priority = text("PRIORITY").toInt();
        f.insert(QStringLiteral("priority"), priority >= 1 && priority <= 4 ? QStringLiteral("High")
                                             : priority == 5 ? QStringLiteral("Normal")
                                             : priority >= 6 && priority <= 9 ? QStringLiteral("Low") : QString());
        f.insert(QStringLiteral("assignee"), text("X-QINDATK-ASSIGNEE"));
        mapped.ok = true;
        return mapped;
    }
    const Property *startProperty = first("DTSTART");
    const Stamp start = startProperty ? parseStampValue(startProperty->value, *startProperty, defaultZone) : Stamp();
    if (!start.ok) {
        mapped.error = QStringLiteral("Event %1 has an invalid DTSTART or unknown time zone").arg(mapped.uid);
        return mapped;
    }
    const QTimeZone zone(start.zone.toUtf8());
    QDate endDate = start.allDay ? start.date.addDays(1) : start.date;
    QTime endTime = start.time;
    if (const Property *end = first("DTEND")) {
        const Stamp stamp = parseStampValue(end->value, *end, start.zone);
        if (!stamp.ok) { mapped.error = QStringLiteral("Event %1 has an invalid DTEND").arg(mapped.uid); return mapped; }
        const QDateTime local = start.allDay ? QDateTime(stamp.date, QTime(0, 0)) : stamp.zoned().toTimeZone(zone);
        endDate = local.date();
        endTime = local.time();
    } else if (const Property *duration = first("DURATION")) {
        const qint64 seconds = durationSeconds(duration->value);
        if (seconds < 0) { mapped.error = QStringLiteral("Event %1 has an invalid DURATION").arg(mapped.uid); return mapped; }
        const QDateTime local = start.allDay ? QDateTime(start.date, QTime(0, 0), QTimeZone::UTC).addSecs(seconds)
                                             : start.zoned().addSecs(seconds).toTimeZone(zone);
        endDate = local.date();
        endTime = local.time();
    }
    f.insert(QStringLiteral("startLocal"), localText(start.date, start.time, start.allDay));
    f.insert(QStringLiteral("endLocal"), localText(endDate, endTime, start.allDay));
    f.insert(QStringLiteral("zone"), start.zone);
    f.insert(QStringLiteral("allDay"), start.allDay);
    f.insert(QStringLiteral("location"), text("LOCATION"));
    const QString rrule = first("RRULE") ? first("RRULE")->value.trimmed() : QString();
    const QVariant rule = rrule.isEmpty() ? QVariant() : parseRule(rrule, start);
    f.insert(QStringLiteral("recurrenceRule"), rule.isValid() ? rule : QVariant::fromValue(nullptr));
    f.insert(QStringLiteral("recurrenceRaw"), rule.isValid() ? QString() : rrule);
    QVariantList exceptions;
    for (const Property &p : std::as_const(props)) {
        if (p.name != QLatin1String("EXDATE")) continue;
        for (const QString &value : p.value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const Stamp stamp = parseStampValue(value, p, start.zone);
            if (!stamp.ok) continue;
            const QDateTime local = stamp.allDay ? QDateTime(stamp.date, QTime(0, 0)) : stamp.zoned().toTimeZone(zone);
            exceptions.append(local.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss")));
        }
    }
    f.insert(QStringLiteral("exceptions"), exceptions);
    f.insert(QStringLiteral("recurrenceOverrides"), block.overrides);
    mapped.ok = true;
    return mapped;
}

QByteArray fromRecord(const QString &component, const QString &uid, const QVariantMap &record, const QByteArray &base)
{
    const QStringList generated = recordLines(component, uid, record);
    const QList<Property> lines = parseLines(base);
    const Block block = findMaster(lines, component);
    QByteArray out;
    if (block.begin < 0) {
        out = foldLine(QStringLiteral("BEGIN:VCALENDAR")) + foldLine(QStringLiteral("VERSION:2.0"))
            + foldLine(QStringLiteral("PRODID:-//QindaTK//Productivity//EN")) + foldLine(QStringLiteral("BEGIN:") + component);
        for (const QString &line : generated) out += foldLine(line);
        return out + foldLine(QStringLiteral("END:") + component) + foldLine(QStringLiteral("END:VCALENDAR"));
    }
    int depth = 0;
    for (int i = 0; i < lines.size(); ++i) {
        const Property &line = lines.at(i);
        const bool inside = i > block.begin && i < block.end;
        if (inside && line.name == QLatin1String("BEGIN")) ++depth;
        if (inside && depth == 0 && managed(component).contains(line.name)) continue;
        if (inside && line.name == QLatin1String("END")) --depth;
        out += foldLine(line.raw);
        if (i == block.begin)
            for (const QString &generatedLine : generated) out += foldLine(generatedLine);
    }
    return out;
}

QString digest(const QString &component, const QVariantMap &record)
{
    QJsonObject subset;
    for (const QString &field : component == QLatin1String("VTODO") ? taskFields() : eventFields())
        subset.insert(field, QJsonValue::fromVariant(record.value(field)));
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(subset).toJson(QJsonDocument::Compact),
                                                        QCryptographicHash::Sha256).toHex());
}

} // namespace QindaTK::ICal
