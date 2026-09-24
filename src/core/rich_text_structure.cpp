// SPDX-License-Identifier: LGPL-3.0-or-later
// Tables, page breaks, page setup and section moves of RichTextDocument.
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include <cmath>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

std::optional<Position> RichTextDocument::insertTable(const Position &at, int rows, int columns,
                                                      int headerRows)
{
    Paragraph *p = paragraph(at.paragraph);
    if (!isValidPosition(at) || rows < 1 || columns < 1 || rows > 10000 || columns > 64) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid table size or position"));
        return std::nullopt;
    }
    if (p->table) {
        reject(ErrorCode::Unsupported, QStringLiteral("Tables cannot be nested"));
        return std::nullopt;
    }
    AutoTransaction transaction(this, tr("Insert table"));
    if (!transaction.valid) return std::nullopt;
    const int typing = formatIndexAt(at);
    auto table = std::make_shared<Table>();
    table->id = QUuid::createUuid();
    table->columnWeights = QVector<double>(columns, 1.0);
    table->headerRows = qBound(0, headerRows, rows - 1);
    for (int r = 0; r < rows; ++r) {
        TableRow row{QUuid::createUuid(), {}, 0.0};
        for (int c = 0; c < columns; ++c) {
            auto cellParagraph = std::make_shared<Paragraph>();
            cellParagraph->id = QUuid::createUuid();
            cellParagraph->markFormat = typing;
            row.cells.append(TableCell{QUuid::createUuid(), {cellParagraph}, {}});
        }
        table->rows.append(row);
    }
    bool ok = true;
    paragraphsInOrder();
    int index = p->blockIndex;
    if (at.offset > 0) {
        if (at.offset < p->text.size()) ok = splitParagraph(at).has_value();
        index = p->blockIndex + 1;
    }
    if (ok) {
        StepBlocks insert{{}, index, {Block{{}, table}}, {}, true};
        ok = !apply(std::move(insert));
    }
    if (ok && (index + 1 >= m_blocks.size() || m_blocks.at(index + 1).table)) {
        auto after = std::make_shared<Paragraph>();
        after->id = QUuid::createUuid();
        after->markFormat = typing;
        ok = !apply(StepBlocks{{}, index + 1, {Block{after, {}}}, {}, true});
    }
    const Position caret{table->rows.first().cells.first().paragraphs.first()->id, 0};
    if (!transaction.finish(ok, caret)) return std::nullopt;
    return caret;
}

std::optional<Position> RichTextDocument::insertPageBreak(const Position &at)
{
    Paragraph *p = paragraph(at.paragraph);
    if (!isValidPosition(at) || p->table) {
        reject(ErrorCode::Unsupported, QStringLiteral("Page breaks belong to body paragraphs"));
        return std::nullopt;
    }
    AutoTransaction transaction(this, tr("Page break"));
    if (!transaction.valid) return std::nullopt;
    Position target = at;
    bool ok = true;
    const bool atStart = at.offset == 0 && previousParagraph(p);
    if (!atStart) {
        const auto next = splitParagraph(at);
        ok = next.has_value();
        if (ok) target = *next;
    }
    Paragraph *breakParagraph = ok ? paragraph(target.paragraph) : nullptr;
    if (ok && !breakParagraph->format.pageBreakBefore) {
        ParagraphFormat after = breakParagraph->format;
        after.pageBreakBefore = true;
        ok = !apply(StepParagraphFormat{breakParagraph->id, breakParagraph->format, after});
    }
    const Position caret{target.paragraph, 0};
    if (!transaction.finish(ok, caret)) return std::nullopt;
    return caret;
}

bool RichTextDocument::setPageSetup(const PageSetup &page)
{
    const bool valid = std::isfinite(page.widthPt) && std::isfinite(page.heightPt)
        && std::isfinite(page.marginTopPt) && std::isfinite(page.marginRightPt)
        && std::isfinite(page.marginBottomPt) && std::isfinite(page.marginLeftPt)
        && page.widthPt >= 72 && page.heightPt >= 72 && page.widthPt <= 14400
        && page.heightPt <= 14400 && page.marginTopPt >= 0 && page.marginRightPt >= 0
        && page.marginBottomPt >= 0 && page.marginLeftPt >= 0 && page.contentWidthPt() >= 36
        && page.contentHeightPt() >= 36;
    if (!valid) {
        reject(ErrorCode::Validation, QStringLiteral("Page content area is too small"));
        return false;
    }
    if (page == m_page) return true;
    AutoTransaction transaction(this, tr("Page setup"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepDocument{m_page, page, m_defaults, m_defaults}), {});
}

bool RichTextDocument::setDefaults(const DocumentDefaults &defaults)
{
    if (!(defaults.sizePt >= 1 && defaults.sizePt <= 1638) || defaults.family.isEmpty()) {
        reject(ErrorCode::Validation, QStringLiteral("Invalid default font"));
        return false;
    }
    if (defaults == m_defaults) return true;
    AutoTransaction transaction(this, tr("Default font"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepDocument{m_page, m_page, m_defaults, defaults}), {});
}

QVector<QUuid> RichTextDocument::sectionIds() const
{
    // A section is identified by its first body block, so its identity
    // survives moves; later sections start at pageBreakBefore paragraphs.
    QVector<QUuid> ids;
    if (m_blocks.isEmpty()) return ids;
    ids.append(m_blocks.first().id());
    for (int i = 1; i < m_blocks.size(); ++i) {
        const ParagraphPtr &p = m_blocks.at(i).paragraph;
        if (p && p->format.pageBreakBefore) ids.append(p->id);
    }
    return ids;
}

bool RichTextDocument::moveSection(const QUuid &section, int targetIndex)
{
    const QVector<QUuid> ids = sectionIds();
    const int from = int(ids.indexOf(section));
    if (from < 0 || targetIndex < 0 || targetIndex > ids.size()) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown section or target"));
        return false;
    }
    if (targetIndex == from || targetIndex == from + 1) return true;
    paragraphsInOrder();
    auto blockOf = [&](int index) {
        if (index >= ids.size()) return int(m_blocks.size());
        if (const Paragraph *p = paragraph(ids.at(index)); p && !p->table) return p->blockIndex;
        const Table *t = table(ids.at(index));
        return t ? t->blockIndex : -1;
    };
    const int first = blockOf(from);
    const int count = blockOf(from + 1) - first;
    Paragraph *head = m_blocks.value(first).paragraph.get();
    if (!head) {
        reject(ErrorCode::Unsupported, QStringLiteral("A section that begins with a table cannot move"));
        return false;
    }
    AutoTransaction transaction(this, tr("Move section"));
    if (!transaction.valid) return false;
    bool ok = !apply(StepMove{first, count, blockOf(targetIndex)});
    paragraphsInOrder();
    if (ok && head->blockIndex > 0 && !head->format.pageBreakBefore) {
        ParagraphFormat after = head->format;
        after.pageBreakBefore = true;
        ok = !apply(StepParagraphFormat{head->id, head->format, after});
    }
    if (ok && m_blocks.last().table) {
        auto trailing = std::make_shared<Paragraph>();
        trailing->id = QUuid::createUuid();
        ok = !apply(StepBlocks{{}, int(m_blocks.size()), {Block{trailing, {}}}, {}, true});
    }
    return transaction.finish(ok, {});
}

} // namespace QindaTK
