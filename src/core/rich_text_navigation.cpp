// SPDX-License-Identifier: LGPL-3.0-or-later
// Layout-aware caret movement: visual (bidi) steps, lines and line edges.
#include "rich_text_graphemes.h"
#include "rich_text_layout_p.h"

namespace QindaTK {
using namespace RichText;

Position RichTextLayout::visualMove(const Position &position, bool right)
{
    int lo = 0;
    const auto line = lineOf(position, &lo);
    if (!line) return position;
    QTextLayout *layout = shaped(*line->paragraph, *line->metrics);
    const QString &text = line->paragraph->text;
    // AGENT-NOTE: measured on Qt 6.11 — in a paragraph with bidi runs
    // QTextEngine::insertionPointsForLine offers every UTF-16 position (even
    // inside surrogate pairs), so keep stepping in the same visual direction
    // until the caret reaches a grapheme boundary (tst_rich_text_layout).
    const GraphemeBoundaries graphemes(text);
    int moved = lo;
    bool atEdge = false;
    for (int guard = 0; guard < 256; ++guard) {
        const int next = right ? layout->rightCursorPosition(moved) : layout->leftCursorPosition(moved);
        if (next == moved) {
            atEdge = true;
            break;
        }
        moved = next;
        if (graphemes.isBoundary(qBound(0, modelOffset(*line->metrics, moved), int(text.size())))) break;
    }
    if (!atEdge) {
        const int offset = qBound(0, modelOffset(*line->metrics, moved), int(text.size()));
        return {line->paragraph->id, offset, Affinity::Downstream};
    }
    // Visual edge of the paragraph: continue into the logical neighbour.
    const bool forward = right != line->metrics->rightToLeft;
    const Paragraph *next = forward ? m_document->nextParagraph(line->paragraph)
                                    : m_document->previousParagraph(line->paragraph);
    if (!next) return position;
    return {next->id, forward ? 0 : int(next->text.size())};
}

Position RichTextLayout::lineEdge(const Position &position, bool end)
{
    const auto line = lineOf(position);
    if (!line) return position;
    const LineBox &box = line->metrics->lines.at(line->line);
    const QString &text = line->paragraph->text;
    if (!end) return {line->paragraph->id, modelOffset(*line->metrics, box.start), Affinity::Downstream};
    int offset = modelOffset(*line->metrics, box.start + box.length);
    offset = qBound(0, offset, int(text.size()));
    const bool wrapped = line->line + 1 < line->metrics->lines.size();
    if (wrapped && offset > 0 && text.at(offset - 1) == QChar(LineBreakChar)) --offset;
    return {line->paragraph->id, offset, wrapped ? Affinity::Upstream : Affinity::Downstream};
}

Position RichTextLayout::lineMove(const Position &position, int direction, qreal *goalX)
{
    const auto current = lineOf(position);
    if (!current) return position;
    const CaretBox caret = caretBox(position);
    if (goalX && *goalX < 0) *goalX = caret.rect.x();
    const qreal x = goalX ? *goalX : caret.rect.x();
    // Nearest line box strictly above/below in the same column of this page
    // (boxes sharing x), then the next line in reading order (the next page
    // column or text frame), then the nearest edge line of the neighbouring
    // page; ties go to the closest column.
    auto pick = [&](const QVector<PageLine> &lines, bool sameLineBand) -> const PageLine * {
        const PageLine *best = nullptr;
        qreal bestEdge = 0;
        qreal bestDx = 0;
        for (const PageLine &line : lines) {
            if (line.paragraph == current->paragraph && line.line == current->line && line.metrics == current->metrics)
                continue;
            if (sameLineBand) {
                if (direction > 0 && line.box.top() < current->box.bottom() - 0.5) continue;
                if (direction < 0 && line.box.bottom() > current->box.top() + 0.5) continue;
                if (line.box.right() <= current->box.left() || line.box.left() >= current->box.right()) continue;
            }
            const qreal edge = direction > 0 ? line.box.top() : -line.box.bottom();
            const qreal dx = x < line.box.left() ? line.box.left() - x
                : x > line.box.right() ? x - line.box.right() : 0;
            if (!best || edge < bestEdge - 0.5 || (qAbs(edge - bestEdge) <= 0.5 && dx < bestDx)) {
                best = &line;
                bestEdge = edge;
                bestDx = dx;
            }
        }
        return best;
    };
    const QVector<PageLine> here = pageLines(current->page);
    if (const PageLine *target = pick(here, true)) return positionInLine(*target, x);
    for (int i = 0; i < here.size(); ++i) {
        const PageLine &line = here.at(i);
        if (line.paragraph != current->paragraph || line.line != current->line || line.metrics != current->metrics)
            continue;
        if (i + direction >= 0 && i + direction < here.size()) return positionInLine(here.at(i + direction), x);
        break;
    }
    int page = current->page + direction;
    while (page >= 0) {
        if (direction > 0) ensurePaginated(page);
        if (page >= pageCount()) break;
        const QVector<PageLine> lines = pageLines(page);
        if (const PageLine *target = pick(lines, false)) return positionInLine(*target, x);
        page += direction;
    }
    return direction > 0 ? lineEdge(position, true) : lineEdge(position, false);
}

} // namespace QindaTK
