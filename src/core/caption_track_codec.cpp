// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caption_track_codec.h"

#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <limits>

namespace {
constexpr qsizetype kMaximumInput = 8 * 1024 * 1024;
constexpr int kMaximumCues = 100000;

QVector<QString> blocks(const QString &input)
{
    QString normalized = input;
    if (!normalized.isEmpty() && normalized.front() == QChar(0xfeff))
        normalized.remove(0, 1);
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const auto lines = normalized.split(QLatin1Char('\n'));
    QVector<QString> result;
    QString current;
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) {
            if (!current.isEmpty()) { result.append(current); current.clear(); }
        } else {
            if (!current.isEmpty()) current += QLatin1Char('\n');
            current += line;
        }
    }
    if (!current.isEmpty()) result.append(current);
    return result;
}

QString unescapeVtt(QString text)
{
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&nbsp;"), QString(QChar(0x00a0)));
    text.replace(QStringLiteral("&lrm;"), QString(QChar(0x200e)));
    text.replace(QStringLiteral("&rlm;"), QString(QChar(0x200f)));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return text;
}

QString escapeVtt(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return text;
}

// A blank line ends a cue in both sidecar formats, so cue text never carries one.
QString withoutBlankLines(QString text)
{
    while (text.contains(QStringLiteral("\n\n"))) text.replace(QStringLiteral("\n\n"), QStringLiteral("\n"));
    return text;
}

bool meaningfulLanguage(const QString &language)
{
    return !language.isEmpty() && language != QLatin1String("und");
}

bool parseBlocks(const QString &input, const QString &language, const bool webVtt,
                 QVector<CaptionCueRecord> *cues, QString *error, QString *headerLanguage)
{
    if (input.size() > kMaximumInput) {
        *error = QStringLiteral("Caption input exceeds the 8 MiB bound.");
        return false;
    }
    auto parts = blocks(input);
    if (webVtt && !parts.isEmpty() && parts.first().startsWith(QStringLiteral("WEBVTT"))) {
        // Header lines up to the first blank line; `Language:` names the track.
        for (const QString &line : parts.first().split(QLatin1Char('\n'))) {
            if (headerLanguage && line.startsWith(QStringLiteral("Language:")))
                *headerLanguage = line.mid(9).trimmed();
        }
        parts.removeFirst();
    }
    static const QRegularExpression langSpan(QStringLiteral("^<lang ([^>\\s]+)>(.*)</lang>$"),
                                             QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpression arrow(QStringLiteral("^\\s*(.+?)\\s+-->\\s+([^\\s]+)(?:\\s+.*)?$"));
    QVector<CaptionCueRecord> parsed;
    int generatedId = 1;
    for (const QString &part : parts) {
        if (parsed.size() >= kMaximumCues) {
            *error = QStringLiteral("Caption track exceeds the 100000 cue bound.");
            return false;
        }
        const QStringList lines = part.split(QLatin1Char('\n'));
        int timingLine = 0;
        QString cueId;
        if (webVtt) {
            if (lines.first().startsWith(QStringLiteral("NOTE")) || lines.first().startsWith(QStringLiteral("STYLE"))
                || lines.first().startsWith(QStringLiteral("REGION")))
                continue;
            if (!lines.first().contains(QStringLiteral("-->"))) {
                cueId = lines.first().trimmed();
                timingLine = 1;
            }
        } else {
            if (!lines.first().contains(QStringLiteral("-->")))
                timingLine = 1; // SRT's display sequence number.
        }
        if (timingLine >= lines.size()) {
            *error = QStringLiteral("Caption cue is missing its timing line.");
            return false;
        }
        const auto match = arrow.match(lines.at(timingLine));
        qint64 inMs = 0, outMs = 0;
        if (!match.hasMatch() || !CaptionTrackCodec::parseTime(match.captured(1), &inMs)
            || !CaptionTrackCodec::parseTime(match.captured(2), &outMs) || outMs <= inMs) {
            *error = QStringLiteral("Caption cue has invalid or non-positive timing.");
            return false;
        }
        CaptionCueRecord cue;
        cue.id = cueId.isEmpty() ? QStringLiteral("cue-%1").arg(generatedId++) : cueId;
        cue.inMs = inMs;
        cue.outMs = outMs;
        cue.language = language.isEmpty() ? QStringLiteral("und") : language;
        cue.text = lines.mid(timingLine + 1).join(QLatin1Char('\n'));
        if (webVtt) {
            const auto span = langSpan.match(cue.text);
            if (span.hasMatch()) {
                cue.language = span.captured(1);
                cue.text = span.captured(2);
            }
            cue.text = unescapeVtt(cue.text);
        }
        parsed.append(cue);
    }
    if (parsed.isEmpty()) {
        *error = QStringLiteral("No supported caption cues were found.");
        return false;
    }
    *cues = parsed;
    return true;
}

bool uniqueAndValid(const QVector<CaptionCueRecord> &cues, QString *error)
{
    if (cues.isEmpty() || cues.size() > kMaximumCues) {
        *error = QStringLiteral("Caption track must contain 1 to 100000 cues.");
        return false;
    }
    QSet<QString> ids;
    for (const auto &cue : cues) {
        if (cue.lane < 0 || cue.lane >= 16) {
            *error = QStringLiteral("Caption lane is outside the supported 0–15 range.");
            return false;
        }
    }
    for (const auto &cue : cues) {
        if (cue.id.isEmpty() || ids.contains(cue.id) || cue.inMs < 0 || cue.outMs <= cue.inMs) {
            *error = QStringLiteral("Caption track contains duplicate IDs or invalid timing.");
            return false;
        }
        ids.insert(cue.id);
    }
    for (qsizetype i = 0; i < cues.size(); ++i) {
        for (qsizetype j = i + 1; j < cues.size(); ++j) {
            if (cues.at(i).lane == cues.at(j).lane && cues.at(i).inMs < cues.at(j).outMs
                && cues.at(j).inMs < cues.at(i).outMs) {
                *error = QStringLiteral("Caption JSON contains overlapping cues in one lane.");
                return false;
            }
        }
    }
    return true;
}
}

namespace CaptionTrackCodec {
bool parseTime(const QString &text, qint64 *milliseconds)
{
    static const QRegularExpression pattern(QStringLiteral("^(?:(\\d+):)?([0-5]?\\d):([0-5]?\\d)[.,](\\d{3})$"));
    const auto match = pattern.match(text.trimmed());
    if (!match.hasMatch()) return false;
    bool okH = true, okM = false, okS = false, okMs = false;
    const qint64 hours = match.captured(1).isEmpty() ? 0 : match.captured(1).toLongLong(&okH);
    const qint64 minutes = match.captured(2).toLongLong(&okM);
    const qint64 seconds = match.captured(3).toLongLong(&okS);
    const qint64 fraction = match.captured(4).toLongLong(&okMs);
    const qint64 remainder = (minutes * 60 + seconds) * 1000 + fraction;
    if (!okH || !okM || !okS || !okMs || minutes > 59 || seconds > 59
        || hours > (std::numeric_limits<qint64>::max() - remainder) / 3600000) return false;
    *milliseconds = ((hours * 60 + minutes) * 60 + seconds) * 1000 + fraction;
    return true;
}

QString formatTime(qint64 milliseconds, QChar decimal)
{
    milliseconds = qMax<qint64>(0, milliseconds);
    const qint64 hours = milliseconds / 3600000;
    const qint64 minutes = (milliseconds / 60000) % 60;
    const qint64 seconds = (milliseconds / 1000) % 60;
    const qint64 fraction = milliseconds % 1000;
    return QStringLiteral("%1:%2:%3%4%5")
        .arg(hours, 2, 10, QLatin1Char('0')).arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0')).arg(decimal)
        .arg(fraction, 3, 10, QLatin1Char('0'));
}

bool parseWebVtt(const QString &text, const QString &language, QVector<CaptionCueRecord> *cues, QString *error,
                 QString *headerLanguage)
{ return parseBlocks(text, language, true, cues, error, headerLanguage); }

bool parseSrt(const QString &text, const QString &language, QVector<CaptionCueRecord> *cues, QString *error)
{ return parseBlocks(text, language, false, cues, error, nullptr); }

QString toWebVtt(const QVector<CaptionCueRecord> &cues, const QString &trackLanguage)
{
    QString output = QStringLiteral("WEBVTT\n");
    if (meaningfulLanguage(trackLanguage))
        output += QStringLiteral("Language: ") + trackLanguage + QLatin1Char('\n');
    output += QLatin1Char('\n');
    for (const auto &cue : cues) {
        QString text = escapeVtt(withoutBlankLines(cue.text));
        if (meaningfulLanguage(trackLanguage) && meaningfulLanguage(cue.language) && cue.language != trackLanguage)
            text = QStringLiteral("<lang ") + cue.language + QLatin1Char('>') + text + QStringLiteral("</lang>");
        output += cue.id + QLatin1Char('\n') + formatTime(cue.inMs) + QStringLiteral(" --> ")
            + formatTime(cue.outMs) + QLatin1Char('\n') + text + QStringLiteral("\n\n");
    }
    return output;
}

QString toSrt(const QVector<CaptionCueRecord> &cues)
{
    QString output;
    for (qsizetype i = 0; i < cues.size(); ++i) {
        const auto &cue = cues.at(i);
        output += QString::number(i + 1) + QLatin1Char('\n') + formatTime(cue.inMs, QLatin1Char(','))
            + QStringLiteral(" --> ") + formatTime(cue.outMs, QLatin1Char(','))
            + QLatin1Char('\n') + withoutBlankLines(cue.text) + QStringLiteral("\n\n");
    }
    return output;
}

QJsonObject toJson(const QVector<CaptionCueRecord> &cues, const QString &trackId, const QString &language)
{
    QJsonArray records;
    for (const auto &cue : cues) {
        records.append(QJsonObject{{QStringLiteral("id"), cue.id}, {QStringLiteral("inMs"), QString::number(cue.inMs)},
            {QStringLiteral("outMs"), QString::number(cue.outMs)}, {QStringLiteral("text"), cue.text},
            {QStringLiteral("language"), cue.language}, {QStringLiteral("styleId"), cue.styleId},
            {QStringLiteral("lane"), cue.lane}});
    }
    return {{QStringLiteral("version"), 1}, {QStringLiteral("trackId"), trackId},
            {QStringLiteral("language"), language}, {QStringLiteral("cues"), records}};
}

bool fromJson(const QJsonObject &json, QVector<CaptionCueRecord> *cues, QString *trackId,
              QString *language, QString *error)
{
    if (json.value(QStringLiteral("version")).toInt() != 1 || !json.value(QStringLiteral("cues")).isArray()) {
        *error = QStringLiteral("Unsupported caption track JSON document.");
        return false;
    }
    QVector<CaptionCueRecord> parsed;
    const auto records = json.value(QStringLiteral("cues")).toArray();
    if (records.size() > kMaximumCues) { *error = QStringLiteral("Caption track exceeds cue bound."); return false; }
    for (const auto &value : records) {
        const auto object = value.toObject();
        bool inOk = false, outOk = false;
        CaptionCueRecord cue;
        cue.id = object.value(QStringLiteral("id")).toString();
        cue.inMs = object.value(QStringLiteral("inMs")).toString().toLongLong(&inOk);
        cue.outMs = object.value(QStringLiteral("outMs")).toString().toLongLong(&outOk);
        cue.text = object.value(QStringLiteral("text")).toString();
        cue.language = object.value(QStringLiteral("language")).toString(QStringLiteral("und"));
        cue.styleId = object.value(QStringLiteral("styleId")).toString(QStringLiteral("default"));
        cue.lane = object.value(QStringLiteral("lane")).toInt();
        if (!inOk || !outOk) { *error = QStringLiteral("Caption JSON times must be decimal millisecond strings."); return false; }
        parsed.append(cue);
    }
    if (!uniqueAndValid(parsed, error)) return false;
    *cues = parsed;
    *trackId = json.value(QStringLiteral("trackId")).toString(QStringLiteral("captions"));
    *language = json.value(QStringLiteral("language")).toString(QStringLiteral("und"));
    return true;
}
}
