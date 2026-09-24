// SPDX-License-Identifier: LGPL-3.0-or-later
// RichTextArea editing: transactions per user action, deletion, typing.
#include "rich_text_area.h"
#include "rich_text_clipboard.h"
#include "rich_text_format_state.h"
#include "rich_text_graphemes.h"

#include <QAccessible>
#include <QTextBoundaryFinder>

namespace QindaTK {
using namespace RichText;

bool RichTextArea::edit(const QString &label, const QString &mergeKey,
                        const std::function<std::optional<Position>(RichTextDocument &)> &operation)
{
    if (!m_document || m_readOnly || m_document->readOnly()) return false;
    const Selection before = selection();
    if (!m_document->beginTransaction(label, mergeKey, before)) return false;
    const std::optional<Position> caret = operation(*m_document);
    if (!caret) {
        m_document->cancelTransaction();
        emit editRejected(m_document->lastError());
        return false;
    }
    const Position after = m_document->clampPosition(*caret);
    // Anchors are set before commit so views repaint with the new caret.
    m_anchor.position = m_focus.position = after;
    if (!m_document->commitTransaction({after, after})) {
        emit editRejected(m_document->lastError());
        return false;
    }
    m_goalX = -1;
    selectionUpdated(true);
    return true;
}

bool RichTextArea::insertText(const QString &text)
{
    if (text.isEmpty()) return false;
    const bool replacing = hasSelection();
    const QString key = replacing ? QString() : QStringLiteral("typing");
    const std::optional<CharFormatPatch> pending = m_pendingFormat;
    const bool ok = edit(tr("Typing"), key, [&](RichTextDocument &doc) -> std::optional<Position> {
        Position at = m_focus.position;
        if (replacing) {
            const auto removed = doc.deleteRange(m_anchor.position, m_focus.position);
            if (!removed) return std::nullopt;
            at = *removed;
        }
        int format = doc.formatIndexAt(at);
        if (pending) format = doc.internFormat(pending->applied(doc.charFormat(format)));
        return doc.insertText(at, text, format);
    });
    if (ok && pending) {
        m_pendingFormat = pending; // keep typing in the chosen format
        m_formatState->refresh();
    }
    return ok;
}

bool RichTextArea::deleteBackward(bool word)
{
    if (hasSelection())
        return edit(tr("Delete"), {}, [this](RichTextDocument &doc) { return doc.deleteRange(m_anchor.position, m_focus.position); });
    const Position caret = m_focus.position;
    const Paragraph *p = m_document ? m_document->paragraph(caret.paragraph) : nullptr;
    if (!p) return false;
    Position from = caret;
    if (caret.offset > 0) {
        if (word) {
            QTextBoundaryFinder words(QTextBoundaryFinder::Word, p->text);
            words.setPosition(caret.offset);
            from.offset = qMax(0, int(words.toPreviousBoundary()));
        } else {
            from.offset = GraphemeBoundaries(p->text).previous(caret.offset);
        }
    } else {
        // At a paragraph start: leave a list first, else join the previous paragraph.
        if (!p->format.listId.isEmpty())
            return edit(tr("Remove list"), {}, [caret](RichTextDocument &doc) -> std::optional<Position> {
                if (!doc.setList(caret, caret, std::nullopt)) return std::nullopt;
                return caret;
            });
        const Paragraph *previous = m_document->previousParagraph(p);
        if (!previous || previous->table != p->table || previous->row != p->row || previous->cell != p->cell)
            return false;
        from = {previous->id, int(previous->text.size())};
    }
    return edit(tr("Delete"), QStringLiteral("backspace"), [from, caret](RichTextDocument &doc) { return doc.deleteRange(from, caret); });
}

bool RichTextArea::deleteForward(bool word)
{
    if (hasSelection()) return deleteBackward(false);
    const Position caret = m_focus.position;
    const Paragraph *p = m_document ? m_document->paragraph(caret.paragraph) : nullptr;
    if (!p) return false;
    Position to = caret;
    if (caret.offset < p->text.size()) {
        if (word) {
            QTextBoundaryFinder words(QTextBoundaryFinder::Word, p->text);
            words.setPosition(caret.offset);
            to.offset = int(words.toNextBoundary());
            if (to.offset < 0) to.offset = int(p->text.size());
        } else {
            to.offset = GraphemeBoundaries(p->text).next(caret.offset);
        }
    } else {
        const Paragraph *next = m_document->nextParagraph(p);
        if (!next || next->table != p->table || next->row != p->row || next->cell != p->cell) return false;
        to = {next->id, 0};
    }
    return edit(tr("Delete"), QStringLiteral("delete"), [caret, to](RichTextDocument &doc) { return doc.deleteRange(caret, to); });
}

bool RichTextArea::newParagraph()
{
    const Paragraph *p = m_document ? m_document->paragraph(m_focus.position.paragraph) : nullptr;
    if (!p) return false;
    // Enter in an empty list item ends the list, as word processors do.
    if (!hasSelection() && p->text.isEmpty() && !p->format.listId.isEmpty()) {
        const Position caret = m_focus.position;
        return edit(tr("Remove list"), {}, [caret](RichTextDocument &doc) -> std::optional<Position> {
            if (!doc.setList(caret, caret, std::nullopt)) return std::nullopt;
            return caret;
        });
    }
    return edit(tr("New paragraph"), {}, [this](RichTextDocument &doc) -> std::optional<Position> {
        Position at = m_focus.position;
        if (hasSelection()) {
            const auto removed = doc.deleteRange(m_anchor.position, m_focus.position);
            if (!removed) return std::nullopt;
            at = *removed;
        }
        return doc.splitParagraph(at);
    });
}

bool RichTextArea::tabKey(bool backwards)
{
    const Paragraph *p = m_document ? m_document->paragraph(m_focus.position.paragraph) : nullptr;
    if (!p) return false;
    m_document->paragraphsInOrder(); // current table, row and cell of p
    if (p->table) {
        // Tab moves between cells in reading order, over the slots a merged
        // cell covers; past the last cell it leaves the table.
        const Table *table = p->table;
        const int columns = int(table->columnWeights.size());
        const int slotCount = int(table->rows.size()) * columns;
        int index = p->row * columns + p->cell;
        do index += backwards ? -1 : 1;
        while (index >= 0 && index < slotCount && table->rows.at(index / columns).cells.at(index % columns).paragraphs.isEmpty());
        if (index < 0) return true;
        if (index >= slotCount) {
            const Paragraph *after = m_document->nextParagraph(p);
            while (after && after->table == table) after = m_document->nextParagraph(after);
            if (after) moveCaret({after->id, 0}, false);
            return true;
        }
        const auto &cell = table->rows.at(index / columns).cells.at(index % columns);
        const Paragraph *target = cell.paragraphs.first().get();
        setSelection({{target->id, 0}, {cell.paragraphs.last()->id, int(cell.paragraphs.last()->text.size())}});
        return true;
    }
    if (!p->format.listId.isEmpty())
        return edit(tr("List level"), {}, [this, backwards](RichTextDocument &doc) -> std::optional<Position> {
            if (!doc.changeListLevel(m_anchor.position, m_focus.position, backwards ? -1 : 1)) return std::nullopt;
            return m_focus.position;
        });
    if (backwards) return false;
    return insertText(QStringLiteral("\t"));
}

bool RichTextArea::formatSelection(const CharFormatPatch &patch, const QString &label)
{
    if (!m_document || m_readOnly || m_document->readOnly()) return false;
    if (!hasSelection()) {
        // No range: the patch applies to the next typed text at the caret.
        CharFormatPatch merged = m_pendingFormat.value_or(CharFormatPatch{});
        merged.merge(patch);
        m_pendingFormat = merged;
        m_formatState->refresh();
        return true;
    }
    const Selection kept = selection();
    const bool ok = edit(label, {}, [&](RichTextDocument &doc) -> std::optional<Position> {
        if (!doc.applyCharFormat(kept.anchor, kept.focus, patch)) return std::nullopt;
        return kept.focus;
    });
    if (ok) setSelection(kept, false); // a formatting action keeps the selection
    return ok;
}

bool RichTextArea::formatParagraphs(const ParagraphFormatPatch &patch, const QString &label)
{
    const Selection kept = selection();
    const bool ok = edit(label, {}, [&](RichTextDocument &doc) -> std::optional<Position> {
        if (!doc.setParagraphFormat(kept.anchor, kept.focus, patch)) return std::nullopt;
        return kept.focus;
    });
    if (ok) setSelection(kept, false);
    return ok;
}

QString RichTextArea::selectedText() const
{
    if (!m_document || !hasSelection()) return {};
    return m_document->plainText(m_anchor.position, m_focus.position);
}

bool RichTextArea::copySelection(bool cut)
{
    if (!m_document || !hasSelection()) return false;
    const Fragment fragment = m_document->extractFragment(m_anchor.position, m_focus.position);
    if (!m_clipboard->copy(*m_document, fragment, selectedText())) {
        emit editRejected(m_clipboard->lastError());
        return false;
    }
    if (!cut) return true;
    return edit(tr("Cut"), {}, [this](RichTextDocument &doc) { return doc.deleteRange(m_anchor.position, m_focus.position); });
}

bool RichTextArea::paste()
{
    if (!m_document) return false;
    const RichTextClipboard::Content content = m_clipboard->read();
    if (content.isEmpty()) return false;
    // Structured content keeps its formats; plain text takes the caret's.
    return edit(tr("Paste"), {}, [&](RichTextDocument &doc) -> std::optional<Position> {
        Position at = m_focus.position;
        if (hasSelection()) {
            const auto removed = doc.deleteRange(m_anchor.position, m_focus.position);
            if (!removed) return std::nullopt;
            at = *removed;
        }
        if (content.fragment) return doc.insertFragment(at, *content.fragment);
        int format = doc.formatIndexAt(at);
        if (m_pendingFormat) format = doc.internFormat(m_pendingFormat->applied(doc.charFormat(format)));
        return doc.insertText(at, content.text, format);
    });
}

void RichTextArea::setClipboardAdapter(ClipboardAdapter *adapter)
{
    m_clipboard->setAdapter(adapter);
}

bool RichTextArea::moveSectionOfPage(int page, int targetSection)
{
    if (!m_document || page < 0 || page >= pageCount()) return false;
    const QUuid section = m_layout->page(page).section;
    const Selection kept = selection();
    return edit(tr("Move section"), {}, [&](RichTextDocument &doc) -> std::optional<Position> {
        if (!doc.moveSection(section, targetSection)) return std::nullopt;
        return kept.focus;
    });
}

// ---- accessible plain-text offsets --------------------------------------

void RichTextArea::rebuildAccessibleIndex() const
{
    if (!m_document || m_accessibleRevision == m_document->revision()) return;
    const auto &flat = m_document->paragraphsInOrder();
    m_accessibleStarts.resize(flat.size());
    int offset = 0;
    for (int i = 0; i < flat.size(); ++i) {
        m_accessibleStarts[i] = offset;
        offset += int(flat.at(i)->text.size()) + 1;
    }
    m_accessibleRevision = m_document->revision();
}

int RichTextArea::accessibleOffset(const Position &position) const
{
    const Paragraph *p = m_document ? m_document->paragraph(position.paragraph) : nullptr;
    if (!p) return 0;
    rebuildAccessibleIndex();
    return m_accessibleStarts.value(p->flatIndex) + position.offset;
}

Position RichTextArea::positionFromAccessible(int offset) const
{
    if (!m_document) return {};
    rebuildAccessibleIndex();
    const auto &flat = m_document->paragraphsInOrder();
    const auto it = std::upper_bound(m_accessibleStarts.begin(), m_accessibleStarts.end(), offset);
    const int index = qMax(0, int(it - m_accessibleStarts.begin()) - 1);
    const Paragraph *p = flat.at(index);
    return m_document->clampPosition({p->id, offset - m_accessibleStarts.at(index)});
}

QString RichTextArea::accessibleText() const
{
    if (!m_document) return {};
    QStringList paragraphs;
    for (const Paragraph *p : m_document->paragraphsInOrder()) paragraphs.append(p->text);
    return paragraphs.join(u'\n');
}

int RichTextArea::cursorPosition() const { return accessibleOffset(m_focus.position); }

void RichTextArea::setCursorPosition(int position)
{
    const Position target = positionFromAccessible(position);
    if (target.isValid()) moveCaret(target, false);
}

int RichTextArea::selectionStart() const
{
    return qMin(accessibleOffset(m_anchor.position), accessibleOffset(m_focus.position));
}

int RichTextArea::selectionEnd() const
{
    return qMax(accessibleOffset(m_anchor.position), accessibleOffset(m_focus.position));
}

} // namespace QindaTK
