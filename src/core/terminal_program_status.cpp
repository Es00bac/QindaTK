// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_program_status_p.h"

#include <QStringDecoder>
#include <algorithm>
#include <optional>

namespace QindaTK::ProgramStatus {
namespace {
constexpr qsizetype maxSequenceBytes = 4096;
constexpr qsizetype maxKeyBytes = 16;
constexpr qsizetype maxAppBytes = 32;
constexpr qsizetype maxIdBytes = 128;
constexpr qsizetype maxIdSegmentBytes = 32;
constexpr int maxIdLevels = 8;
constexpr qsizetype maxMessageEncoded = 2732, maxMessageDecoded = 2048;
constexpr qsizetype maxTitleEncoded = 256, maxTitleDecoded = 192;

QByteArrayView trimmed(QByteArrayView text)
{
    while (!text.isEmpty() && (text.front() == ' ' || text.front() == '\t')) text = text.sliced(1);
    while (!text.isEmpty() && (text.back() == ' ' || text.back() == '\t')) text.chop(1);
    return text;
}

bool isNameByte(char byte)
{
    return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
        || byte == '_' || byte == '.' || byte == '+' || byte == '-';
}

bool isValueByte(char byte) { return isNameByte(byte) || byte == ',' || byte == '/' || byte == '='; }

bool isName(QByteArrayView text)
{
    return !text.isEmpty() && std::all_of(text.begin(), text.end(), isNameByte);
}

bool isId(QByteArrayView id)
{
    if (id.isEmpty() || id.size() > maxIdBytes) return false;
    int levels = 0;
    for (qsizetype start = 0; start <= id.size();) {
        qsizetype end = id.indexOf('/', start);
        if (end < 0) end = id.size();
        const QByteArrayView segment = id.sliced(start, end - start);
        if (segment.size() > maxIdSegmentBytes || !isName(segment) || ++levels > maxIdLevels) return false;
        start = end + 1;
    }
    return true;
}

// Standard alphabet, optional padding, canonical trailing bits. A lenient
// decoder would let two different strings name the same text.
std::optional<QByteArray> decodeBase64(QByteArrayView text)
{
    qsizetype length = text.size();
    qsizetype padding = 0;
    while (padding < 2 && length > 0 && text.at(length - 1) == '=') { --length; ++padding; }
    if (length % 4 == 1 || (padding && (length + padding) % 4 != 0)) return std::nullopt;
    QByteArray decoded;
    decoded.reserve(length * 3 / 4);
    quint32 bits = 0;
    int count = 0;
    for (qsizetype index = 0; index < length; ++index) {
        const char byte = text.at(index);
        int value;
        if (byte >= 'A' && byte <= 'Z') value = byte - 'A';
        else if (byte >= 'a' && byte <= 'z') value = byte - 'a' + 26;
        else if (byte >= '0' && byte <= '9') value = byte - '0' + 52;
        else if (byte == '+') value = 62;
        else if (byte == '/') value = 63;
        else return std::nullopt;
        bits = (bits << 6) | quint32(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            decoded += char((bits >> count) & 0xff);
        }
    }
    if (bits & ((1u << count) - 1)) return std::nullopt;
    return decoded;
}

// Text shown outside the grid must not carry controls, and direction
// overrides or invisible formatting are removed rather than trusted.
std::optional<QString> decodeText(QByteArrayView encoded, qsizetype maxDecoded)
{
    const auto bytes = decodeBase64(encoded);
    if (!bytes || bytes->size() > maxDecoded) return std::nullopt;
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = decoder(*bytes);
    if (decoder.hasError()) return std::nullopt;
    for (const QChar ch : std::as_const(text)) {
        const ushort code = ch.unicode();
        if (code < 0x20 || (code >= 0x7f && code <= 0x9f)) return std::nullopt;
    }
    text.removeIf([](QChar ch) {
        const ushort code = ch.unicode();
        return (code >= 0x200b && code <= 0x200f) || (code >= 0x202a && code <= 0x202e)
            || (code >= 0x2066 && code <= 0x2069) || code == 0x061c || code == 0xfeff;
    });
    return text;
}

int statePriority(const QByteArray &state)
{
    if (state == "blocked") return 5;
    if (state == "error") return 4;
    if (state == "working") return 3;
    if (state == "done") return 2;
    return 1;
}
} // namespace

QByteArray queryReply()
{
    return QByteArrayLiteral("?:states=idle,working,done,blocked,error:kinds=permission,question,auth");
}

Report parse(QByteArrayView body, qsizetype sequenceBytes)
{
    Report report;
    if (sequenceBytes > maxSequenceBytes) return report;
    QByteArrayView state, kind, progress, app, title, message, id;
    bool hasId = false, first = true;
    for (qsizetype start = 0; start <= body.size();) {
        qsizetype end = body.indexOf(':', start);
        if (end < 0) end = body.size();
        const QByteArrayView pair = trimmed(body.sliced(start, end - start));
        start = end + 1;
        if (first && pair == "?") {
            report.type = Report::Query;
            return report;
        }
        first = false;
        const qsizetype equals = pair.indexOf('=');
        const QByteArrayView key = trimmed(equals < 0 ? pair : pair.first(equals));
        // Limits are checked on every pair before any of them is applied.
        if (key.size() > maxKeyBytes) return report;
        if (equals <= 0 || !std::all_of(key.begin(), key.end(), [](char c) { return c >= 'a' && c <= 'z'; }))
            continue;
        const QByteArrayView value = trimmed(pair.sliced(equals + 1));
        if ((key == "app" && value.size() > maxAppBytes) || (key == "id" && value.size() > maxIdBytes)
            || (key == "msg" && value.size() > maxMessageEncoded)
            || (key == "title" && value.size() > maxTitleEncoded))
            return report;
        if (key == "id") {
            // Every id occurrence must be valid; a report never falls back to
            // a different record than the one it named.
            if (!isId(value)) return report;
            id = value;
            hasId = true;
            continue;
        }
        if (!std::all_of(value.begin(), value.end(), isValueByte)) continue;
        if (key == "state") state = value;
        else if (key == "kind") kind = value;
        else if (key == "progress") progress = value;
        else if (key == "app") app = value;
        else if (key == "title") title = value;
        else if (key == "msg") message = value;
    }
    const bool clear = state == "clear";
    if (!clear && state != "idle" && state != "working" && state != "done" && state != "blocked"
        && state != "error")
        return report;
    // Only the value that won is decoded; an overridden one is never trusted.
    if (!title.isEmpty()) {
        const auto text = decodeText(title, maxTitleDecoded);
        if (!text) return report;
        report.title = *text;
    }
    if (!message.isEmpty()) {
        const auto text = decodeText(message, maxMessageDecoded);
        if (!text) return report;
        report.message = *text;
    }
    if (hasId) report.id = id.toByteArray();
    if (clear) {
        report = Report{Report::Clear, report.id, {}, {}, {}, -1, {}, {}};
        return report;
    }
    report.type = Report::Status;
    report.state = state.toByteArray();
    if (isName(app)) report.app = app.toByteArray();
    if (report.state == "blocked" && (kind == "permission" || kind == "question" || kind == "auth"))
        report.kind = kind.toByteArray();
    if ((report.state == "working" || report.state == "blocked") && !progress.isEmpty()
        && progress.size() <= 6 && std::all_of(progress.begin(), progress.end(),
                                                [](char c) { return c >= '0' && c <= '9'; })) {
        const int value = progress.toInt();
        if (value <= 100) report.progress = value;
    }
    return report;
}

bool Store::apply(const Report &report)
{
    if (report.type == Report::Clear) {
        if (report.id.isEmpty()) return clear();
        const QByteArray prefix = report.id + '/';
        return m_records.removeIf([&](const Record &record) {
            return record.id == report.id || record.id.startsWith(prefix);
        }) > 0;
    }
    if (report.type != Report::Status) return false;
    Record record{report.id, report.state, report.kind, report.app, report.progress,
                  report.title, report.message, ++m_clock};
    for (Record &existing : m_records) {
        if (existing.id == report.id) {
            existing = std::move(record);
            return true;
        }
    }
    if (m_records.size() >= capacity) {
        const auto oldest = std::min_element(m_records.begin(), m_records.end(),
            [](const Record &a, const Record &b) { return a.updated < b.updated; });
        m_records.erase(oldest);
    }
    m_records.append(std::move(record));
    return true;
}

bool Store::dropTransient(bool dropIdle)
{
    return m_records.removeIf([dropIdle](const Record &record) {
        return record.state == "working" || record.state == "blocked" || (dropIdle && record.state == "idle");
    }) > 0;
}

bool Store::acknowledge()
{
    return m_records.removeIf([](const Record &record) {
        return record.state == "done" || record.state == "error";
    }) > 0;
}

bool Store::clear()
{
    const bool changed = !m_records.isEmpty();
    m_records.clear();
    return changed;
}

QByteArray Store::effectiveApp(const Record &record) const
{
    if (!record.app.isEmpty()) return record.app;
    // The nearest ancestor with an app names it; a parent need not exist.
    QByteArray id = record.id;
    while (!id.isEmpty()) {
        const qsizetype slash = id.lastIndexOf('/');
        id = slash < 0 ? QByteArray() : id.first(slash);
        for (const Record &candidate : m_records)
            if (candidate.id == id && !candidate.app.isEmpty()) return candidate.app;
    }
    return {};
}

QVariantMap Store::toMap(const Record &record) const
{
    return {{QStringLiteral("id"), QString::fromLatin1(record.id)},
            {QStringLiteral("state"), QString::fromLatin1(record.state)},
            {QStringLiteral("kind"), QString::fromLatin1(record.kind)},
            {QStringLiteral("app"), QString::fromLatin1(effectiveApp(record))},
            {QStringLiteral("progress"), record.progress},
            {QStringLiteral("title"), record.title},
            {QStringLiteral("message"), record.message}};
}

QVariantMap Store::summary() const
{
    const Record *chosen = nullptr;
    for (const Record &record : m_records) {
        if (!chosen || statePriority(record.state) > statePriority(chosen->state)
            || (statePriority(record.state) == statePriority(chosen->state)
                && record.updated > chosen->updated))
            chosen = &record;
    }
    if (!chosen) return {};
    QVariantMap map = toMap(*chosen);
    map.insert(QStringLiteral("records"), int(m_records.size()));
    return map;
}

QVariantList Store::records() const
{
    QVariantList list;
    list.reserve(m_records.size());
    for (const Record &record : m_records) list.append(toMap(record));
    return list;
}

} // namespace QindaTK::ProgramStatus
