// SPDX-License-Identifier: LGPL-3.0-or-later
// Range copy (extractFragment), paste (insertFragment) and plain-text export.
#include "rich_text_document.h"
#include "rich_text_ops_p.h"
#include "rich_text_spans.h"

#include <algorithm>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

namespace {

struct FragmentBuilder {
    const RichTextDocument &doc;
    Fragment fragment;
    QHash<int, int> formats;

    // A copied style travels with its parents.
    void addStyle(const QString &id)
    {
        for (const QString &style : styleChain(doc.styleSheet(), id))
            fragment.styles.insert(style, doc.styleSheet().value(style));
    }

    int mapFormat(int index)
    {
        const auto found = formats.constFind(index);
        if (found != formats.constEnd()) return found.value();
        const int mapped = int(fragment.formats.size());
        fragment.formats.append(doc.charFormat(index));
        addStyle(doc.charFormat(index).styleId);
        formats.insert(index, mapped);
        return mapped;
    }

    ParagraphPtr copy(const Paragraph &source, int from, int to)
    {
        auto p = std::make_shared<Paragraph>();
        p->id = source.id;
        p->text = source.text.mid(from, to - from);
        p->spans = Spans::mapped(Spans::slice(source.spans, from, to - from),
                                 [this](int f) { return mapFormat(f); });
        p->markFormat = mapFormat(source.markFormat);
        p->format = source.format;
        addStyle(source.format.styleId);
        for (const RichObject &object : doc.objectsIn(source, from, to))
            fragment.objects.insert(object.id, object);
        if (const ListDefinition *list = doc.list(source.format.listId))
            fragment.lists.insert(list->id, *list);
        return p;
    }

    TablePtr copy(const Table &source)
    {
        addStyle(source.styleId);
        auto table = std::make_shared<Table>(source);
        for (TableRow &row : table->rows)
            for (TableCell &cell : row.cells)
                for (ParagraphPtr &p : cell.paragraphs) p = copy(*p, 0, int(p->text.size()));
        return table;
    }
};

} // namespace

Fragment RichTextDocument::extractFragment(const Position &a, const Position &b) const
{
    FragmentBuilder builder{*this, {}, {}};
    if (!isValidPosition(a) || !isValidPosition(b)) return builder.fragment;
    const bool ordered = compare(a, b) <= 0;
    const Position start = ordered ? a : b;
    const Position end = ordered ? b : a;
    if (start == end) return builder.fragment;
    const Paragraph *first = paragraph(start.paragraph);
    const Paragraph *last = paragraph(end.paragraph);
    const ContainerSpec firstSpec = containerOf(first);
    auto partial = [&](const Paragraph *p) {
        const int from = p == first ? start.offset : 0;
        const int to = p == last ? end.offset : int(p->text.size());
        builder.fragment.blocks.append(Block{builder.copy(*p, from, to), {}});
    };
    if (firstSpec == containerOf(last) && !firstSpec.isBody()) {
        // Inside one cell: the covered paragraphs only.
        const auto &flat = paragraphsInOrder();
        for (int i = first->flatIndex; i <= last->flatIndex; ++i) partial(flat.at(i));
    } else {
        // Body range; a table touched by either end is copied whole.
        const int from = first->table ? first->table->blockIndex : first->blockIndex;
        const int to = last->table ? last->table->blockIndex : last->blockIndex;
        for (int i = from; i <= to; ++i) {
            const Block &block = m_blocks.at(i);
            if (block.paragraph) partial(block.paragraph.get());
            else builder.fragment.blocks.append(Block{{}, builder.copy(*block.table)});
        }
    }
    builder.fragment.openStart = bool(builder.fragment.blocks.first().paragraph);
    builder.fragment.openEnd = bool(builder.fragment.blocks.last().paragraph);
    return builder.fragment;
}

namespace {

// Gives pasted content fresh identities and document format indexes.
struct FragmentImporter {
    RichTextDocument &doc;
    const Fragment &fragment;
    QVector<int> formats;
    QHash<QString, QString> objectIds;
    QHash<QString, QString> listIds;
    QHash<QString, QString> styleIds;
    QSet<QString> reanchored;   // fields and comments whose text is not in the document
    QString insertion;          // tracked insertion of the paste, or ""

    bool nameTaken(const QString &name) const
    {
        const QString folded = name.trimmed().toCaseFolded();
        for (const NamedStyle &style : doc.styleSheet())
            if (style.name.trimmed().toCaseFolded() == folded) return true;
        return false;
    }

    // Pasted styles: an existing style with the same ID, or else the same
    // name, and kind is used as it is (the destination's definition wins);
    // any other is created, parents first, under a free name.
    bool importStyles()
    {
        QStringList order = fragment.styles.keys();
        std::sort(order.begin(), order.end(), [this](const QString &a, const QString &b) {
            const qsizetype da = styleChain(fragment.styles, a).size();
            const qsizetype db = styleChain(fragment.styles, b).size();
            return da != db ? da < db : a < b;
        });
        for (const QString &id : std::as_const(order)) {
            const NamedStyle &source = fragment.styles[id];
            if (const NamedStyle *same = doc.style(id); same && same->kind == source.kind) {
                styleIds.insert(id, id);
                continue;
            }
            QString reuse;
            for (const NamedStyle &style : doc.styleSheet())
                if (style.kind == source.kind && style.name.trimmed().toCaseFolded() == source.name.trimmed().toCaseFolded())
                    reuse = style.id;
            if (!reuse.isEmpty()) {
                styleIds.insert(id, reuse);
                continue;
            }
            NamedStyle copy = source;
            copy.id = doc.style(id) ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
            copy.parentId = styleIds.value(source.parentId);
            for (int n = 2; nameTaken(copy.name); ++n) copy.name = QStringLiteral("%1 %2").arg(source.name.trimmed()).arg(n);
            if (doc.apply(StepStyle{copy.id, std::nullopt, copy})) return false;
            styleIds.insert(id, copy.id);
        }
        return true;
    }

    bool prepare()
    {
        if (!importStyles()) return false;
        for (const RichObject &object : fragment.objects)
            objectIds.insert(object.id, QUuid::createUuid().toString(QUuid::WithoutBraces));
        for (const ListDefinition &def : fragment.lists) {
            const ListDefinition *existing = doc.list(def.id);
            if (existing && *existing == def) {
                listIds.insert(def.id, def.id);
                continue;
            }
            ListDefinition copy = def;
            copy.id = existing ? QUuid::createUuid().toString(QUuid::WithoutBraces) : def.id;
            listIds.insert(def.id, copy.id);
            if (doc.apply(StepList{copy.id, std::nullopt, copy})) return false;
        }
        for (CharFormat format : fragment.formats) {
            if (!format.objectId.isEmpty()) format.objectId = objectIds.value(format.objectId);
            format.styleId = styleIds.value(format.styleId);
            // AGENT-NOTE (docs/references-review.md#moving-annotated-text): a
            // field or comment whose text left the document (cut) anchors
            // again where it is pasted; a copy of text still in place is
            // plain. Change marks never travel: a tracked paste is an insertion.
            if (!reanchored.contains(format.fieldId)) format.fieldId.clear();
            format.commentIds.removeIf([this](const QString &id) { return !reanchored.contains(id); });
            format.deletionId.clear();
            format.formatChangeId.clear();
            format.formatSlot = -1;
            format.insertionId = insertion;
            formats.append(doc.internFormat(format));
        }
        return true;
    }

    int format(int index) const { return formats.value(index, 0); }

    QVector<RichObject> objects(const Paragraph &source) const
    {
        QVector<RichObject> result;
        for (int i = 0; i < source.text.size(); ++i) {
            if (source.text.at(i) != QChar(ObjectChar)) continue;
            const QString id = fragment.formats.value(Spans::formatAt(source.spans, i)).objectId;
            RichObject object = fragment.objects.value(id);
            if (object.id.isEmpty()) continue;
            object.id = objectIds.value(id);
            result.append(object);
        }
        return result;
    }

    ParagraphPtr paragraph(const Paragraph &source, bool inCell) const
    {
        auto p = std::make_shared<Paragraph>();
        p->id = QUuid::createUuid();
        p->text = source.text;
        p->spans = Spans::mapped(source.spans, [this](int f) { return format(f); });
        // A pasted break is new: its mark takes only the paste's insertion.
        CharFormat mark = doc.charFormat(format(source.markFormat));
        mark.fieldId.clear();
        mark.commentIds.clear();
        p->markFormat = doc.internFormat(mark);
        p->format = source.format;
        p->format.listId = listIds.value(source.format.listId, source.format.listId);
        p->format.styleId = styleIds.value(source.format.styleId);
        if (p->format.styleId.isEmpty()) p->format.overrides = 0;
        p->format = materializedFormat(doc.styleSheet(), p->format);
        if (inCell) p->format.pageBreakBefore = false;
        return p;
    }

    TablePtr table(const Table &source) const
    {
        auto t = std::make_shared<Table>(source);
        t->id = QUuid::createUuid();
        t->styleId = styleIds.value(source.styleId);
        // One fresh identity per column, whatever the source carried.
        t->columnIds.clear();
        for (int c = 0; c < t->columnWeights.size(); ++c) t->columnIds.append(QUuid::createUuid());
        for (TableRow &row : t->rows) {
            row.id = QUuid::createUuid();
            for (TableCell &cell : row.cells) {
                cell.id = QUuid::createUuid();
                for (ParagraphPtr &p : cell.paragraphs) p = paragraph(*p, true);
            }
        }
        return t;
    }
};

} // namespace

std::optional<Position> RichTextDocument::insertFragment(const Position &requested, const Fragment &fragment)
{
    if (!isValidPosition(requested)) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Invalid paste position"));
        return std::nullopt;
    }
    const Position at = outsideField(requested); // field results are atomic
    if (fragment.blocks.isEmpty()) return at;
    AutoTransaction transaction(this, tr("Paste"));
    if (!transaction.valid) return std::nullopt;
    FragmentImporter importer{*this, fragment, {}, {}, {}, {}, {}, {}};
    for (auto it = m_annotations.fields.keyBegin(); it != m_annotations.fields.keyEnd(); ++it) importer.reanchored.insert(*it);
    for (auto it = m_annotations.threads.keyBegin(); it != m_annotations.threads.keyEnd(); ++it) importer.reanchored.insert(*it);
    // AGENT-GUARD: text marked deleted by tracking is on its way out, so a
    // field or comment left only there is being moved; the paste anchors
    // it again (a tracked cut and paste keeps its identity, D-116).
    if (!importer.reanchored.isEmpty())
        for (const Paragraph *p : paragraphsInOrder())
            for (const Span &span : p->spans) {
                const CharFormat &format = charFormat(span.format);
                if (!format.deletionId.isEmpty()) continue;
                importer.reanchored.remove(format.fieldId);
                for (const QString &id : format.commentIds) importer.reanchored.remove(id);
            }
    const std::optional<QString> insertion = trackedInsertionId(at);
    if (!insertion) return transaction.finish(false, at), std::nullopt;
    importer.insertion = *insertion;
    const ScopedValue hint(m_insertionHint, *insertion);
    if (!importer.prepare()) return transaction.finish(false, at), std::nullopt;
    const bool inCell = paragraph(at.paragraph)->table;
    const Block &single = fragment.blocks.first();
    if (fragment.blocks.size() == 1 && single.paragraph && fragment.openStart && fragment.openEnd) {
        const ParagraphPtr source = importer.paragraph(*single.paragraph, inCell);
        const Paragraph *target = paragraph(at.paragraph);
        const Position caret{at.paragraph, at.offset + int(source->text.size())};
        const bool ok = !apply(textStep(*this, *target, at.offset, 0, source->text, source->spans,
                                        importer.objects(*single.paragraph)));
        if (!transaction.finish(ok, caret)) return std::nullopt;
        return caret;
    }
    const auto tailStart = splitParagraph(at);
    if (!tailStart) return transaction.finish(false, at), std::nullopt;
    // Enter at a paragraph start keeps the text in place and opens a new
    // paragraph above, so the head is whichever paragraph precedes the tail.
    const QUuid tailId = at.offset == 0 && !paragraph(at.paragraph)->text.isEmpty()
        ? at.paragraph : tailStart->paragraph;
    Paragraph *head = previousParagraph(paragraph(tailId));
    QVector<Block> middle = fragment.blocks;
    bool ok = true;
    if (fragment.openStart && middle.first().paragraph) {
        const Paragraph &source = *middle.first().paragraph;
        const ParagraphPtr copy = importer.paragraph(source, inCell);
        ok = !apply(textStep(*this, *head, int(head->text.size()), 0, copy->text, copy->spans,
                             importer.objects(source)));
        middle.removeFirst();
    }
    Position caret{tailId, 0};
    if (ok && fragment.openEnd && !middle.isEmpty() && middle.last().paragraph) {
        const Paragraph &source = *middle.last().paragraph;
        const ParagraphPtr copy = importer.paragraph(source, inCell);
        const Paragraph *tail = paragraph(tailId);
        ok = !apply(textStep(*this, *tail, 0, 0, copy->text, copy->spans, importer.objects(source)));
        caret.offset = int(copy->text.size());
        middle.removeLast();
    }
    QVector<Block> blocks;
    QVector<RichObject> objects;
    for (const Block &block : std::as_const(middle)) {
        if (block.paragraph) {
            blocks.append(Block{importer.paragraph(*block.paragraph, inCell), {}});
            objects += importer.objects(*block.paragraph);
            continue;
        }
        // Tables do not nest: inside a cell, a pasted table becomes paragraphs.
        for (const TableRow &row : block.table->rows)
            for (const TableCell &cell : row.cells)
                for (const ParagraphPtr &p : cell.paragraphs) {
                    if (inCell) blocks.append(Block{importer.paragraph(*p, true), {}});
                    objects += importer.objects(*p);
                }
        if (!inCell) blocks.append(Block{{}, importer.table(*block.table)});
    }
    if (ok && !blocks.isEmpty()) {
        paragraphsInOrder();
        int index = head->blockIndex + 1;
        if (head->table) {
            const auto &cell = head->table->rows.at(head->row).cells.at(head->cell).paragraphs;
            index = int(std::find_if(cell.begin(), cell.end(), [head](const ParagraphPtr &p) {
                return p.get() == head; }) - cell.begin()) + 1;
        }
        ok = !apply(StepBlocks{containerOf(head), index, blocks, objects, true});
    }
    if (!transaction.finish(ok, caret)) return std::nullopt;
    return caret;
}

QString RichTextDocument::plainText(const Position &a, const Position &b) const
{
    if (!isValidPosition(a) || !isValidPosition(b)) return {};
    const bool ordered = compare(a, b) <= 0;
    const Position start = ordered ? a : b;
    const Position end = ordered ? b : a;
    const Paragraph *first = paragraph(start.paragraph);
    const Paragraph *last = paragraph(end.paragraph);
    const auto &flat = paragraphsInOrder();
    QString out;
    const Paragraph *previous = nullptr;
    for (int i = first->flatIndex; i <= last->flatIndex; ++i) {
        const Paragraph *p = flat.at(i);
        if (previous) {
            const bool sameRow = p->table && p->table == previous->table && p->row == previous->row
                && p->cell != previous->cell;
            out += sameRow ? u'\t' : u'\n';
        }
        const int from = p == first ? start.offset : 0;
        const int to = p == last ? end.offset : int(p->text.size());
        QString text = p->text.mid(from, to - from);
        text.remove(QChar(ObjectChar));
        text.replace(QChar(LineBreakChar), QChar(u'\n'));
        out += text;
        previous = p;
    }
    return out;
}

} // namespace QindaTK
