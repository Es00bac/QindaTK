// SPDX-License-Identifier: LGPL-3.0-or-later
#include "clipboard_service.h"
#include "notebook_document_p.h"
#include "rich_text_graphemes.h"

namespace QindaTK {
using namespace Notebook;

namespace {
constexpr auto RangeFormat = "qindatk.notebook.blocks";

int textLength(const Block &block)
{
    return NotebookText::isTextType(block.type)
        ? int(block.data.value(QStringLiteral("text")).toString().size()) : 0;
}

struct Endpoints {
    int page = -1;
    int first = -1, last = -1;
    int firstOffset = 0, lastOffset = 0;
};
} // namespace

// AGENT-CONTRACT (docs/notebooks.md#selection): a selection is ordered by
// flat block index, then offset. Blocks strictly between the endpoints are
// whole; a text endpoint contributes the part inside the selection and any
// other endpoint is whole. BlockEditor sends these maps unchanged.
static bool resolveSelection(const NotebookDocument &doc, const QVariantMap &selection, Endpoints *out)
{
    const QString anchor = selection.value(QStringLiteral("anchorBlockId")).toString();
    const QString focus = selection.value(QStringLiteral("focusBlockId"), anchor).toString();
    const QString pageId = doc.pageOfBlock(anchor);
    if (pageId.isEmpty() || doc.pageOfBlock(focus) != pageId) return false;
    const auto &pages = doc.state().pages;
    int page = -1;
    for (int i = 0; i < pages.size(); ++i) if (pages.at(i).id == pageId) page = i;
    const auto &blocks = pages.at(page).blocks;
    int a = NotebookTree::indexOf(blocks, anchor), f = NotebookTree::indexOf(blocks, focus);
    auto offsetOf = [&](int index, const char *key) {
        const Block &block = blocks.at(index);
        const QString text = block.data.value(QStringLiteral("text")).toString();
        return NotebookText::isTextType(block.type)
            ? NotebookText::snap(text, selection.value(QLatin1String(key)).toInt()) : 0;
    };
    int ao = offsetOf(a, "anchorOffset"), fo = offsetOf(f, "focusOffset");
    if (f < a || (f == a && fo < ao)) { std::swap(a, f); std::swap(ao, fo); }
    *out = {page, a, f, ao, fo};
    return true;
}

QVector<Block> NotebookDocument::selectedBlocks(const QVariantMap &selection, int *pageOut,
                                                bool *openStart, bool *openEnd) const
{
    Endpoints ends;
    if (!resolveSelection(*this, selection, &ends)) return {};
    const auto &blocks = m_state.pages.at(ends.page).blocks;
    QVector<Block> result;
    for (int i = ends.first; i <= ends.last; ++i) {
        Block block = blocks.at(i);
        if (NotebookText::isTextType(block.type)) {
            const int from = i == ends.first ? ends.firstOffset : 0;
            const int to = i == ends.last ? ends.lastOffset : textLength(block);
            NotebookText::write(&block.data, NotebookText::slice(NotebookText::read(block.data), from, to - from));
            if (!NotebookText::hasRuns(block.type)) block.data.remove(QStringLiteral("runs"));
        }
        result.append(block);
    }
    if (pageOut) *pageOut = ends.page;
    if (openStart) *openStart = NotebookText::isTextType(result.first().type);
    if (openEnd) *openEnd = NotebookText::isTextType(result.last().type);
    return result;
}

QString NotebookDocument::selectionText(const QVariantMap &selection) const
{
    QStringList lines;
    for (const Block &block : selectedBlocks(selection, nullptr, nullptr, nullptr))
        lines.append(NotebookText::plainText(block));
    return lines.join(u'\n');
}

QVariantMap NotebookDocument::selectionRange(const QVariantMap &selection) const
{
    bool openStart = false, openEnd = false;
    const QVector<Block> blocks = selectedBlocks(selection, nullptr, &openStart, &openEnd);
    if (blocks.isEmpty()) return {};
    QVariantList records;
    for (int i = 0; i < blocks.size(); ++i) {
        int parent = -1;
        for (int j = 0; j < i; ++j) if (blocks.at(j).id == blocks.at(i).parentId) parent = j;
        records.append(QVariantMap{{"id", blocks.at(i).id}, {"type", blocks.at(i).type},
                                   {"parent", parent}, {"data", blocks.at(i).data.toVariantMap()}});
    }
    return {{"format", QString::fromLatin1(RangeFormat)}, {"version", 1}, {"openStart", openStart},
            {"openEnd", openEnd}, {"blocks", records}};
}

QVariantMap NotebookDocument::moveCaret(const QString &blockId, int offset, const QString &direction) const
{
    const Location at = locate(blockId);
    if (!at.valid()) return {};
    const auto &blocks = m_state.pages.at(at.page).blocks;
    const Block &block = blocks.at(at.block);
    const QString text = block.data.value(QStringLiteral("text")).toString();
    const int length = textLength(block);
    offset = NotebookText::isTextType(block.type) ? NotebookText::snap(text, offset) : 0;
    auto result = [&](int index, int position, bool moved) {
        return QVariantMap{{"blockId", blocks.at(index).id}, {"offset", position},
                           {"pageId", m_state.pages.at(at.page).id}, {"moved", moved}};
    };
    const bool hasPrevious = at.block > 0, hasNext = at.block + 1 < blocks.size();
    if (direction == QLatin1String("left")) {
        if (offset > 0) return result(at.block, RichText::GraphemeBoundaries(text).previous(offset), true);
        return hasPrevious ? result(at.block - 1, textLength(blocks.at(at.block - 1)), true) : result(at.block, 0, false);
    }
    if (direction == QLatin1String("right")) {
        if (offset < length) return result(at.block, RichText::GraphemeBoundaries(text).next(offset), true);
        return hasNext ? result(at.block + 1, 0, true) : result(at.block, length, false);
    }
    if (direction == QLatin1String("up") || direction == QLatin1String("previous")) {
        if (!hasPrevious) return result(at.block, 0, offset != 0);
        const Block &target = blocks.at(at.block - 1);
        return result(at.block - 1, NotebookText::snap(target.data.value(QStringLiteral("text")).toString(),
                                                        qMin(offset, textLength(target))), true);
    }
    if (direction == QLatin1String("down") || direction == QLatin1String("next")) {
        if (!hasNext) return result(at.block, length, offset != length);
        const Block &target = blocks.at(at.block + 1);
        return result(at.block + 1, NotebookText::snap(target.data.value(QStringLiteral("text")).toString(),
                                                        qMin(offset, textLength(target))), true);
    }
    if (direction == QLatin1String("blockStart")) return result(at.block, 0, offset != 0);
    if (direction == QLatin1String("blockEnd")) return result(at.block, length, offset != length);
    if (direction == QLatin1String("start")) return result(0, 0, at.block != 0 || offset != 0);
    if (direction == QLatin1String("end")) {
        const int last = int(blocks.size()) - 1;
        return result(last, textLength(blocks.at(last)), at.block != last || offset != length);
    }
    return result(at.block, offset, false);
}

QVariantMap NotebookDocument::deleteSelection(const QVariantMap &selection)
{
    Endpoints ends;
    if (!resolveSelection(*this, selection, &ends)) {
        fail(QStringLiteral("The selection must lie inside one page"));
        return {};
    }
    const QVector<Block> &original = m_state.pages.at(ends.page).blocks;
    for (int i = ends.first; i <= ends.last; ++i)
        if (original.at(i).locked) {
            fail(QStringLiteral("The selection contains a read-only block"));
            return {};
        }
    const Block &first = original.at(ends.first);
    const Block &last = original.at(ends.last);
    const bool firstText = NotebookText::isTextType(first.type);
    const bool lastText = NotebookText::isTextType(last.type);
    if (ends.first == ends.last && firstText && ends.firstOffset == ends.lastOffset)
        return caret(first.id, ends.firstOffset);
    State next = m_state;
    QVector<Block> &list = next.pages[ends.page].blocks;
    QString caretId;
    int caretOffset = 0;
    QSet<QString> removed;
    if (ends.first == ends.last) {
        if (firstText) {
            NotebookText::write(&list[ends.first].data, NotebookText::replace(NotebookText::read(first.data),
                ends.firstOffset, ends.lastOffset - ends.firstOffset, {}));
            caretId = first.id;
            caretOffset = ends.firstOffset;
        } else {
            removed.insert(first.id);
        }
    } else {
        for (int i = ends.first + 1; i < ends.last; ++i) removed.insert(original.at(i).id);
        if (firstText) {
            NotebookText::Runs head = NotebookText::slice(NotebookText::read(first.data), 0, ends.firstOffset);
            caretId = first.id;
            caretOffset = ends.firstOffset;
            if (lastText) {
                const NotebookText::Runs tail = NotebookText::read(last.data);
                head = NotebookText::concat(head, NotebookText::slice(tail, ends.lastOffset,
                                                                      int(tail.text.size()) - ends.lastOffset));
                removed.insert(last.id);
                // The surviving children of the merged block follow the first.
                for (Block &block : list) if (block.parentId == last.id) block.parentId = first.id;
            }
            NotebookText::write(&list[ends.first].data, head);
            if (!NotebookText::hasRuns(first.type)) list[ends.first].data.remove(QStringLiteral("runs"));
        } else {
            removed.insert(first.id);
        }
        if (!lastText) removed.insert(last.id);
        else if (!firstText) {
            const NotebookText::Runs tail = NotebookText::read(last.data);
            NotebookText::write(&list[ends.last].data, NotebookText::slice(tail, ends.lastOffset,
                                                                           int(tail.text.size()) - ends.lastOffset));
            if (!NotebookText::hasRuns(last.type)) list[ends.last].data.remove(QStringLiteral("runs"));
            caretId = last.id;
        }
    }
    list.removeIf([&](const Block &block) { return removed.contains(block.id); });
    NotebookTree::repair(&list, original);
    if (caretId.isEmpty()) {
        // Only whole non-text blocks went: the caret lands where they were.
        int index = qMin(ends.first, int(list.size()) - 1);
        if (index >= 0) {
            caretId = list.at(index).id;
            caretOffset = 0;
        }
    }
    if (!commit(next, tr("Delete selection"))) return {};
    return caretId.isEmpty() ? QVariantMap{{"pageId", m_state.pages.at(ends.page).id}}
                             : caret(caretId, caretOffset);
}

bool NotebookDocument::copySelection(ClipboardService *clipboard, const QVariantMap &selection)
{
    if (!clipboard) return fail(QStringLiteral("No clipboard service"));
    const QVariantMap range = selectionRange(selection);
    if (range.isEmpty()) return fail(QStringLiteral("The selection must lie inside one page"));
    if (!clipboard->copyRange(m_documentId, revision(), range, selectionText(selection)))
        return fail(clipboard->lastError());
    clearError();
    return true;
}

QVariantMap NotebookDocument::cutSelection(ClipboardService *clipboard, const QVariantMap &selection)
{
    if (!copySelection(clipboard, selection)) return {};
    return deleteSelection(selection);
}

QVariantMap NotebookDocument::paste(ClipboardService *clipboard, const QString &pageId,
                                    const QString &blockId, int offset)
{
    if (!clipboard) { fail(QStringLiteral("No clipboard service")); return {}; }
    const QVariantMap data = clipboard->rangePasteData();
    const QString kind = data.value(QStringLiteral("kind")).toString();
    const QVariantMap range = data.value(QStringLiteral("range")).toMap();
    if (kind == QLatin1String("range") && range.value(QStringLiteral("format")).toString() == QLatin1String(RangeFormat))
        return insertRange(pageId, blockId, offset, range);
    if (kind == QLatin1String("range") || kind == QLatin1String("text"))
        return insertText(pageId, blockId, offset, data.value(QStringLiteral("text")).toString());
    fail(QStringLiteral("The clipboard holds nothing a notebook can paste"));
    return {};
}

QVariantMap NotebookDocument::insertRange(const QString &pageId, const QString &blockId, int offset,
                                          const QVariantMap &range)
{
    if (range.value(QStringLiteral("format")).toString() != QLatin1String(RangeFormat)
        || range.value(QStringLiteral("version")).toInt() != 1) {
        fail(QStringLiteral("Unsupported notebook range"));
        return {};
    }
    const QVariantList records = range.value(QStringLiteral("blocks")).toList();
    if (records.isEmpty() || records.size() > Limits::MaxBlocksPerPage) {
        fail(QStringLiteral("A notebook range holds 1 to 10,000 blocks"));
        return {};
    }
    // AGENT-GUARD: every pasted block gets a fresh ID; references between
    // pasted blocks (parent, chart source) are rewritten to the new IDs, the
    // same rule PropertyCollectionDropTarget applies to $ref values.
    QHash<QString, QString> renamed;
    QVector<Block> blocks;
    for (const QVariant &value : records) {
        const QVariantMap record = value.toMap();
        Block block{NotebookTree::newId(), record.value(QStringLiteral("type")).toString(), {}, {}, false};
        if (!NotebookText::isKnownType(block.type)) {
            fail(QStringLiteral("Unsupported block type %1 in the range").arg(block.type));
            return {};
        }
        QString problem;
        block.data = NotebookText::normalized(block.type,
            QJsonObject::fromVariantMap(record.value(QStringLiteral("data")).toMap()), &problem);
        if (!problem.isEmpty()) { fail(problem); return {}; }
        const int parent = record.value(QStringLiteral("parent"), -1).toInt();
        if (parent >= 0 && parent < blocks.size()) block.parentId = blocks.at(parent).id;
        renamed.insert(record.value(QStringLiteral("id")).toString(), block.id);
        blocks.append(block);
    }
    for (Block &block : blocks) {
        const QString source = block.data.value(QStringLiteral("sourceBlockId")).toString();
        if (block.type == QLatin1String("chart") && renamed.contains(source))
            block.data.insert(QStringLiteral("sourceBlockId"), renamed.value(source));
    }
    return insertBlocksAt(pageId, blockId, offset, blocks, range.value(QStringLiteral("openStart")).toBool(),
                          range.value(QStringLiteral("openEnd")).toBool(), tr("Paste blocks"));
}

QVariantMap NotebookDocument::insertText(const QString &pageId, const QString &blockId, int offset,
                                         const QString &text)
{
    if (text.size() > 4 * 1024 * 1024) { fail(QStringLiteral("Pasted text exceeds 4 Mi characters")); return {}; }
    QString clean = text;
    clean.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QVector<Block> blocks;
    for (const QString &line : clean.split(u'\n')) {
        Block block{NotebookTree::newId(), QStringLiteral("text"), {}, {}, false};
        block.data.insert(QStringLiteral("text"), line);
        blocks.append(block);
    }
    return insertBlocksAt(pageId, blockId, offset, blocks, true, true, tr("Paste text"));
}

QVariantMap NotebookDocument::insertBlocksAt(const QString &pageId, const QString &blockId, int offset,
                                             QVector<Block> payload, bool openStart, bool openEnd,
                                             const QString &label)
{
    const int pageAt = pageIndex(blockId.isEmpty() ? pageId : pageOfBlock(blockId));
    if (pageAt < 0 || (!pageId.isEmpty() && m_state.pages.at(pageAt).id != pageId)) {
        fail(QStringLiteral("The paste target is not on page %1").arg(pageId));
        return {};
    }
    const QVector<Block> original = m_state.pages.at(pageAt).blocks;
    if (original.size() + payload.size() + 1 > Limits::MaxBlocksPerPage
        || blockCount() + payload.size() + 1 > Limits::MaxBlocks) {
        fail(QStringLiteral("The page or notebook block limit is reached"));
        return {};
    }
    State next = m_state;
    QVector<Block> &list = next.pages[pageAt].blocks;
    const int targetAt = blockId.isEmpty() ? -1 : NotebookTree::indexOf(list, blockId);
    if (targetAt >= 0 && list.at(targetAt).locked) { fail(QStringLiteral("Block %1 is read only").arg(blockId)); return {}; }
    const QString parent = targetAt >= 0 ? list.at(targetAt).parentId : QString();
    const bool textTarget = targetAt >= 0 && NotebookText::isTextType(list.at(targetAt).type);
    QString tailId;
    int position = targetAt >= 0 ? NotebookTree::subtreeEnd(list, targetAt) : int(list.size());
    NotebookText::Runs head;
    if (textTarget) {
        // Split the target at the caret; the tail keeps the target's children.
        Block &target = list[targetAt];
        const NotebookText::Runs runs = NotebookText::read(target.data);
        const int at = NotebookText::snap(runs.text, offset);
        head = NotebookText::slice(runs, 0, at);
        Block tail{NotebookTree::newId(), target.type == QLatin1String("heading") ? QStringLiteral("text") : target.type,
                   parent, {}, false};
        NotebookText::write(&tail.data, NotebookText::slice(runs, at, int(runs.text.size()) - at));
        if (tail.type == target.type)
            for (const QString &key : target.data.keys())
                if (!tail.data.contains(key) && key != QLatin1String("runs")) tail.data.insert(key, target.data.value(key));
        NotebookText::write(&target.data, head);
        tailId = tail.id;
        for (Block &block : list) if (block.parentId == blockId) block.parentId = tailId;
        list.insert(targetAt + 1, tail);
        position = targetAt + 1;
    }
    QStringList inserted;
    for (int i = 0; i < payload.size(); ++i) {
        if (payload.at(i).parentId.isEmpty()) payload[i].parentId = parent;
        list.insert(position + i, payload.at(i));
        inserted.append(payload.at(i).id);
    }
    auto indexOf = [&list](const QString &id) { return NotebookTree::indexOf(list, id); };
    auto mergeInto = [&](const QString &intoId, const QString &fromId) {
        Block &into = list[indexOf(intoId)];
        const Block from = list.at(indexOf(fromId));
        NotebookText::write(&into.data, NotebookText::concat(NotebookText::read(into.data), NotebookText::read(from.data)));
        if (!NotebookText::hasRuns(into.type)) into.data.remove(QStringLiteral("runs"));
        for (Block &block : list) if (block.parentId == fromId) block.parentId = intoId;
        list.removeAt(indexOf(fromId));
    };
    if (textTarget && openStart && NotebookText::isTextType(payload.first().type)) {
        mergeInto(blockId, inserted.takeFirst());
    }
    QString caretId = inserted.isEmpty() ? (textTarget ? blockId : QString()) : inserted.last();
    int caretOffset = caretId.isEmpty() ? 0 : textLength(list.at(indexOf(caretId)));
    if (textTarget) {
        const bool tailEmpty = textLength(list.at(indexOf(tailId))) == 0;
        if (openEnd && NotebookText::isTextType(list.at(indexOf(caretId)).type)) {
            mergeInto(caretId, tailId);
        } else if (tailEmpty) {
            QString heir = caretId;
            for (const QString &id : std::as_const(inserted))
                if (list.at(indexOf(id)).parentId == parent) heir = id;
            for (Block &block : list) if (block.parentId == tailId) block.parentId = heir;
            list.removeAt(indexOf(tailId));
        }
    }
    NotebookTree::repair(&list, original);
    if (!commit(next, label)) return {};
    return caretId.isEmpty() ? QVariantMap{{"pageId", m_state.pages.at(pageAt).id}} : caret(caretId, caretOffset);
}

} // namespace QindaTK
