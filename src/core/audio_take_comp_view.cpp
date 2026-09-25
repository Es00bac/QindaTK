// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioTakeCompModel::viewState: the TakeCompView snapshot of a comp (WP-065,
// docs/audio-comping.md). Exact decimal sample strings throughout.
#include "audio_take_comp_model.h"

#include <QVariantList>
#include <algorithm>
#include <cmath>
#include <set>

namespace QindaTK {
namespace {

constexpr qint64 kPeakReads = 4096; // samples read per peak column at most

QString text(qint64 value)
{
    return QString::number(value);
}

// Max |sample| over every channel of the take's source for `columns` equal
// parts of [0, length) in comp samples; outside the take's range reads 0.
QVariantList peaks(const AudioTake &take, const AudioBufferPtr &source, qint64 length, int columns)
{
    QVariantList out;
    if (!source || columns < 1) return out;
    const qint64 first = std::max<qint64>(take.sourceStart, 0);
    const qint64 last = std::min<qint64>(take.sourceEnd, source->frames);
    for (int column = 0; column < columns; ++column) {
        const qint64 from = std::max(first, take.sourceOffset + length * column / columns);
        const qint64 to = std::min(last, take.sourceOffset + length * (column + 1) / columns);
        const qint64 stride = std::max<qint64>(1, (to - from + kPeakReads - 1) / kPeakReads);
        float peak = 0.0f;
        for (qint64 frame = from; frame < to; frame += stride)
            for (int channel = 0; channel < source->channels; ++channel)
                peak = std::max(peak, std::abs(source->channel(channel)[frame]));
        out.push_back(double(std::min(peak, 1.0f)));
    }
    return out;
}

} // namespace

QVariantMap AudioTakeCompModel::viewState(const QString &compId, int columns, int peakCount) const
{
    const AudioTakeComp *committed = comp(compId);
    if (!committed) return {};
    // A swipe in progress shows its preview, which is also what plays.
    const AudioTakeComp &content = m_preview.active && m_preview.comp.id == compId ? m_preview.comp : *committed;
    columns = std::clamp(columns, 1, 8192);
    peakCount = std::clamp(peakCount, 0, 4096);

    // AGENT-GUARD: the boundary table comes from the committed comp only, so
    // it stays the same while a swipe previews: the view's gesture holds
    // indices into it. Preview segment edges are already in it (they are the
    // swipe's own boundaries or committed edges); a preview fade edge that is
    // not in it is left out of the snapshot rather than moving the table.
    std::set<qint64> boundaries{0, committed->length};
    for (int i = 1; i < columns; ++i) boundaries.insert(committed->length * i / columns);
    for (const AudioCompPiece &piece : AudioComp::resolve(*committed)) {
        boundaries.insert(piece.start);
        boundaries.insert(piece.end);
        if (piece.fadeInEnd > piece.fadeInStart) boundaries.insert(piece.fadeInEnd);
        if (piece.fadeOutEnd > piece.fadeOutStart) boundaries.insert(piece.fadeOutStart);
    }
    QVariantList segments;
    for (const AudioCompPiece &piece : AudioComp::resolve(content)) {
        const AudioTake *take = content.take(piece.takeId);
        QVariantMap segment{{QStringLiteral("segmentId"), piece.segmentId},
                            {QStringLiteral("takeId"), piece.takeId},
                            {QStringLiteral("startSample"), text(piece.start)},
                            {QStringLiteral("endSample"), text(piece.end)},
                            {QStringLiteral("sourceOffsetSample"), text(take ? take->sourceOffset : 0)}};
        // The view draws fades inside the segment: the crossfade's end on the
        // right side of a junction and its start on the left side.
        if (piece.fadeInEnd > piece.fadeInStart && boundaries.count(piece.fadeInEnd))
            segment.insert(QStringLiteral("fadeInEndSample"), text(piece.fadeInEnd));
        if (piece.fadeOutEnd > piece.fadeOutStart && boundaries.count(piece.fadeOutStart))
            segment.insert(QStringLiteral("fadeOutStartSample"), text(piece.fadeOutStart));
        segments.push_back(segment);
    }
    QVariantList takes;
    for (const AudioTake &take : content.takes) {
        const AudioBufferPtr source = m_sources ? m_sources(take.resourceId) : nullptr;
        takes.push_back(QVariantMap{{QStringLiteral("takeId"), take.id},
                                    {QStringLiteral("sourceId"), take.resourceId},
                                    {QStringLiteral("label"), take.label.isEmpty() ? take.id : take.label},
                                    {QStringLiteral("sourceOffsetSample"), text(take.sourceOffset)},
                                    {QStringLiteral("coverStartSample"), text(std::max<qint64>(0, take.compStart()))},
                                    {QStringLiteral("coverEndSample"), text(std::min(content.length, take.compEnd()))},
                                    {QStringLiteral("available"), bool(source)},
                                    {QStringLiteral("peaks"), peaks(take, source, content.length, peakCount)}});
    }
    QStringList exact;
    for (qint64 boundary : boundaries) exact.push_back(text(boundary));
    return {{QStringLiteral("compId"), content.id},
            {QStringLiteral("sampleRate"), content.sampleRate},
            {QStringLiteral("lengthSample"), text(content.length)},
            {QStringLiteral("fadeSamples"), text(content.fadeSamples)},
            {QStringLiteral("fadeShape"), AudioComp::fadeName(content.fadeShape)},
            {QStringLiteral("audition"), audition(compId)},
            {QStringLiteral("previewing"), m_preview.active && m_preview.comp.id == compId},
            {QStringLiteral("takes"), takes},
            {QStringLiteral("compSegments"), segments},
            {QStringLiteral("sampleBoundaries"), exact}};
}

} // namespace QindaTK
