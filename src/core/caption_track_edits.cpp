// SPDX-License-Identifier: LGPL-3.0-or-later
// CaptionTrackModel's structural edits (split, merge, shift), reading
// warnings, format capabilities and embedded-caption import
// (docs/caption-rendering.md). Timing, validation and undo live in
// caption_track_model.cpp; every edit here is one undo step.
#include "caption_track_model.h"
#include "media_captions.h"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {
bool signedMilliseconds(const QString &text, qint64 *value)
{
    static const QRegularExpression digits(QStringLiteral("^[+-]?(0|[1-9][0-9]*)$"));
    if (!digits.match(text).hasMatch()) return false;
    bool ok = false;
    *value = text.toLongLong(&ok);
    return ok;
}

void sortByTime(QVector<CaptionCueRecord> *cues)
{
    std::stable_sort(cues->begin(), cues->end(), [](const auto &a, const auto &b) {
        if (a.inMs != b.inMs) return a.inMs < b.inMs;
        return a.id < b.id;
    });
}

bool overlaps(const CaptionCueRecord &a, const CaptionCueRecord &b)
{
    return a.inMs < b.outMs && b.inMs < a.outMs;
}

// The whitespace index nearest `wanted`, or -1 when the text has none.
int nearestWhitespace(const QString &text, int wanted)
{
    int best = -1;
    for (int i = 0; i < text.size(); ++i) {
        if (!text.at(i).isSpace()) continue;
        if (best < 0 || std::abs(i - wanted) < std::abs(best - wanted)) best = i;
    }
    return best;
}

int visibleCharacters(const QString &text)
{
    int count = 0;
    for (const QChar ch : text)
        if (ch != QLatin1Char('\n')) ++count;
    return count;
}
} // namespace

void CaptionTrackModel::pushUndo()
{
    m_undo.append(m_cues);
    if (m_undo.size() > 100) m_undo.removeFirst();
}

QString CaptionTrackModel::allocateId()
{
    QString id;
    do { id = QStringLiteral("cue-%1").arg(m_nextId++, 3, 10, QLatin1Char('0')); }
    while (indexOf(m_cues, id) >= 0);
    return id;
}

bool CaptionTrackModel::rejectEdit(const QString &message)
{
    m_error = message;
    setAck(QStringLiteral("rejected"), message);
    emit changed();
    return false;
}

void CaptionTrackModel::acceptEdit(QVector<CaptionCueRecord> cues, const QString &selected, const QString &message)
{
    pushUndo();
    sortByTime(&cues);
    m_cues = cues;
    m_preview.clear();
    m_pendingRequest.clear();
    m_pendingCue.clear();
    m_selectedId = selected;
    m_error.clear();
    setAck(QStringLiteral("accepted"), message);
    emit changed();
}

bool CaptionTrackModel::splitCue(const QString &cueId, const QString &atMs, int textPosition)
{
    const int index = indexOf(m_cues, cueId);
    qint64 at = 0;
    if (index < 0) return rejectEdit(QStringLiteral("No cue %1 to split.").arg(cueId));
    const CaptionCueRecord original = m_cues.at(index);
    if (!signedMilliseconds(atMs, &at) || at <= original.inMs || at >= original.outMs)
        return rejectEdit(QStringLiteral("A split time must be an exact millisecond strictly inside the cue."));
    const QString text = original.text;
    int cut = textPosition;
    if (cut < 0 || cut > text.size()) {
        const int wanted = int(qint64(text.size()) * (at - original.inMs) / (original.outMs - original.inMs));
        cut = nearestWhitespace(text, wanted);
    }
    CaptionCueRecord left = original;
    CaptionCueRecord right = original;
    left.outMs = at;
    right.inMs = at;
    right.id = allocateId();
    if (cut >= 0) {
        left.text = text.left(cut).trimmed();
        right.text = text.mid(cut).trimmed();
    }
    QVector<CaptionCueRecord> next = m_cues;
    next[index] = left;
    next.append(right);
    acceptEdit(next, right.id, QStringLiteral("Cue split into %1 and %2.").arg(left.id, right.id));
    return true;
}

bool CaptionTrackModel::mergeCues(const QString &firstCueId, const QString &secondCueId)
{
    const int a = indexOf(m_cues, firstCueId);
    const int b = indexOf(m_cues, secondCueId);
    if (a < 0 || b < 0 || a == b) return rejectEdit(QStringLiteral("Merging needs two different existing cues."));
    const CaptionCueRecord &first = m_cues.at(a);
    const CaptionCueRecord &second = m_cues.at(b);
    if (first.language != second.language)
        return rejectEdit(QStringLiteral("Cues in different languages (%1, %2) cannot be merged.")
                              .arg(first.language, second.language));
    const bool firstEarlier = first.inMs < second.inMs || (first.inMs == second.inMs && first.id < second.id);
    CaptionCueRecord merged = first;
    merged.inMs = std::min(first.inMs, second.inMs);
    merged.outMs = std::max(first.outMs, second.outMs);
    merged.text = firstEarlier ? first.text + QLatin1Char('\n') + second.text
                               : second.text + QLatin1Char('\n') + first.text;
    QVector<CaptionCueRecord> next;
    for (qsizetype i = 0; i < m_cues.size(); ++i) {
        if (i == a || i == b) continue;
        const CaptionCueRecord &other = m_cues.at(i);
        if (other.lane == merged.lane && overlaps(other, merged))
            return rejectEdit(QStringLiteral("The merged cue would overlap %1 in its lane.").arg(other.id));
        next.append(other);
    }
    next.append(merged);
    acceptEdit(next, merged.id, QStringLiteral("Cues merged into %1.").arg(merged.id));
    return true;
}

bool CaptionTrackModel::shiftCues(const QStringList &cueIds, const QString &deltaMs)
{
    qint64 delta = 0;
    if (!signedMilliseconds(deltaMs, &delta) || delta == 0)
        return rejectEdit(QStringLiteral("A shift needs a nonzero signed decimal millisecond delta."));
    const QSet<QString> moving(cueIds.cbegin(), cueIds.cend());
    for (const QString &id : cueIds)
        if (indexOf(m_cues, id) < 0) return rejectEdit(QStringLiteral("No cue %1 to shift.").arg(id));
    QVector<CaptionCueRecord> next = m_cues;
    for (CaptionCueRecord &cue : next) {
        if (!moving.isEmpty() && !moving.contains(cue.id)) continue;
        if ((delta < 0 && cue.inMs < -delta)
            || (delta > 0 && cue.outMs > std::numeric_limits<qint64>::max() - delta))
            return rejectEdit(QStringLiteral("Shifting would move %1 outside the representable time range.").arg(cue.id));
        cue.inMs += delta;
        cue.outMs += delta;
    }
    for (qsizetype i = 0; i < next.size(); ++i) {
        if (!moving.isEmpty() && !moving.contains(next.at(i).id)) continue;
        for (qsizetype j = 0; j < next.size(); ++j) {
            if (i == j || (moving.isEmpty() || moving.contains(next.at(j).id))) continue;
            if (next.at(i).lane == next.at(j).lane && overlaps(next.at(i), next.at(j)))
                return rejectEdit(QStringLiteral("Shifted %1 would overlap %2 in its lane.")
                                      .arg(next.at(i).id, next.at(j).id));
        }
    }
    const QString selected = moving.isEmpty() || moving.contains(m_selectedId) ? m_selectedId : cueIds.first();
    acceptEdit(next, selected, QStringLiteral("%1 cue(s) shifted by %2 ms.")
                                   .arg(moving.isEmpty() ? m_cues.size() : moving.size()).arg(delta));
    return true;
}

bool CaptionTrackModel::importEmbedded(const QString &mediaPath)
{
    const QindaTK::MediaCaptionTrack track = QindaTK::MediaCaptions::readEmbedded(mediaPath);
    if (track.error) return rejectEdit(track.error.message);
    applyImported(track.cues, track.language);
    return true;
}

QVariantList CaptionTrackModel::warnings() const
{
    QVariantList result;
    const auto add = [&result](const QString &id, const QString &kind, const QString &message) {
        result.append(QVariantMap{{QStringLiteral("cueId"), id}, {QStringLiteral("kind"), kind},
                                  {QStringLiteral("message"), message}});
    };
    // m_cues is sorted by in time, so only later cues starting before this
    // one ends can overlap it; each pair warns on both cues.
    for (qsizetype i = 0; i < m_cues.size(); ++i) {
        const CaptionCueRecord &cue = m_cues.at(i);
        for (qsizetype j = i + 1; j < m_cues.size() && m_cues.at(j).inMs < cue.outMs; ++j) {
            const CaptionCueRecord &other = m_cues.at(j);
            if (!overlaps(cue, other)) continue;
            add(cue.id, QStringLiteral("overlap"),
                QStringLiteral("Shown together with %1 (lane %2).").arg(other.id).arg(other.lane));
            add(other.id, QStringLiteral("overlap"),
                QStringLiteral("Shown together with %1 (lane %2).").arg(cue.id).arg(cue.lane));
        }
    }
    for (qsizetype i = 0; i < m_cues.size(); ++i) {
        const CaptionCueRecord &cue = m_cues.at(i);
        const qint64 duration = cue.outMs - cue.inMs;
        if (duration < kMinimumDurationMs)
            add(cue.id, QStringLiteral("short"),
                QStringLiteral("Shown for %1 ms, under the %2 ms minimum.").arg(duration).arg(kMinimumDurationMs));
        const qint64 characters = visibleCharacters(cue.text);
        if (characters * 1000 > qint64(kMaximumCharactersPerSecond) * duration)
            add(cue.id, QStringLiteral("reading-rate"),
                QStringLiteral("Reading rate %1 characters per second exceeds %2.")
                    .arg(QString::number(double(characters) * 1000.0 / double(duration), 'f', 1))
                    .arg(kMaximumCharactersPerSecond));
        for (const QString &line : cue.text.split(QLatin1Char('\n'))) {
            if (line.size() > kMaximumLineLength) {
                add(cue.id, QStringLiteral("line-length"),
                    QStringLiteral("A line has %1 characters, over %2.").arg(line.size()).arg(kMaximumLineLength));
                break;
            }
        }
    }
    return result;
}

QString CaptionTrackModel::warningText() const
{
    QStringList messages;
    for (const QVariant &value : warnings()) {
        const QVariantMap warning = value.toMap();
        if (warning.value(QStringLiteral("cueId")).toString() == m_selectedId)
            messages << warning.value(QStringLiteral("message")).toString();
    }
    if (!m_error.isEmpty()) messages.prepend(m_error);
    return messages.join(QStringLiteral(" · "));
}

QVariantList CaptionTrackModel::formats() const
{
    const auto format = [](const QString &id, const QString &kind, const QString &direction,
                           const QStringList &carries, const QString &note) {
        return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("kind"), kind},
                           {QStringLiteral("direction"), direction}, {QStringLiteral("carries"), carries},
                           {QStringLiteral("note"), note}};
    };
    return {
        format(QStringLiteral("json"), QStringLiteral("native"), QStringLiteral("import-export"),
               {QStringLiteral("timing"), QStringLiteral("text"), QStringLiteral("language"),
                QStringLiteral("style"), QStringLiteral("lane"), QStringLiteral("id")},
               QStringLiteral("Canonical track document; exact decimal milliseconds.")),
        format(QStringLiteral("webvtt"), QStringLiteral("sidecar"), QStringLiteral("import-export"),
               {QStringLiteral("timing"), QStringLiteral("text"), QStringLiteral("language"), QStringLiteral("id")},
               QStringLiteral("Language: header plus <lang> spans; styles and lanes are not carried.")),
        format(QStringLiteral("srt"), QStringLiteral("sidecar"), QStringLiteral("import-export"),
               {QStringLiteral("timing"), QStringLiteral("text")},
               QStringLiteral("No language field; the host names the language on import.")),
        format(QStringLiteral("matroska-subrip"), QStringLiteral("embedded"), QStringLiteral("import-export"),
               {QStringLiteral("timing"), QStringLiteral("text"), QStringLiteral("language")},
               QStringLiteral("A SubRip stream in exported Matroska; the stream language is the track's BCP 47 tag.")),
        format(QStringLiteral("burned-in"), QStringLiteral("render"), QStringLiteral("export"),
               {QStringLiteral("timing"), QStringLiteral("text"), QStringLiteral("style")},
               QStringLiteral("Pixels in program frames and PNG sequences; not accessible text."))};
}
