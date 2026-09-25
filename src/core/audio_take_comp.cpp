// SPDX-License-Identifier: LGPL-3.0-or-later
// Take comps (WP-065, docs/audio-comping.md): validation, the comp edits,
// the crossfade policy and the persistence codec. Pure functions over plain
// data; the model (audio_take_comp_model.cpp) adds history and previews.
#include "audio_take_comp.h"

#include <QCoreApplication>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace QindaTK {
namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("AudioTakeComp", text);
}

std::optional<qint64> integer(const QVariant &value)
{
    if (value.typeId() == QMetaType::QString) {
        bool ok = false;
        const qint64 parsed = value.toString().toLongLong(&ok, 10);
        return ok ? std::optional<qint64>(parsed) : std::nullopt;
    }
    if (!value.canConvert<qint64>()) return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || std::abs(number) > 9.0e15) return std::nullopt;
    return value.toLongLong();
}

QString text(qint64 value)
{
    return QString::number(value);
}

void sortSegments(QVector<AudioCompSegment> &segments)
{
    std::sort(segments.begin(), segments.end(),
              [](const AudioCompSegment &a, const AudioCompSegment &b) { return a.start < b.start; });
}

// Removes [start, end) from the segments. A segment cut in two keeps its ID
// on the left part; the right part gets a new ID. Returns whether any
// segment lost samples.
bool cut(QVector<AudioCompSegment> &segments, qint64 start, qint64 end, QString *exact)
{
    QVector<AudioCompSegment> out;
    bool changed = false;
    for (const AudioCompSegment &segment : std::as_const(segments)) {
        if (segment.end <= start || segment.start >= end) {
            out.push_back(segment);
            continue;
        }
        changed = true;
        if (exact && segment.start == start && segment.end == end) *exact = segment.id;
        bool left = false;
        if (segment.start < start) {
            AudioCompSegment part = segment;
            part.end = start;
            out.push_back(part);
            left = true;
        }
        if (segment.end > end) {
            AudioCompSegment part = segment;
            part.start = end;
            if (left) part.id = AudioComp::newId("segment");
            out.push_back(part);
        }
    }
    segments = out;
    return changed;
}

} // namespace

const AudioTake *AudioTakeComp::take(const QString &takeId) const
{
    for (const AudioTake &entry : takes)
        if (entry.id == takeId) return &entry;
    return nullptr;
}

namespace AudioComp {

QString newId(const char *prefix)
{
    return QString::fromLatin1(prefix) + QLatin1Char(':') + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString fadeName(AudioCompFade shape)
{
    return shape == AudioCompFade::Linear ? QStringLiteral("linear") : QStringLiteral("equalPower");
}

QString validate(const AudioTakeComp &comp)
{
    if (comp.id.isEmpty()) return tr("A comp needs an ID");
    if (comp.sampleRate < 1 || comp.sampleRate > 1000000) return tr("The comp sample rate is invalid");
    if (comp.length < 1 || comp.length > kMaximumLength) return tr("The comp length is invalid");
    if (comp.fadeSamples < 0 || comp.fadeSamples > comp.sampleRate)
        return tr("The crossfade must be between 0 samples and one second");
    QSet<QString> ids;
    for (const AudioTake &take : comp.takes) {
        if (take.id.isEmpty() || ids.contains(take.id)) return tr("Take IDs must be unique and not empty");
        ids.insert(take.id);
        if (take.resourceId.isEmpty()) return tr("Take %1 has no source").arg(take.id);
        if (take.sourceStart < 0 || take.sourceEnd <= take.sourceStart || take.sourceEnd > kMaximumLength
            || std::abs(take.sourceOffset) > kMaximumLength)
            return tr("Take %1 has an invalid source range").arg(take.id);
    }
    QSet<QString> segmentIds;
    qint64 previousEnd = 0;
    for (const AudioCompSegment &segment : comp.segments) {
        if (segment.id.isEmpty() || segmentIds.contains(segment.id)) return tr("Segment IDs must be unique and not empty");
        segmentIds.insert(segment.id);
        const AudioTake *take = comp.take(segment.takeId);
        if (!take) return tr("Segment %1 names no take of this comp").arg(segment.id);
        if (segment.start < previousEnd || segment.end <= segment.start || segment.end > comp.length)
            return tr("Segments must be sorted, disjoint and inside the comp");
        if (segment.start < take->compStart() || segment.end > take->compEnd())
            return tr("Take %1 has no audio for all of segment %2").arg(take->id, segment.id);
        previousEnd = segment.end;
    }
    return {};
}

QString select(AudioTakeComp &comp, const QString &takeId, qint64 start, qint64 end)
{
    const AudioTake *take = comp.take(takeId);
    if (!take) return tr("There is no take %1 in this comp").arg(takeId);
    if (start < 0 || end <= start || end > comp.length) return tr("The range must lie inside the comp");
    if (start < take->compStart() || end > take->compEnd())
        return tr("Take %1 has no audio for all of that range").arg(take->label.isEmpty() ? takeId : take->label);
    QVector<AudioCompSegment> segments = comp.segments;
    QString exact;
    cut(segments, start, end, &exact);
    segments.push_back({exact.isEmpty() ? newId("segment") : exact, takeId, start, end});
    sortSegments(segments);
    comp.segments = segments;
    return {};
}

QString split(AudioTakeComp &comp, qint64 at)
{
    for (qsizetype i = 0; i < comp.segments.size(); ++i) {
        AudioCompSegment &segment = comp.segments[i];
        if (at <= segment.start || at >= segment.end) continue;
        AudioCompSegment right = segment;
        right.id = newId("segment");
        right.start = at;
        segment.end = at;
        comp.segments.insert(i + 1, right);
        return {};
    }
    return tr("No segment can be split at sample %1").arg(at);
}

QString merge(AudioTakeComp &comp, const QString &segmentId)
{
    for (qsizetype i = 0; i + 1 < comp.segments.size(); ++i) {
        if (comp.segments.at(i).id != segmentId) continue;
        const AudioCompSegment left = comp.segments.at(i);
        const AudioCompSegment right = comp.segments.at(i + 1);
        if (left.end != right.start) return tr("The segment after %1 does not touch it").arg(segmentId);
        const AudioTake *take = comp.take(left.takeId);
        if (!take || right.end > take->compEnd())
            return tr("Take %1 has no audio for all of the merged range").arg(left.takeId);
        comp.segments[i].end = right.end;
        comp.segments.removeAt(i + 1);
        return {};
    }
    return tr("Segment %1 has no following segment to merge with").arg(segmentId);
}

QString clear(AudioTakeComp &comp, qint64 start, qint64 end)
{
    if (start < 0 || end <= start || end > comp.length) return tr("The range must lie inside the comp");
    QVector<AudioCompSegment> segments = comp.segments;
    if (!cut(segments, start, end, nullptr)) return tr("Nothing is selected in that range");
    comp.segments = segments;
    return {};
}

QString setFade(AudioTakeComp &comp, qint64 samples, AudioCompFade shape)
{
    if (samples < 0 || samples > comp.sampleRate) return tr("The crossfade must be between 0 samples and one second");
    comp.fadeSamples = samples;
    comp.fadeShape = shape;
    return {};
}

QString addTakes(AudioTakeComp &comp, const QVector<AudioTake> &takes)
{
    AudioTakeComp next = comp;
    next.takes += takes;
    if (const QString problem = validate(next); !problem.isEmpty()) return problem;
    comp = next;
    return {};
}

QVector<AudioCompPiece> resolve(const AudioTakeComp &comp, const QString &auditionTakeId, qint64 auditionStart,
                                qint64 auditionEnd)
{
    QVector<AudioCompPiece> pieces;
    const auto plain = [](const QString &segmentId, const QString &takeId, qint64 start, qint64 end) {
        AudioCompPiece piece;
        piece.segmentId = segmentId;
        piece.takeId = takeId;
        piece.start = piece.playStart = piece.fadeInStart = piece.fadeInEnd = start;
        piece.end = piece.playEnd = piece.fadeOutStart = piece.fadeOutEnd = end;
        return piece;
    };
    if (!auditionTakeId.isEmpty()) {
        // An audition range (end < 0: the whole comp) limits what is heard.
        const AudioTake *take = comp.take(auditionTakeId);
        const qint64 start = take ? std::max<qint64>({0, take->compStart(), auditionStart}) : 0;
        const qint64 end = take ? std::min({comp.length, take->compEnd(), auditionEnd < 0 ? comp.length : auditionEnd}) : 0;
        if (start < end) pieces.push_back(plain({}, auditionTakeId, start, end));
        return pieces;
    }
    for (const AudioCompSegment &segment : comp.segments)
        pieces.push_back(plain(segment.id, segment.takeId, segment.start, segment.end));
    // AGENT-NOTE (D-175): a junction b of two different takes A | B
    // crossfades over [b - x, b + y): x samples inside A, y inside B, centred
    // when it can be (x = ⌊F/2⌋). x never exceeds what B has before b nor half
    // of A's run; y never exceeds what A has after b nor half of B's run. A
    // run is the touching segments of one take around the junction: a split
    // or merge inside a take never changes a run, so it never changes a fade
    // (the fade regions are set on every piece of the run and weight only the
    // samples inside them). A side that lacks material moves the fade to the
    // other side, so the fade keeps its length when a take starts or ends
    // exactly at the junction. The half bounds keep both fades of one run
    // apart. A junction inside one take, a gap and the comp edges are cut
    // exactly.
    const auto touching = [&pieces](qsizetype k) {
        return pieces.at(k).end == pieces.at(k + 1).start && pieces.at(k).takeId == pieces.at(k + 1).takeId;
    };
    for (qsizetype i = 0; i + 1 < pieces.size(); ++i) {
        const AudioCompPiece &a = pieces.at(i);
        const AudioCompPiece &b = pieces.at(i + 1);
        if (a.end != b.start || a.takeId == b.takeId || comp.fadeSamples <= 0) continue;
        const AudioTake *left = comp.take(a.takeId);
        const AudioTake *right = comp.take(b.takeId);
        if (!left || !right) continue;
        qsizetype first = i;
        while (first > 0 && touching(first - 1)) --first;
        qsizetype last = i + 1;
        while (last + 1 < pieces.size() && touching(last)) ++last;
        const qint64 junction = a.end;
        const qint64 maxBefore = std::min(std::max<qint64>(0, junction - right->compStart()),
                                          (junction - pieces.at(first).start) / 2);
        const qint64 maxAfter = std::min(std::max<qint64>(0, left->compEnd() - junction),
                                         (pieces.at(last).end - junction) / 2);
        qint64 before = std::min(comp.fadeSamples / 2, maxBefore);
        const qint64 after = std::min(comp.fadeSamples - before, maxAfter);
        before = std::min(comp.fadeSamples - after, maxBefore);
        if (before + after <= 0) continue;
        for (qsizetype k = first; k <= i; ++k) {
            pieces[k].fadeOutStart = junction - before;
            pieces[k].fadeOutEnd = junction + after;
        }
        for (qsizetype k = i + 1; k <= last; ++k) {
            pieces[k].fadeInStart = junction - before;
            pieces[k].fadeInEnd = junction + after;
        }
        pieces[i].playEnd = junction + after;
        pieces[i + 1].playStart = junction - before;
    }
    return pieces;
}

AudioCompPlayback playback(const AudioTakeComp &comp, const QString &auditionTakeId, qint64 auditionStart,
                           qint64 auditionEnd)
{
    AudioCompPlayback result;
    result.valid = true;
    result.sampleRate = comp.sampleRate;
    result.length = comp.length;
    result.fadeShape = comp.fadeShape;
    for (const AudioCompPiece &piece : resolve(comp, auditionTakeId, auditionStart, auditionEnd)) {
        const AudioTake *take = comp.take(piece.takeId);
        if (!take) continue;
        result.parts.push_back({piece, take->resourceId, take->sourceOffset, take->sourceStart, take->sourceEnd});
    }
    return result;
}

double fadeWeight(AudioCompFade shape, bool in, qint64 s, qint64 a, qint64 b) noexcept
{
    if (b <= a || s < a || s >= b) return 1.0;
    const double t = (double(s - a) + 0.5) / double(b - a);
    if (shape == AudioCompFade::Linear) return in ? t : 1.0 - t;
    const double angle = t * std::numbers::pi / 2.0;
    return in ? std::sin(angle) : std::cos(angle);
}

QVariantMap toMap(const AudioTakeComp &comp)
{
    QVariantList takes;
    for (const AudioTake &take : comp.takes)
        takes.push_back(QVariantMap{{QStringLiteral("takeId"), take.id},
                                    {QStringLiteral("resourceId"), take.resourceId},
                                    {QStringLiteral("label"), take.label},
                                    {QStringLiteral("sourceOffset"), text(take.sourceOffset)},
                                    {QStringLiteral("sourceStart"), text(take.sourceStart)},
                                    {QStringLiteral("sourceEnd"), text(take.sourceEnd)}});
    QVariantList segments;
    for (const AudioCompSegment &segment : comp.segments)
        segments.push_back(QVariantMap{{QStringLiteral("segmentId"), segment.id},
                                       {QStringLiteral("takeId"), segment.takeId},
                                       {QStringLiteral("start"), text(segment.start)},
                                       {QStringLiteral("end"), text(segment.end)}});
    return {{QStringLiteral("compId"), comp.id},
            {QStringLiteral("sampleRate"), comp.sampleRate},
            {QStringLiteral("length"), text(comp.length)},
            {QStringLiteral("fadeSamples"), text(comp.fadeSamples)},
            {QStringLiteral("fadeShape"), fadeName(comp.fadeShape)},
            {QStringLiteral("takes"), takes},
            {QStringLiteral("segments"), segments}};
}

std::optional<AudioTakeComp> fromMap(const QVariantMap &map, QString *error)
{
    const auto refuse = [error](const QString &message) {
        if (error) *error = message;
        return std::optional<AudioTakeComp>();
    };
    AudioTakeComp comp;
    comp.id = map.value(QStringLiteral("compId")).toString();
    const auto rate = integer(map.value(QStringLiteral("sampleRate")));
    const auto length = integer(map.value(QStringLiteral("length")));
    const auto fade = integer(map.value(QStringLiteral("fadeSamples"), QStringLiteral("0")));
    const QString shape = map.value(QStringLiteral("fadeShape"), QStringLiteral("equalPower")).toString();
    if (!rate || !length || !fade || *rate > 1000000 || *rate < 1) return refuse(tr("Comp %1 has invalid numbers").arg(comp.id));
    if (shape != QLatin1String("linear") && shape != QLatin1String("equalPower"))
        return refuse(tr("Comp %1 has an unknown fade shape %2").arg(comp.id, shape));
    comp.sampleRate = int(*rate);
    comp.length = *length;
    comp.fadeSamples = *fade;
    comp.fadeShape = shape == QLatin1String("linear") ? AudioCompFade::Linear : AudioCompFade::EqualPower;
    for (const QVariant &value : map.value(QStringLiteral("takes")).toList()) {
        const QVariantMap entry = value.toMap();
        const auto offset = integer(entry.value(QStringLiteral("sourceOffset")));
        const auto start = integer(entry.value(QStringLiteral("sourceStart")));
        const auto end = integer(entry.value(QStringLiteral("sourceEnd")));
        if (!offset || !start || !end) return refuse(tr("A take of comp %1 has invalid numbers").arg(comp.id));
        comp.takes.push_back({entry.value(QStringLiteral("takeId")).toString(),
                              entry.value(QStringLiteral("resourceId")).toString(),
                              entry.value(QStringLiteral("label")).toString(), *offset, *start, *end});
    }
    for (const QVariant &value : map.value(QStringLiteral("segments")).toList()) {
        const QVariantMap entry = value.toMap();
        const auto start = integer(entry.value(QStringLiteral("start")));
        const auto end = integer(entry.value(QStringLiteral("end")));
        if (!start || !end) return refuse(tr("A segment of comp %1 has invalid numbers").arg(comp.id));
        comp.segments.push_back({entry.value(QStringLiteral("segmentId")).toString(),
                                 entry.value(QStringLiteral("takeId")).toString(), *start, *end});
    }
    if (const QString problem = validate(comp); !problem.isEmpty()) return refuse(problem);
    return comp;
}

QVariantMap pieceMap(const AudioCompPiece &piece)
{
    return {{QStringLiteral("segmentId"), piece.segmentId},
            {QStringLiteral("takeId"), piece.takeId},
            {QStringLiteral("startSample"), text(piece.start)},
            {QStringLiteral("endSample"), text(piece.end)},
            {QStringLiteral("playStartSample"), text(piece.playStart)},
            {QStringLiteral("playEndSample"), text(piece.playEnd)},
            {QStringLiteral("fadeInStartSample"), text(piece.fadeInStart)},
            {QStringLiteral("fadeInEndSample"), text(piece.fadeInEnd)},
            {QStringLiteral("fadeOutStartSample"), text(piece.fadeOutStart)},
            {QStringLiteral("fadeOutEndSample"), text(piece.fadeOutEnd)}};
}

} // namespace AudioComp
} // namespace QindaTK
