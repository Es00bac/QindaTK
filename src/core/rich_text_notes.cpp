// SPDX-License-Identifier: LGPL-3.0-or-later
// Footnotes and endnotes (WP-052, docs/references-review.md#notes). A note
// is a reference field (FieldKind::Footnote or ::Endnote) whose result is
// its number and whose record holds its text. Pagination gives each
// footnote room at the foot of the page column where its reference is set,
// in the same page as the reference line; endnotes, and footnotes of table
// cells, follow the last block. Continuous layouts have no page foot, so
// every note follows the last block there.
#include "rich_text_fields_p.h"
#include "rich_text_layout_p.h"
#include "rich_text_ops_p.h"
#include "rich_text_shaping_p.h"
#include "rich_text_spans.h"

#include <QPainter>

namespace QindaTK {
using namespace RichText;

QString RichTextDocument::insertNote(const QString &paragraphId, int offset, const QString &kind, const QString &text)
{
    if (kind != u"footnote" && kind != u"endnote") {
        reject(ErrorCode::InvalidArgument, tr("A note is a footnote or an endnote."));
        return {};
    }
    return insertReference(paragraphId, offset, kind, text);
}

bool RichTextDocument::setNoteText(const QString &noteId, const QString &text)
{
    const auto found = m_annotations.fields.constFind(noteId);
    if (found == m_annotations.fields.constEnd() || !found->isNote())
        return !reject(ErrorCode::InvalidArgument, tr("Unknown note"));
    return editReference(noteId, {{QStringLiteral("text"), text}});
}

namespace RichText {

QVector<NoteReference> noteReferences(const RichTextDocument &doc, const Paragraph &p)
{
    QVector<NoteReference> result;
    const Annotations &a = doc.annotations();
    if (a.fields.isEmpty()) return result;
    int position = 0;
    QString previous;
    QVector<QString> numbers;
    for (const Span &span : p.spans) {
        const CharFormat &format = doc.charFormat(span.format);
        const auto field = a.fields.constFind(format.fieldId);
        // A reference marked deleted sets no note until the deletion is rejected.
        const bool note = field != a.fields.constEnd() && field->isNote() && format.deletionId.isEmpty();
        if (note && format.fieldId != previous) {
            result.append({field->id, position, field->kind == FieldKind::Footnote, field->text.simplified()});
            numbers.append(QString());
        }
        if (note) numbers.last() += p.text.mid(position, span.length); // a result may span runs
        previous = note ? format.fieldId : QString();
        position += span.length;
    }
    for (qsizetype i = 0; i < result.size(); ++i) result[i].text = numbers.at(i) + u'\t' + result.at(i).text;
    return result;
}

QVector<std::pair<QUuid, NoteReference>> endNotes(const RichTextDocument &doc, bool allFootnotes)
{
    QVector<std::pair<QUuid, NoteReference>> result;
    bool any = false;
    for (const ReferenceField &field : doc.annotations().fields) any = any || field.isNote();
    if (!any) return result;
    for (const Paragraph *p : doc.paragraphsInOrder())
        for (const NoteReference &note : noteReferences(doc, *p))
            if (!note.footnote || allFootnotes || p->table) result.append({p->id, note});
    return result;
}

qreal FlowBuilder::noteRoom(qreal height) const
{
    if (height <= 0) return 0;
    return m_notes.isEmpty() ? height + NoteGapPx : height;
}

void FlowBuilder::reserveNote(const Paragraph &p, const QString &fieldId, const QString &text, qreal height)
{
    PageItem item;
    item.kind = PageItem::Note;
    item.block = p.id;
    item.objectId = fieldId;
    item.text = text;
    item.width = m_width;
    item.height = height;
    item.rule = m_notes.isEmpty();
    m_height -= noteRoom(height);
    m_notes.append(item);
}

void FlowBuilder::finishNotes(Page &page)
{
    // AGENT-GUARD: footnotes sit below everything placed in the region: the
    // region shrank by their room as they were reserved (reserveNote).
    qreal y = m_height + NoteGapPx;
    for (PageItem item : std::as_const(m_notes)) {
        item.y = y;
        y += item.height;
        page.items.append(item);
    }
    m_notes.clear();
    m_height = m_regionHeight;
}

bool FlowBuilder::placeEndNotes(FlowState &state, Page &page)
{
    const QVector<std::pair<QUuid, NoteReference>> notes = endNotes(m_document, !m_paged);
    if (notes.isEmpty()) return false;
    state.notes = true;
    state.blockId = QUuid();
    for (int k = state.line; k < notes.size(); ++k) {
        const NoteReference &note = notes.at(k).second;
        const qreal height = m_layout.noteHeight(note.text, m_width);
        const bool rule = k == 0 || !m_hasContent;
        const qreal top = m_y + (rule ? NoteGapPx : 0);
        if (m_paged && m_hasContent && top + height > m_height + 0.01) {
            state.line = k;
            return true;
        }
        PageItem item;
        item.kind = PageItem::Note;
        item.block = notes.at(k).first;
        item.objectId = note.fieldId;
        item.text = note.text;
        item.y = top;
        item.width = m_width;
        item.height = height;
        item.rule = rule;
        page.items.append(item);
        page.overflow = page.overflow || (m_paged && top + height > m_height + 0.01);
        m_y = top + height;
        m_hasContent = true;
    }
    state.line = int(notes.size());
    return false;
}

} // namespace RichText

namespace {

QFont noteFont(const RichTextDocument &doc)
{
    QFont font = Shaping::resolvedFont(doc, CharFormat{}, ParagraphFormat{});
    font.setPointSizeF(font.pointSizeF() * 0.8);
    return font;
}

// "number\ttext" with the number raised, wrapped at `width`.
std::unique_ptr<QTextLayout> shapeNote(const RichTextDocument &doc, const QString &text, qreal width)
{
    const qsizetype tab = text.indexOf(u'\t');
    const QString display = text.left(tab) + QStringLiteral("  ") + text.mid(tab + 1);
    auto layout = std::make_unique<QTextLayout>(display, noteFont(doc), Shaping::layoutDevice());
    QTextCharFormat raised;
    raised.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    layout->setFormats({{0, int(tab), raised}});
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout->setTextOption(option);
    layout->beginLayout();
    qreal y = 0;
    for (QTextLine line = layout->createLine(); line.isValid(); line = layout->createLine()) {
        line.setLineWidth(qMax<qreal>(1, width));
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout->endLayout();
    return layout;
}

} // namespace

qreal RichTextLayout::noteHeight(const QString &text, qreal width)
{
    const QString key = QString::number(width, 'f', 2) + u'|' + text;
    if (const auto found = m_noteHeights.constFind(key); found != m_noteHeights.constEnd()) return *found;
    if (m_noteHeights.size() > 4096) m_noteHeights.clear();
    const qreal height = shapeNote(*m_document, text, width)->boundingRect().height();
    m_noteHeights.insert(key, height);
    return height;
}

bool RichTextLayout::hasNotes() const
{
    if (!m_document) return false;
    for (const ReferenceField &field : m_document->annotations().fields)
        if (field.isNote()) return true;
    return false;
}

void RichTextLayout::paintNote(QPainter *painter, const PageItem &item, const PaintContext &context)
{
    const QPointF content = contentRect().topLeft();
    const QPointF origin(content.x() + item.x, content.y() + item.y);
    if (item.rule) {
        const qreal w = qMax<qreal>(0.75, context.caretWidth * 0.75);
        painter->fillRect(QRectF(origin.x(), origin.y() - NoteGapPx / 2 - w / 2, item.width / 3, w), context.border);
    }
    const QColor defaultInk = m_document->defaults().color;
    painter->setPen(defaultInk.isValid() ? defaultInk : context.ink);
    shapeNote(*m_document, item.text, item.width)->draw(painter, origin);
}

} // namespace QindaTK
