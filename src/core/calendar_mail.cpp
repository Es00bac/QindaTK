// SPDX-License-Identifier: LGPL-3.0-or-later
#include "calendar_mail.h"
#include "ical_codec.h"
#include "mail_mime.h"
#include <QUuid>

namespace QindaTK {
namespace {
constexpr int MaximumCalendarBytes = 256 * 1024;
QVariantMap failure(const QString &error) { return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}}; }
bool supported(const QString &method)
{ return method == QLatin1String("REQUEST") || method == QLatin1String("REPLY") || method == QLatin1String("CANCEL"); }
QString mailbox(const QString &uri)
{ return uri.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive) ? uri.mid(7) : QString(); }

QVariantMap inspect(const QByteArray &calendar)
{
    if (calendar.isEmpty() || calendar.size() > MaximumCalendarBytes)
        return failure(QStringLiteral("Calendar must be between 1 byte and 256 KiB"));
    auto mapped = ICal::toRecord(calendar, QStringLiteral("UTC"));
    QString recurrenceId;
    if (!mapped.ok) {
        // A changed occurrence in an iTIP message can be the sole VEVENT,
        // without its series master. Decode its display fields as a standalone
        // event while preserving RECURRENCE-ID in the actual scheduling bytes.
        QByteArray displayCalendar;
        for (const auto &line : ICal::parseLines(calendar)) {
            if (line.name == QLatin1String("RECURRENCE-ID")) recurrenceId = line.value;
            else displayCalendar += ICal::foldLine(line.raw);
        }
        if (!recurrenceId.isEmpty()) mapped = ICal::toRecord(displayCalendar, QStringLiteral("UTC"));
    }
    if (!mapped.ok || mapped.component != QLatin1String("VEVENT"))
        return failure(mapped.ok ? QStringLiteral("An event invitation is required") : mapped.error);
    QString method, organizer;
    QVariantList attendees;
    int depth = 0, events = 0, sequence = 0;
    for (const auto &line : ICal::parseLines(calendar)) {
        if (line.name == QLatin1String("BEGIN")) {
            ++depth;
            if (depth == 2 && line.value == QLatin1String("VEVENT")) ++events;
        } else if (line.name == QLatin1String("END")) --depth;
        else if (depth == 1 && line.name == QLatin1String("METHOD")) method = line.value.toUpper();
        else if (depth == 2 && events == 1) {
            if (line.name == QLatin1String("ORGANIZER")) organizer = mailbox(line.value);
            else if (line.name == QLatin1String("SEQUENCE")) sequence = line.value.toInt();
            else if (line.name == QLatin1String("ATTENDEE"))
                attendees.append(QVariantMap{{QStringLiteral("address"), mailbox(line.value)},
                    {QStringLiteral("name"), line.param(QStringLiteral("CN"))},
                    {QStringLiteral("partstat"), line.param(QStringLiteral("PARTSTAT"))},
                    {QStringLiteral("rsvp"), line.param(QStringLiteral("RSVP")).compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0}});
        }
    }
    // An independently changed occurrence may share a UID; callers can
    // inspect those through ICal, but one response must identify one event.
    if (events != 1) return failure(QStringLiteral("Choose one VEVENT for this scheduling message"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("method"), method}, {QStringLiteral("uid"), mapped.uid},
            {QStringLiteral("event"), mapped.fields}, {QStringLiteral("organizer"), organizer},
            {QStringLiteral("attendees"), attendees}, {QStringLiteral("sequence"), sequence},
            {QStringLiteral("calendar"), calendar}, {QStringLiteral("recurrenceIdRaw"), recurrenceId},
            {QStringLiteral("authenticated"), false}};
}
QByteArray withMethod(const QByteArray &calendar, const QString &method)
{
    QByteArray out;
    int depth = 0;
    for (const auto &line : ICal::parseLines(calendar)) {
        if (line.name == QLatin1String("BEGIN")) ++depth;
        const bool replacedMethod = depth == 1 && line.name == QLatin1String("METHOD");
        const bool replacedStatus = method == QLatin1String("CANCEL") && depth == 2 && line.name == QLatin1String("STATUS");
        if (!replacedMethod && !replacedStatus) out += ICal::foldLine(line.raw);
        if (method == QLatin1String("CANCEL") && depth == 2 && line.name == QLatin1String("BEGIN") && line.value == QLatin1String("VEVENT"))
            out += ICal::foldLine(QStringLiteral("STATUS:CANCELLED"));
        if (depth == 1 && line.name == QLatin1String("BEGIN")) out += ICal::foldLine(QStringLiteral("METHOD:") + method);
        if (line.name == QLatin1String("END")) --depth;
    }
    return out;
}
}
QVariantMap CalendarMail::parse(const QByteArray &message) const
{
    if (message.size() > 32 * 1024 * 1024) return failure(QStringLiteral("Mail message exceeds 32 MiB"));
    const auto mime = Mime::parse(message);
    auto parsed = inspect(mime.calendar.toUtf8());
    if (!parsed.value(QStringLiteral("ok")).toBool()) return parsed;
    const QString method = parsed.value(QStringLiteral("method")).toString();
    if (!supported(method) || method != mime.calendarMethod)
        return failure(QStringLiteral("Calendar METHOD and MIME method must match REQUEST, REPLY or CANCEL"));
    parsed.insert(QStringLiteral("senderAddress"), mime.fromAddress);
    return parsed;
}
QVariantMap CalendarMail::compose(const QString &rawMethod, const QByteArray &calendar, const QVariantMap &draft) const
{
    const QString method = rawMethod.toUpper();
    if (!supported(method)) return failure(QStringLiteral("Unsupported scheduling method"));
    const auto parsed = inspect(calendar);
    if (!parsed.value(QStringLiteral("ok")).toBool()) return parsed;
    if (!Mime::validAddress(parsed.value(QStringLiteral("organizer")).toString()))
        return failure(QStringLiteral("A valid organizer address is required"));
    QString error;
    const auto from = Mime::parseAddressList(draft.value(QStringLiteral("from")).toString(), &error);
    if (!error.isEmpty() || from.size() != 1) return failure(QStringLiteral("One valid sender address is required"));
    Mime::Outgoing out;
    out.from = from.first();
    out.to = Mime::parseAddressList(draft.value(QStringLiteral("to")).toString(), &error);
    if (error.isEmpty()) out.cc = Mime::parseAddressList(draft.value(QStringLiteral("cc")).toString(), &error);
    if (error.isEmpty()) out.replyTo = Mime::parseAddressList(draft.value(QStringLiteral("replyTo")).toString(), &error);
    if (!error.isEmpty()) return failure(error);
    if (out.to.isEmpty() && out.cc.isEmpty()) return failure(QStringLiteral("A recipient is required"));
    out.subject = draft.value(QStringLiteral("subject"), parsed.value(QStringLiteral("event")).toMap().value(QStringLiteral("title"))).toString();
    out.plain = draft.value(QStringLiteral("body")).toString();
    out.html = draft.value(QStringLiteral("bodyHtml")).toString();
    out.calendarMethod = method;
    out.calendar = withMethod(calendar, method);
    out.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral("@qindatk.invalid");
    const QByteArray bytes = Mime::build(out);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("message"), bytes}, {QStringLiteral("calendar"), out.calendar},
            {QStringLiteral("method"), method}, {QStringLiteral("messageId"), out.messageId}};
}
QVariantMap CalendarMail::reply(const QByteArray &calendar, const QString &address, const QString &rawPartstat) const
{
    const QString partstat = rawPartstat.toUpper();
    if (!Mime::validAddress(address) || !QStringList{QStringLiteral("ACCEPTED"), QStringLiteral("TENTATIVE"), QStringLiteral("DECLINED")}.contains(partstat))
        return failure(QStringLiteral("A valid attendee and participation status are required"));
    const auto parsed = inspect(calendar);
    if (!parsed.value(QStringLiteral("ok")).toBool()) return parsed;
    if (parsed.value(QStringLiteral("method")) != QLatin1String("REQUEST")) return failure(QStringLiteral("Only a REQUEST can be answered"));
    if (!Mime::validAddress(parsed.value(QStringLiteral("organizer")).toString()))
        return failure(QStringLiteral("A valid organizer address is required"));
    bool found = false;
    int depth = 0;
    QByteArray out;
    for (const auto &line : ICal::parseLines(withMethod(calendar, QStringLiteral("REPLY")))) {
        if (line.name == QLatin1String("BEGIN")) ++depth;
        if (depth == 2 && line.name == QLatin1String("ATTENDEE")) {
            if (mailbox(line.value).compare(address, Qt::CaseInsensitive) != 0) continue;
            QString updated = QStringLiteral("ATTENDEE");
            for (const auto &[key, value] : line.params) {
                if (key == QLatin1String("PARTSTAT") || key == QLatin1String("RSVP")) continue;
                // Parsed quoted parameter values have no line separators;
                // quote delimiters so names containing commas stay one value.
                QString escaped = value;
                escaped.replace(QLatin1Char('"'), QStringLiteral("^'"));
                updated += QLatin1Char(';') + key + QStringLiteral("=\"") + escaped + QLatin1Char('"');
            }
            updated += QStringLiteral(";PARTSTAT=") + partstat + QStringLiteral(";RSVP=FALSE:mailto:") + address;
            out += ICal::foldLine(updated);
            found = true;
        } else out += ICal::foldLine(line.raw);
        if (line.name == QLatin1String("END")) --depth;
    }
    if (!found) return failure(QStringLiteral("This address is not an attendee of the request"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("calendar"), out}, {QStringLiteral("method"), QStringLiteral("REPLY")},
            {QStringLiteral("to"), parsed.value(QStringLiteral("organizer"))}};
}
}
