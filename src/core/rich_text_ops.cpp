// SPDX-License-Identifier: LGPL-3.0-or-later
// High-level editing operations of RichTextDocument, built from primitive steps.
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

#include <algorithm>

namespace QindaTK {
using namespace RichText;

namespace RichText::Detail {

AutoTransaction::AutoTransaction(RichTextDocument *document, const QString &label)
    : doc(document)
{
    if (!doc->inTransaction()) {
        owned = doc->beginTransaction(label);
        valid = owned;
    }
}

AutoTransaction::~AutoTransaction()
{
    if (owned) doc->cancelTransaction();
}

bool AutoTransaction::finish(bool ok, const Position &caret)
{
    if (!owned) return ok;
    owned = false;
    if (!ok) {
        doc->cancelTransaction();
        return false;
    }
    return doc->commitTransaction(Selection{caret, caret});
}

QString cleanInsertedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    text.replace(QChar(QChar::ParagraphSeparator), QChar(u'\n'));
    QString out;
    out.reserve(text.size());
    for (const QChar c : std::as_const(text)) {
        const char16_t u = c.unicode();
        if (u == u'\n' || u == u'\t' || u == LineBreakChar) out += c;
        else if (u >= 0x20 && u != 0x7f && u != ObjectChar && !(u >= 0x80 && u < 0xa0)) out += c;
    }
    return out;
}

StepText textStep(const RichTextDocument &doc, const Paragraph &p, int offset, int length,
                  const QString &inserted, const QVector<Span> &insertedSpans,
                  const QVector<RichObject> &insertedObjects)
{
    StepText step;
    step.paragraph = p.id;
    step.offset = offset;
    step.removed = p.text.mid(offset, length);
    step.removedSpans = Spans::slice(p.spans, offset, length);
    step.removedObjects = doc.objectsIn(p, offset, offset + length);
    step.inserted = inserted;
    step.insertedSpans = Spans::normalized(insertedSpans);
    step.insertedObjects = insertedObjects;
    return step;
}

} // namespace RichText::Detail

using namespace RichText::Detail;

ParagraphPtr RichTextDocument::paragraphPtr(const Paragraph *p) const
{
    if (!p) return {};
    ensureOrder();
    if (!p->table) return m_blocks.value(p->blockIndex).paragraph;
    for (const ParagraphPtr &item : p->table->rows.at(p->row).cells.at(p->cell).paragraphs)
        if (item.get() == p) return item;
    return {};
}

QVector<RichObject> RichTextDocument::objectsIn(const Paragraph &p, int from, int to) const
{
    QVector<RichObject> result;
    for (int i = qMax(0, from); i < qMin(to, int(p.text.size())); ++i) {
        if (p.text.at(i) != QChar(ObjectChar)) continue;
        if (const RichObject *o = object(charFormat(Spans::formatAt(p.spans, i)).objectId))
            result.append(*o);
    }
    return result;
}

QVector<RichObject> RichTextDocument::objectsIn(const QVector<Block> &blocks) const
{
    QVector<RichObject> result;
    for (const Block &block : blocks) {
        if (block.paragraph) {
            result += objectsIn(*block.paragraph, 0, int(block.paragraph->text.size()));
            continue;
        }
        for (const TableRow &row : block.table->rows)
            for (const TableCell &cell : row.cells)
                for (const ParagraphPtr &p : cell.paragraphs) result += objectsIn(*p, 0, int(p->text.size()));
    }
    return result;
}

std::optional<Position> RichTextDocument::insertText(const Position &requested, const QString &raw, int format)
{
    if (!isValidPosition(requested)) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid insertion position"));
        return std::nullopt;
    }
    const Position at = outsideField(requested); // field results are atomic
    const QString text = cleanInsertedText(raw);
    AutoTransaction transaction(this, tr("Typing"));
    if (!transaction.valid) return std::nullopt;
    CharFormat plain = charFormat(format >= 0 && format < m_formats.size() ? format : formatIndexAt(at));
    // Typed text is never an object, a field or a deletion; with tracking on
    // it joins or starts an insertion (docs/references-review.md#tracked-changes).
    // Only text with characters records a change (a bare "\n" splits a paragraph).
    const bool characters = std::any_of(text.cbegin(), text.cend(), [](QChar c) { return c != u'\n'; });
    const std::optional<QString> insertion = characters ? trackedInsertionId(at) : std::optional<QString>(QString());
    if (!insertion) return transaction.finish(false, at), std::nullopt;
    plain.objectId.clear();
    plain.fieldId.clear();
    plain.deletionId.clear();
    plain.insertionId = *insertion;
    plain.commentIds = m_annotations.threads.isEmpty() ? QStringList() : charFormat(formatIndexAt(at)).commentIds;
    const int typing = internFormat(plain);
    Position caret = at;
    const QStringList lines = text.split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            const auto next = splitParagraph(caret);
            if (!next) return transaction.finish(false, caret), std::nullopt;
            caret = *next;
        }
        const QString &line = lines.at(i);
        if (line.isEmpty()) continue;
        const Paragraph *p = paragraph(caret.paragraph);
        if (apply(textStep(*this, *p, caret.offset, 0, line, {{int(line.size()), typing}})))
            return transaction.finish(false, caret), std::nullopt;
        caret.offset += int(line.size());
    }
    if (!transaction.finish(true, caret)) return std::nullopt;
    return caret;
}

std::optional<Position> RichTextDocument::splitParagraph(const Position &at)
{
    if (!isValidPosition(at)) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid split position"));
        return std::nullopt;
    }
    AutoTransaction transaction(this, tr("New paragraph"));
    if (!transaction.valid) return std::nullopt;
    Paragraph *p = paragraph(at.paragraph);
    if (at.offset == 0 && !p->text.isEmpty()) {
        // Enter at a paragraph start opens an empty paragraph above it, so the
        // text keeps its identity; the page break moves up with the new line.
        auto created = std::make_shared<Paragraph>();
        created->id = QUuid::createUuid();
        created->format = p->format;
        created->markFormat = formatIndexAt(at);
        ensureOrder();
        const ContainerSpec spec = containerOf(p);
        int index = p->blockIndex;
        if (!spec.isBody()) {
            const auto &cell = p->table->rows.at(p->row).cells.at(p->cell).paragraphs;
            index = int(std::find_if(cell.begin(), cell.end(), [p](const ParagraphPtr &item) {
                return item.get() == p; }) - cell.begin());
        }
        bool ok = !apply(StepBlocks{spec, index, {Block{created, {}}}, {}, true});
        if (ok && p->format.pageBreakBefore) {
            ParagraphFormat after = p->format;
            after.pageBreakBefore = false;
            ok = !apply(StepParagraphFormat{p->id, p->format, after});
        }
        if (!transaction.finish(ok, at)) return std::nullopt;
        return at;
    }
    auto created = std::make_shared<Paragraph>();
    created->id = QUuid::createUuid();
    created->format = p->format;
    created->format.pageBreakBefore = false;
    if (at.offset == p->text.size() && p->format.headingLevel > 0) {
        // Body text follows a heading; a heading style does not carry over.
        created->format.headingLevel = 0;
        created->format.keepWithNext = false;
        created->format.styleId.clear();
        created->format.overrides = 0;
    }
    created->markFormat = formatIndexAt(at);
    const Position caret{created->id, 0};
    if (!transaction.finish(!apply(StepSplit{p->id, at.offset, created}), caret)) return std::nullopt;
    return caret;
}

static bool containerIndexRange(const RichTextDocument &doc, const Paragraph *first,
                                const Paragraph *last, int *from, int *to)
{
    if (!first->table) {
        *from = first->blockIndex;
        *to = last->blockIndex;
        return true;
    }
    const auto &cell = first->table->rows.at(first->row).cells.at(first->cell).paragraphs;
    *from = *to = -1;
    for (int i = 0; i < cell.size(); ++i) {
        if (cell.at(i).get() == first) *from = i;
        if (cell.at(i).get() == last) *to = i;
    }
    Q_UNUSED(doc);
    return *from >= 0 && *to >= *from;
}

// Delete [start, end) whose paragraphs share one container: trim both ends,
// drop the blocks between and join the two remaining paragraphs.
static bool deleteWithinContainer(RichTextDocument &doc, const Position &start, const Position &end)
{
    Paragraph *first = doc.paragraph(start.paragraph);
    Paragraph *last = doc.paragraph(end.paragraph);
    if (first == last)
        return !doc.apply(textStep(doc, *first, start.offset, end.offset - start.offset, {}, {}));
    if (start.offset < first->text.size()
        && doc.apply(textStep(doc, *first, start.offset, int(first->text.size()) - start.offset, {}, {})))
        return false;
    if (end.offset > 0 && doc.apply(textStep(doc, *last, 0, end.offset, {}, {}))) return false;
    doc.paragraphsInOrder();
    int from = 0, to = 0;
    if (!containerIndexRange(doc, first, last, &from, &to)) return false;
    const ContainerSpec spec = doc.containerOf(first);
    if (to - from > 1) {
        QVector<Block> between;
        if (spec.isBody()) {
            between = doc.blocks().mid(from + 1, to - from - 1);
        } else {
            const auto &cell = first->table->rows.at(first->row).cells.at(first->cell).paragraphs;
            for (int i = from + 1; i < to; ++i) between.append(Block{cell.at(i), {}});
        }
        if (doc.apply(StepBlocks{spec, from + 1, between, doc.objectsIn(between), false})) return false;
    }
    return !doc.apply(StepMerge{first->id, doc.paragraphPtr(last), -1});
}

std::optional<Position> RichTextDocument::deleteRange(const Position &a, const Position &b)
{
    if (!isValidPosition(a) || !isValidPosition(b)) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid range"));
        return std::nullopt;
    }
    const bool ordered = compare(a, b) <= 0;
    Position start = ordered ? a : b;
    Position end = ordered ? b : a;
    if (start == end) return start;
    expandOverFields(&start, &end); // a field result goes as a whole
    AutoTransaction transaction(this, tr("Delete"));
    if (!transaction.valid) return std::nullopt;
    if (m_trackChanges) {
        // Tracked: the text stays, marked deleted (paragraphs are not joined).
        if (!transaction.finish(markDeleted(start, end), start)) return std::nullopt;
        return start;
    }
    Paragraph *first = paragraph(start.paragraph);
    Paragraph *last = paragraph(end.paragraph);
    bool ok = true;
    if (containerOf(first) == containerOf(last)) {
        ok = deleteWithinContainer(*this, start, end);
    } else {
        // Crossing a table boundary: text is removed per container, fully
        // covered cells are emptied and body paragraphs between tables go;
        // table structure only disappears when both ends are in the body.
        QVector<QVector<Paragraph *>> groups;
        ContainerSpec current;
        const auto &flat = paragraphsInOrder();
        for (int i = first->flatIndex; i <= last->flatIndex; ++i) {
            const ContainerSpec spec = containerOf(flat.at(i));
            if (groups.isEmpty() || !(spec == current)) {
                groups.append(QVector<Paragraph *>{});
                current = spec;
            }
            groups.last().append(flat.at(i));
        }
        QVector<QPair<Position, Position>> ranges;
        QVector<bool> removeWhole;
        for (int g = 0; g < groups.size(); ++g) {
            const auto &group = groups.at(g);
            const Position from = g == 0 ? start : Position{group.first()->id, 0};
            const Position to = g == groups.size() - 1
                ? end : Position{group.last()->id, int(group.last()->text.size())};
            ranges.append({from, to});
            removeWhole.append(g > 0 && g < groups.size() - 1 && !group.first()->table);
        }
        for (qsizetype g = groups.size() - 1; ok && g >= 0; --g) {
            if (!removeWhole.at(g)) {
                ok = deleteWithinContainer(*this, ranges.at(g).first, ranges.at(g).second);
                continue;
            }
            paragraphsInOrder();
            const int from = groups.at(g).first()->blockIndex;
            const QVector<Block> whole = m_blocks.mid(from, groups.at(g).size());
            ok = !apply(StepBlocks{{}, from, whole, objectsIn(whole), false});
        }
    }
    if (!transaction.finish(ok, start)) return std::nullopt;
    return start;
}

bool RichTextDocument::applyCharFormat(const Position &a, const Position &b, const CharFormatPatch &patch)
{
    if (patch.isEmpty() || !isValidPosition(a) || !isValidPosition(b)) return false;
    const bool ordered = compare(a, b) <= 0;
    const Position start = ordered ? a : b;
    const Position end = ordered ? b : a;
    if (start == end) return true;
    AutoTransaction transaction(this, tr("Format"));
    if (!transaction.valid) return false;
    const Paragraph *first = paragraph(start.paragraph);
    const Paragraph *last = paragraph(end.paragraph);
    const QVector<Paragraph *> flat = paragraphsInOrder();
    bool ok = true;
    for (int i = first->flatIndex; ok && i <= last->flatIndex; ++i) {
        Paragraph *p = flat.at(i);
        const int from = p == first ? start.offset : 0;
        const int to = p == last ? end.offset : int(p->text.size());
        StepSpans step;
        step.paragraph = p->id;
        step.offset = from;
        step.before = Spans::slice(p->spans, from, to - from);
        step.after = Spans::mapped(step.before, [&](int f) { return internFormat(patch.applied(charFormat(f))); });
        if (p->text.isEmpty() || (from == 0 && to == p->text.size())) {
            const int mark = internFormat(patch.applied(charFormat(p->markFormat)));
            if (mark != p->markFormat) {
                step.markBefore = p->markFormat;
                step.markAfter = mark;
            }
        }
        if (step.before == step.after && step.markBefore < 0) continue;
        ok = !apply(step);
    }
    return transaction.finish(ok, end);
}

bool RichTextDocument::setParagraphFormat(const Position &a, const Position &b,
                                          const ParagraphFormatPatch &patch)
{
    if (patch.isEmpty() || !isValidPosition(a) || !isValidPosition(b)) return false;
    const bool ordered = compare(a, b) <= 0;
    const Paragraph *first = paragraph((ordered ? a : b).paragraph);
    const Paragraph *last = paragraph((ordered ? b : a).paragraph);
    AutoTransaction transaction(this, tr("Paragraph format"));
    if (!transaction.valid) return false;
    const QVector<Paragraph *> flat = paragraphsInOrder();
    bool ok = true;
    for (int i = first->flatIndex; ok && i <= last->flatIndex; ++i) {
        Paragraph *p = flat.at(i);
        // Patched members of a styled paragraph become direct overrides; the
        // rest keep (or, with a new style ID, take) the style's values.
        ParagraphFormat after = materializedFormat(m_styles, patch.applied(p->format));
        if (p->table) after.pageBreakBefore = false;
        if (!(after == p->format)) ok = !apply(StepParagraphFormat{p->id, p->format, after});
    }
    return transaction.finish(ok, ordered ? b : a);
}

static ListDefinition makeList(ListKind kind)
{
    static const ListKind bullets[] = {ListKind::Disc, ListKind::Circle, ListKind::Square};
    static const ListKind numbers[] = {ListKind::Decimal, ListKind::LowerAlpha, ListKind::LowerRoman};
    ListDefinition def;
    def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (int level = 0; level < 9; ++level) {
        ListLevel entry;
        entry.kind = level == 0 ? kind : isListKindOrdered(kind) ? numbers[level % 3] : bullets[level % 3];
        def.levels.append(entry);
    }
    return def;
}

bool RichTextDocument::setList(const Position &a, const Position &b, std::optional<ListKind> kind)
{
    if (!isValidPosition(a) || !isValidPosition(b)) return false;
    ParagraphFormatPatch patch;
    AutoTransaction transaction(this, kind ? tr("List") : tr("Remove list"));
    if (!transaction.valid) return false;
    bool ok = true;
    if (!kind) {
        patch.listId = QString();
        patch.listLevel = 0;
    } else {
        const bool ordered = compare(a, b) <= 0;
        const Paragraph *first = paragraph((ordered ? a : b).paragraph);
        const Paragraph *previous = previousParagraph(first);
        QString listId;
        if (previous && previous->table == first->table && !previous->format.listId.isEmpty()) {
            const ListDefinition *def = list(previous->format.listId);
            if (def && !def->levels.isEmpty() && def->levels.first().kind == *kind) listId = def->id;
        }
        if (listId.isEmpty()) {
            const ListDefinition def = makeList(*kind);
            listId = def.id;
            ok = !apply(StepList{def.id, std::nullopt, def});
        }
        patch.listId = listId;
    }
    ok = ok && setParagraphFormat(a, b, patch);
    return transaction.finish(ok, b);
}

bool RichTextDocument::changeListLevel(const Position &a, const Position &b, int delta)
{
    if (!isValidPosition(a) || !isValidPosition(b) || delta == 0) return false;
    const bool ordered = compare(a, b) <= 0;
    const Paragraph *first = paragraph((ordered ? a : b).paragraph);
    const Paragraph *last = paragraph((ordered ? b : a).paragraph);
    AutoTransaction transaction(this, tr("List level"));
    if (!transaction.valid) return false;
    const QVector<Paragraph *> flat = paragraphsInOrder();
    bool ok = true;
    bool changed = false;
    for (int i = first->flatIndex; ok && i <= last->flatIndex; ++i) {
        Paragraph *p = flat.at(i);
        if (p->format.listId.isEmpty()) continue;
        ParagraphFormat after = p->format;
        after.listLevel = qBound(0, p->format.listLevel + delta, 8);
        if (after == p->format) continue;
        changed = true;
        ok = !apply(StepParagraphFormat{p->id, p->format, after});
    }
    return transaction.finish(ok && changed, b);
}

std::optional<Position> RichTextDocument::insertObject(const Position &requested, RichObject object)
{
    if (object.id.isEmpty()) object.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const Position at = isValidPosition(requested) ? outsideField(requested) : requested;
    const Paragraph *p = paragraph(at.paragraph);
    if (!isValidPosition(at) || m_objects.contains(object.id) || !(object.widthPt > 0)
        || !(object.heightPt > 0) || (p->table && object.placement != Placement::Inline)) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Object cannot be inserted here"));
        return std::nullopt;
    }
    AutoTransaction transaction(this, tr("Insert object"));
    if (!transaction.valid) return std::nullopt;
    CharFormat format = charFormat(formatIndexAt(at));
    format.objectId = object.id;
    format.href.clear();
    const std::optional<QString> insertion = trackedInsertionId(at);
    if (!insertion) return transaction.finish(false, at), std::nullopt;
    format.insertionId = *insertion;
    const int index = internFormat(format);
    const Position caret{at.paragraph, at.offset + 1};
    const bool ok = !apply(textStep(*this, *p, at.offset, 0, QString(QChar(ObjectChar)),
                                    {{1, index}}, {object}));
    if (!transaction.finish(ok, caret)) return std::nullopt;
    return caret;
}

bool RichTextDocument::setObject(const RichObject &object)
{
    const RichObject *current = this->object(object.id);
    if (!current || !(object.widthPt > 0) || !(object.heightPt > 0)) return false;
    if (*current == object) return true;
    AutoTransaction transaction(this, tr("Object"));
    if (!transaction.valid) return false;
    return transaction.finish(!apply(StepObject{object.id, *current, object}), {});
}

} // namespace QindaTK
