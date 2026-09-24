// SPDX-License-Identifier: LGPL-3.0-or-later
// Page identity, revisions, list numbering and page lookup by paragraph.
#include "rich_text_layout_p.h"
#include "rich_text_spans.h"

#include <QHashFunctions>

namespace QindaTK {
using namespace RichText;

namespace {
// Namespace of the deterministic page keys (UUIDv5 of section + ordinal).
const QUuid PageKeyNamespace(QStringLiteral("{6b1f3c52-8d6e-4c0a-9a57-1f5e0b8a4d20}"));
} // namespace

bool RichTextLayout::rebuildMarkers() const
{
    QHash<QUuid, QString> markers;
    if (m_document) {
        QHash<QString, QVector<int>> counters;
        for (const Paragraph *p : m_document->paragraphsInOrder()) {
            const ListDefinition *list = m_document->list(p->format.listId);
            if (!list || list->levels.isEmpty()) continue;
            const int level = qBound(0, p->format.listLevel, int(list->levels.size()) - 1);
            QVector<int> &counter = counters[list->id];
            if (counter.isEmpty()) {
                counter.resize(list->levels.size());
                for (int i = 0; i < list->levels.size(); ++i) counter[i] = list->levels.at(i).start - 1;
            }
            ++counter[level];
            for (int deeper = level + 1; deeper < counter.size(); ++deeper)
                counter[deeper] = list->levels.at(deeper).start - 1;
            markers.insert(p->id, listMarkerText(list->levels.at(level).kind, counter.at(level)));
        }
    }
    const bool changed = markers != m_markers;
    m_markers = markers;
    m_markersDirty = false;
    m_markersChanged = m_markersChanged || changed;
    return changed;
}

QString RichTextLayout::listMarker(const QUuid &paragraph) const
{
    if (m_markersDirty) rebuildMarkers();
    return m_markers.value(paragraph);
}

static size_t hashItems(const RichTextDocument &doc, const Page &page, const QHash<QUuid, QString> &markers)
{
    size_t seed = 0;
    for (const PageItem &item : page.items) {
        seed = qHashMulti(seed, int(item.kind), item.block, item.first, item.count,
                          qRound64(item.y * 64), qRound64(item.height * 64), item.cellFirstLine,
                          qRound64(item.rowClip * 64), item.objectId, item.headerRepeat);
        if (const Paragraph *p = doc.paragraph(item.block))
            seed = qHashMulti(seed, p->revision, markers.value(p->id));
        else if (const Table *t = doc.table(item.block))
            seed = qHashMulti(seed, t->revision);
    }
    return seed;
}

void RichTextLayout::pageRange(Page &page)
{
    page.first = page.last = Position{};
    for (const PageItem &item : page.items) {
        if (item.kind == PageItem::Lines) {
            const Paragraph *p = m_document->paragraph(item.block);
            const ParagraphMetrics &m = ensureParagraph(*p, itemWidth(), item.exclusions);
            const LineBox &firstLine = m.lines.at(item.first);
            const LineBox &lastLine = m.lines.at(item.first + item.count - 1);
            if (!page.first.isValid()) page.first = {p->id, modelOffset(m, firstLine.start)};
            page.last = {p->id, modelOffset(m, lastLine.start + lastLine.length)};
        } else if (item.kind == PageItem::TableRows && !item.headerRepeat) {
            const Table *t = m_document->table(item.block);
            const TableRow &firstRow = t->rows.at(item.first);
            const TableRow &lastRow = t->rows.at(item.first + item.count - 1);
            if (!page.first.isValid()) page.first = {firstRow.cells.first().paragraphs.first()->id, 0};
            const Paragraph *tail = lastRow.cells.last().paragraphs.last().get();
            page.last = {tail->id, int(tail->text.size())};
        }
    }
}

void RichTextLayout::finalizePages(int firstChanged, int reusedFrom)
{
    if (!m_document) return;
    if (m_markersDirty) rebuildMarkers();
    const bool markersChanged = m_markersChanged;
    m_markersChanged = false;
    const int start = markersChanged ? 0 : qBound(0, firstChanged, pageCount());
    const auto &blocks = m_document->blocks();
    QUuid section = start > 0 ? m_pages.at(start - 1).section : QUuid();
    int ordinal = start > 0 ? m_pages.at(start - 1).ordinal : -1;
    for (int i = start; i < m_pages.size(); ++i) {
        Page &page = m_pages[i];
        const FlowState &s = page.start;
        const Paragraph *startParagraph = s.block < blocks.size() ? blocks.at(s.block).paragraph.get() : nullptr;
        const bool newSection = i == 0 || (s.line == 0 && startParagraph && s.block > 0
                                           && startParagraph->format.pageBreakBefore);
        const QUuid pageSection = newSection ? (i == 0 && !blocks.isEmpty() ? blocks.first().id() : s.blockId) : section;
        const int pageOrdinal = newSection ? 0 : ordinal + 1;
        section = pageSection;
        ordinal = pageOrdinal;
        const bool reused = reusedFrom >= 0 && i >= reusedFrom && !markersChanged;
        if (reused && page.section == pageSection && page.ordinal == pageOrdinal)
            break; // this and every later reused page is unchanged
        page.section = pageSection;
        page.ordinal = pageOrdinal;
        page.key = QUuid::createUuidV5(PageKeyNamespace, pageSection.toRfc4122() + QByteArray::number(pageOrdinal));
        if (!reused) {
            pageRange(page);
            page.contentHash = hashItems(*m_document, page, m_markers);
            const auto previous = m_revisions.constFind(page.key);
            page.revision = previous != m_revisions.constEnd() && previous->first == page.contentHash
                ? previous->second : ++m_revisionCounter;
        }
        m_revisions.insert(page.key, {page.contentHash, page.revision});
    }
    if (m_revisions.size() > 2 * m_pages.size() + 1024) {
        m_revisions.clear();
        for (const Page &page : std::as_const(m_pages)) m_revisions.insert(page.key, {page.contentHash, page.revision});
    }
    m_paginatedBlocks = blockIds();
    if (m_mode == Mode::Continuous) {
        qreal bottom = 0;
        for (const Page &page : std::as_const(m_pages))
            for (const PageItem &item : page.items) bottom = qMax(bottom, item.y + item.height);
        m_flowHeight = bottom;
    }
    m_stats.shapedLayoutsCached = m_shapedCount;
    m_stats.metricsCached = int(m_metrics.size() + m_contextMetrics.size());
    emit pagesChanged(qMin(start, pageCount()), pageCount());
}

QVector<int> RichTextLayout::pagesContaining(const Paragraph *p) const
{
    // Pages are in document order and carry their first/last positions, so a
    // binary search on the paragraph's order finds its pages without an index.
    QVector<int> result;
    if (!m_document || !p) return result;
    m_document->paragraphsInOrder(); // refreshes flatIndex after structural edits
    auto flatOf = [this](const Position &position) {
        const Paragraph *q = m_document->paragraph(position.paragraph);
        return q ? q->flatIndex : -1;
    };
    int low = 0;
    int high = pageCount();
    while (low < high) {
        const int mid = (low + high) / 2;
        if (flatOf(m_pages.at(mid).last) < p->flatIndex) low = mid + 1;
        else high = mid;
    }
    for (int i = low; i < pageCount() && flatOf(m_pages.at(i).first) <= p->flatIndex; ++i) result.append(i);
    return result;
}

} // namespace QindaTK
