// SPDX-License-Identifier: LGPL-3.0-or-later
// Whole-calendar iCalendar (RFC 5545) streams for IcsCalendarProvider: split
// a VCALENDAR into per-UID items that ical_codec.cpp maps one at a time, and
// join per-record VCALENDARs back into one file. No second parser: every
// component still goes through parseLines/toRecord/fromRecord.
#include "ical_codec.h"
#include "ical_codec_p.h"

#include <QCryptographicHash>
#include <QHash>
#include <QMap>
#include <QSet>

namespace QindaTK::ICal {
using namespace detail;
namespace {
struct ComponentBlock {
    QString type;                  // VEVENT, VTODO, VTIMEZONE, ...
    QList<Property> lines;         // BEGIN..END inclusive, nested components kept
};

QString calendarProdId() { return QStringLiteral("PRODID:-//QindaTK//Productivity//EN"); }

QString propertyOf(const ComponentBlock &block, const char *name)
{
    int depth = 0;
    for (const Property &line : block.lines) {
        if (line.name == QLatin1String("BEGIN")) ++depth;
        else if (line.name == QLatin1String("END")) --depth;
        else if (depth == 1 && line.name == QLatin1String(name)) return unescapeText(line.value).trimmed();
    }
    return {};
}

// Top-level components of every VCALENDAR in the stream, plus X-WR-CALNAME.
bool splitBlocks(const QList<Property> &lines, QList<ComponentBlock> *blocks, QString *name)
{
    int depth = 0;
    bool calendar = false;
    ComponentBlock current;
    for (const Property &line : lines) {
        if (line.name == QLatin1String("BEGIN")) {
            ++depth;
            if (depth == 1) calendar = calendar || line.value.trimmed().toUpper() == QLatin1String("VCALENDAR");
            if (depth == 2) current = {line.value.trimmed().toUpper(), {}};
        }
        if (depth >= 2) current.lines.append(line);
        else if (depth == 1 && line.name == QLatin1String("X-WR-CALNAME") && name->isEmpty())
            *name = unescapeText(line.value).trimmed();
        if (line.name == QLatin1String("END")) {
            if (depth == 2) blocks->append(current);
            depth = qMax(0, depth - 1);
        }
    }
    return calendar;
}

QString rewriteTzid(const Property &line, const QHash<QString, QString> &aliases)
{
    const QString tzid = line.param(QStringLiteral("TZID"));
    if (tzid.isEmpty() || !aliases.contains(tzid)) return line.raw;
    QString prefix = line.raw.left(line.raw.size() - line.value.size());
    prefix.replace(QStringLiteral("TZID=\"") + tzid + QLatin1Char('"'), QStringLiteral("TZID=") + aliases.value(tzid));
    prefix.replace(QStringLiteral("TZID=") + tzid, QStringLiteral("TZID=") + aliases.value(tzid));
    return prefix + line.value;
}

QByteArray header()
{
    return foldLine(QStringLiteral("BEGIN:VCALENDAR")) + foldLine(QStringLiteral("VERSION:2.0")) + foldLine(calendarProdId());
}
} // namespace

ParsedCalendar parseCalendar(const QByteArray &text, const QString &defaultZone, int maximumItems)
{
    ParsedCalendar parsed;
    QList<ComponentBlock> blocks;
    if (!splitBlocks(parseLines(text), &blocks, &parsed.name)) {
        parsed.error = QStringLiteral("The data is not an iCalendar (no BEGIN:VCALENDAR)");
        return parsed;
    }
    parsed.ok = true;
    // VTIMEZONEs: raw text by TZID, and an IANA alias for vendor TZIDs.
    QHash<QString, QByteArray> zones;
    QHash<QString, QString> aliases;
    for (const ComponentBlock &block : std::as_const(blocks)) {
        if (block.type != QLatin1String("VTIMEZONE")) continue;
        const QString tzid = propertyOf(block, "TZID");
        if (tzid.isEmpty() || zones.contains(tzid)) continue;
        QString iana = resolveZone(tzid);
        if (iana.isEmpty()) iana = resolveZone(propertyOf(block, "X-LIC-LOCATION"));
        if (!iana.isEmpty() && iana != tzid) aliases.insert(tzid, iana);
        QByteArray raw;
        for (const Property &line : block.lines)
            raw += foldLine(line.name == QLatin1String("TZID") && aliases.contains(tzid)
                                ? QStringLiteral("TZID:") + aliases.value(tzid) : line.raw);
        zones.insert(tzid, raw);
    }
    // Group VEVENT/VTODO blocks by UID, keeping the order of first appearance.
    QList<QString> order;
    QHash<QString, QList<const ComponentBlock *>> groups;
    QHash<QString, QByteArray> synthetic;
    QMap<QString, int> unsupported;
    for (const ComponentBlock &block : std::as_const(blocks)) {
        if (block.type == QLatin1String("VTIMEZONE")) continue;
        if (block.type != QLatin1String("VEVENT") && block.type != QLatin1String("VTODO")) {
            ++unsupported[block.type];
            continue;
        }
        QString uid = propertyOf(block, "UID");
        if (uid.isEmpty()) {
            // DTSTAMP is left out: generators stamp every download anew.
            QByteArray source;
            for (const Property &line : block.lines)
                if (line.name != QLatin1String("DTSTAMP")) source += line.raw.toUtf8() + '\n';
            uid = QStringLiteral("qindatk-")
                + QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha1).toHex());
            synthetic.insert(uid, source);
        }
        if (!groups.contains(uid)) {
            if (order.size() >= maximumItems) { parsed.truncated = true; continue; }
            order.append(uid);
        }
        groups[uid].append(&block);
    }
    for (auto it = unsupported.cbegin(); it != unsupported.cend(); ++it)
        parsed.warnings.append(QStringLiteral("%1 %2 component(s) skipped (not supported)").arg(it.value()).arg(it.key()));
    if (!synthetic.isEmpty())
        parsed.warnings.append(QStringLiteral("%1 component(s) without UID were given a stable UID").arg(synthetic.size()));
    if (parsed.truncated)
        parsed.warnings.append(QStringLiteral("Only the first %1 items were read").arg(maximumItems));

    for (const QString &uid : std::as_const(order)) {
        QSet<QString> referenced;
        QByteArray body;
        for (const ComponentBlock *block : groups.value(uid)) {
            for (qsizetype i = 0; i < block->lines.size(); ++i) {
                const Property &line = block->lines.at(i);
                const QString tzid = line.param(QStringLiteral("TZID"));
                if (!tzid.isEmpty()) referenced.insert(tzid);
                body += foldLine(rewriteTzid(line, aliases));
                // A synthesized UID goes right after BEGIN so toRecord finds it.
                if (i == 0 && synthetic.contains(uid)) body += foldLine(QStringLiteral("UID:") + uid);
            }
        }
        QByteArray ics = header();
        for (const QString &tzid : std::as_const(referenced)) ics += zones.value(tzid);
        ics += body + foldLine(QStringLiteral("END:VCALENDAR"));
        CalendarItem item{toRecord(ics, defaultZone), ics};
        if (!item.mapped.ok) {
            item.mapped.uid = uid;
            parsed.warnings.append(QStringLiteral("%1: %2").arg(uid, item.mapped.error));
        }
        parsed.items.append(item);
    }
    return parsed;
}

QByteArray mergeCalendars(const QList<QByteArray> &calendars, const QString &name)
{
    QByteArray zones, components;
    QSet<QString> seenZones;
    for (const QByteArray &calendar : calendars) {
        QList<ComponentBlock> blocks;
        QString ignored;
        splitBlocks(parseLines(calendar), &blocks, &ignored);
        for (const ComponentBlock &block : std::as_const(blocks)) {
            QByteArray raw;
            for (const Property &line : block.lines) raw += foldLine(line.raw);
            if (block.type == QLatin1String("VTIMEZONE")) {
                const QString tzid = propertyOf(block, "TZID");
                if (seenZones.contains(tzid)) continue;
                seenZones.insert(tzid);
                zones += raw;
            } else {
                components += raw;
            }
        }
    }
    QByteArray out = header() + foldLine(QStringLiteral("CALSCALE:GREGORIAN"));
    if (!name.isEmpty()) out += foldLine(QStringLiteral("X-WR-CALNAME:") + escapeText(name));
    return out + zones + components + foldLine(QStringLiteral("END:VCALENDAR"));
}

} // namespace QindaTK::ICal
