// SPDX-License-Identifier: LGPL-3.0-or-later
// Thread tracks, lanes and aggregated window layout for TraceView.
#include "trace_index.h"

#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <qnumeric.h>

namespace QindaTK {
namespace {
constexpr int MaxLanesPerThread = 32;
constexpr int MaxColumns = 8192;
constexpr int MaxRows = 256;

// AGENT-GUARD: layoutWindow draws, merges and recordAt hit-tests with this
// one extent, so clicking any drawn pixel of a mark finds its record
// (tst_trace_view large-trace case). Columns [c0, c1) come from the real,
// clipped times; the drawn x0/x1 stay inside them. A mark narrower than a
// pixel fills its first column instead of spilling into the next one, which
// would either overlap the next mark or chain every mark in a dense row into
// one merge.
struct PixelSpan {
    double x0 = 0, x1 = 0;  // drawn window fractions, clipped
    int c0 = 0, c1 = 0;     // pixel columns [c0, c1)
};

PixelSpan pixelSpan(double a, double b, int columns)
{
    PixelSpan span;
    const double from = std::max(0.0, a), to = std::min(1.0, b);
    span.c0 = std::min(columns - 1, static_cast<int>(std::floor(from * columns)));
    span.c1 = std::max(span.c0 + 1, static_cast<int>(std::ceil(to * columns)));
    const bool narrow = (to - from) * columns < 1.0;
    span.x0 = narrow ? static_cast<double>(span.c0) / columns : from;
    span.x1 = std::max(to, static_cast<double>(span.c0 + 1) / columns);
    return span;
}
}

// AGENT-NOTE (EXT-11): each thread is a track of lanes. A record takes the
// first lane free at its start, and never a lane above (numerically at or
// below) its same-thread parent, so nesting reads as a flame chart and marks
// in one lane never overlap in time. Lane 31 is the overflow lane: records
// past it share it and the layout merges them. Runs off-thread in analyze().
bool TraceIndex::assignLanes(Analysis *result, const CancelFlag &cancel)
{
    QVector<Record> &records = result->records;
    QHash<QString, int> byId;
    QHash<QString, QVector<int>> byThread;
    for (int i = 0; i < records.size(); ++i) {
        byId.insert(records[i].id, i);
        byThread[records[i].threadId].append(i);
    }
    int visited = 0;
    for (auto it = byThread.begin(); it != byThread.end(); ++it) {
        QVector<int> &rows = it.value();
        // Records are sorted by (start, id); longer first at equal starts
        // puts a parent before a child that starts with it.
        std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
            if (records[a].startNs != records[b].startNs)
                return records[a].startNs < records[b].startNs;
            return records[a].endNs > records[b].endNs;
        });
        QVector<qint64> laneEnd;
        QSet<int> placed;
        for (int row : std::as_const(rows)) {
            if ((++visited & 1023) == 0 && cancel->load()) return false;
            Record &r = records[row];
            int minLane = 0;
            const auto parent = byId.constFind(r.parentId);
            if (parent != byId.cend() && placed.contains(*parent)
                && records[*parent].threadId == r.threadId)
                minLane = std::min(records[*parent].lane + 1, MaxLanesPerThread - 1);
            int lane = -1;
            for (int l = minLane; l < laneEnd.size(); ++l)
                if (laneEnd[l] <= r.startNs) { lane = l; break; }
            if (lane < 0) {
                while (laneEnd.size() < minLane && laneEnd.size() < MaxLanesPerThread)
                    laneEnd.append(std::numeric_limits<qint64>::min());
                lane = laneEnd.size() < MaxLanesPerThread ? laneEnd.size()
                                                          : MaxLanesPerThread - 1;
                if (lane == laneEnd.size()) laneEnd.append(r.endNs);
            }
            laneEnd[lane] = std::max(laneEnd[lane], r.endNs);
            r.lane = lane;
            placed.insert(row);
        }
        result->lanes.insert(it.key(), std::max<int>(1, laneEnd.size()));
    }
    return true;
}

QVariantList TraceIndex::tracks(const QString &threadId) const
{
    QVariantList out;
    int row = 0;
    for (const QString &thread : m_threads) {
        if (!threadId.isEmpty() && thread != threadId) continue;
        const int lanes = m_lanes.value(thread, 1);
        out.append(QVariantMap{{QStringLiteral("threadId"), thread},
                               {QStringLiteral("firstRow"), row},
                               {QStringLiteral("lanes"), lanes}});
        row += lanes;
    }
    return out;
}

// AGENT-CONTRACT (EXT-11, TraceView.qml): the view draws only these marks,
// one canvas, no per-record items. Every record in the window and in rows
// [firstRow, firstRow + rowCount) is in exactly one mark; marks in a row
// never share a pixel column, so a row holds at most `columns` marks. A mark
// with count > 1 merges records that would share a pixel; its id is the
// longest of them. x0/x1 are fractions of the window, clipped to [0, 1].
QVariantMap TraceIndex::layoutWindow(double fromFraction, double toFraction, int columns,
                                     const QString &threadId, const QString &textFilter,
                                     int firstRow, int rowCount) const
{
    QVariantMap out;
    QHash<QString, int> trackRow;
    int rows = 0;
    for (const QString &thread : m_threads) {
        if (!threadId.isEmpty() && thread != threadId) continue;
        trackRow.insert(thread, rows);
        rows += m_lanes.value(thread, 1);
    }
    out.insert(QStringLiteral("rows"), rows);
    if (m_records.isEmpty() || !qIsFinite(fromFraction) || !qIsFinite(toFraction)
        || toFraction <= fromFraction) {
        out.insert(QStringLiteral("tiles"), QVariantList{});
        out.insert(QStringLiteral("matched"), 0);
        return out;
    }
    columns = std::clamp(columns, 1, MaxColumns);
    firstRow = std::max(0, firstRow);
    rowCount = std::clamp(rowCount, 1, MaxRows);
    const double total = std::max(1.0, static_cast<double>(static_cast<quint64>(m_end)
                                                           - static_cast<quint64>(m_start)));
    const double from = fromFraction * total, span = (toFraction - fromFraction) * total;
    struct Tile {
        int c1 = 0;
        double x0 = 0, x1 = 0;
        int count = 0;
        int representative = -1;
        bool mixed = false;
    };
    QVector<Tile> open(rowCount);
    QVariantList tiles;
    auto flush = [&](int slot) {
        Tile &t = open[slot];
        if (t.count == 0) return;
        const Record &r = m_records.at(t.representative);
        tiles.append(QVariantMap{{QStringLiteral("row"), firstRow + slot},
            {QStringLiteral("x0"), t.x0}, {QStringLiteral("x1"), t.x1},
            {QStringLiteral("count"), t.count}, {QStringLiteral("id"), r.id},
            {QStringLiteral("kind"), t.mixed ? QStringLiteral("mixed") : r.kind},
            {QStringLiteral("name"), r.name}, {QStringLiteral("threadId"), r.threadId}});
        t = Tile{};
    };
    int matched = 0, drawn = 0;
    for (int i = 0; i < m_records.size(); ++i) {
        const Record &r = m_records.at(i);
        const double a = (static_cast<double>(static_cast<quint64>(r.startNs)
                                              - static_cast<quint64>(m_start)) - from) / span;
        if (a > 1.0) break;  // sorted by start: nothing later is in the window
        const double b = (static_cast<double>(static_cast<quint64>(r.endNs)
                                              - static_cast<quint64>(m_start)) - from) / span;
        if (b < 0.0 || !trackRow.contains(r.threadId)) continue;
        if (!textFilter.isEmpty() && !r.name.contains(textFilter, Qt::CaseInsensitive)
            && !r.category.contains(textFilter, Qt::CaseInsensitive)) continue;
        ++matched;
        const int slot = trackRow.value(r.threadId) + r.lane - firstRow;
        if (slot < 0 || slot >= rowCount) continue;
        ++drawn;
        // Clip to the window before measuring, so a span that started
        // before the window is drawn from the left edge to its real end.
        const PixelSpan px = pixelSpan(a, b, columns);
        Tile &t = open[slot];
        if (t.count > 0 && px.c0 < t.c1) {
            ++t.count;
            t.c1 = std::max(t.c1, px.c1);
            t.x1 = std::max(t.x1, px.x1);
            t.mixed = t.mixed || m_records.at(t.representative).kind != r.kind;
            const Record &best = m_records.at(t.representative);
            if (static_cast<quint64>(r.endNs) - static_cast<quint64>(r.startNs)
                > static_cast<quint64>(best.endNs) - static_cast<quint64>(best.startNs))
                t.representative = i;
            continue;
        }
        flush(slot);
        t.c1 = px.c1;
        t.x0 = px.x0;
        t.x1 = px.x1;
        t.count = 1;
        t.representative = i;
    }
    for (int slot = 0; slot < rowCount; ++slot) flush(slot);
    out.insert(QStringLiteral("tiles"), tiles);
    out.insert(QStringLiteral("matched"), matched);
    out.insert(QStringLiteral("drawn"), drawn);
    return out;
}

QString TraceIndex::recordAt(double fromFraction, double toFraction, int columns, int row,
                             int column, const QString &threadId,
                             const QString &textFilter) const
{
    if (m_records.isEmpty() || !qIsFinite(fromFraction) || !qIsFinite(toFraction)
        || toFraction <= fromFraction || columns < 1 || column < 0 || column >= columns)
        return {};
    int first = 0;
    QString thread;
    int lane = -1;
    for (const QString &candidate : m_threads) {
        if (!threadId.isEmpty() && candidate != threadId) continue;
        const int lanes = m_lanes.value(candidate, 1);
        if (row >= first && row < first + lanes) { thread = candidate; lane = row - first; break; }
        first += lanes;
    }
    if (lane < 0) return {};
    const double total = std::max(1.0, static_cast<double>(static_cast<quint64>(m_end)
                                                           - static_cast<quint64>(m_start)));
    const double from = fromFraction * total, span = (toFraction - fromFraction) * total;
    int best = -1;
    for (int i = 0; i < m_records.size(); ++i) {
        const Record &r = m_records.at(i);
        const double a = (static_cast<double>(static_cast<quint64>(r.startNs)
                                              - static_cast<quint64>(m_start)) - from) / span;
        if (a > 1.0) break;
        if (r.threadId != thread || r.lane != lane) continue;
        const double b = (static_cast<double>(static_cast<quint64>(r.endNs)
                                              - static_cast<quint64>(m_start)) - from) / span;
        if (b < 0.0) continue;
        const PixelSpan px = pixelSpan(a, b, columns);
        if (column < px.c0 || column >= px.c1) continue;
        if (!textFilter.isEmpty() && !r.name.contains(textFilter, Qt::CaseInsensitive)
            && !r.category.contains(textFilter, Qt::CaseInsensitive)) continue;
        if (best < 0 || static_cast<quint64>(r.endNs) - static_cast<quint64>(r.startNs)
                > static_cast<quint64>(m_records[best].endNs)
                  - static_cast<quint64>(m_records[best].startNs))
            best = i;
    }
    return best < 0 ? QString{} : m_records.at(best).id;
}

} // namespace QindaTK
