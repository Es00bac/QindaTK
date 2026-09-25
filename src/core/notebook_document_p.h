// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "notebook_document.h"
#include "rich_text_types.h"
#include <QJsonArray>

// Internal helpers of NotebookDocument: the flat block tree and the inline
// text runs of text-like blocks. Pure functions over plain values.
namespace QindaTK::NotebookTree {

QString newId();
// Depth of every block (0 = top level); parents precede children.
QVector<int> depths(const QVector<Notebook::Block> &blocks);
// One past the last descendant of blocks[index].
int subtreeEnd(const QVector<Notebook::Block> &blocks, int index);
// Flat indices of the direct children of `parentId` ("" = top level).
QVector<int> children(const QVector<Notebook::Block> &blocks, const QString &parentId);
int indexOf(const QVector<Notebook::Block> &blocks, const QString &id);
// Flat position where a new child of `parentId` at sibling `index` goes
// (index < 0 or past the end appends). -1 when the parent is unknown.
int insertionPoint(const QVector<Notebook::Block> &blocks, const QString &parentId, int index);
// AGENT-GUARD: every structural edit ends with repair(). A block whose parent
// is missing or no longer encloses it is lifted to the nearest enclosing
// ancestor recorded in `original` (or to the top level), so the flat
// depth-first invariant decodeNative checks always holds after an edit.
void repair(QVector<Notebook::Block> *blocks, const QVector<Notebook::Block> &original = {});
// Empty when the order satisfies the invariant; otherwise the problem.
QString validate(const QVector<Notebook::Block> &blocks);
// True when a block in [from, to) is read-only. Every deletion of more than
// its target (a subtree, a page, a section) checks the whole span.
inline bool anyLocked(const QVector<Notebook::Block> &blocks, int from = 0, int to = -1)
{
    if (to < 0) to = int(blocks.size());
    for (int i = from; i < to; ++i) if (blocks.at(i).locked) return true;
    return false;
}

} // namespace QindaTK::NotebookTree

namespace QindaTK::NotebookText {

// text, heading, quote, list, todo and code hold editable text; all but code
// also hold inline formatting runs.
bool isTextType(const QString &type);
bool hasRuns(const QString &type);
bool isKnownType(const QString &type);

// AGENT-NOTE: runs reuse the rich-text span algebra (RichText::Spans, WP-050):
// `spans` index `formats`, whose entry 0 is the plain format {}. A format is a
// small object of bold/italic/underline/strike/code booleans and an href.
struct Runs {
    QString text;
    QVector<RichText::Span> spans;
    QVector<QJsonObject> formats{QJsonObject{}};
};
Runs read(const QJsonObject &data);
void write(QJsonObject *data, const Runs &runs);
Runs slice(const Runs &runs, int offset, int length);
Runs concat(const Runs &first, const Runs &second);
// Inserted text takes the format just before `offset` (or at it, at 0).
Runs replace(const Runs &runs, int offset, int removed, const QString &text);
// Patch keys set to false or "" clear that attribute.
Runs format(const Runs &runs, int offset, int length, const QJsonObject &patch);
QString validateRuns(const QJsonObject &data);
// Snaps to the nearest grapheme boundary at or before `offset`.
int snap(const QString &text, int offset);
// Default data of a new block, merged with `data`, bounded and normalized.
QJsonObject normalized(const QString &type, const QJsonObject &data, QString *problem);
// Plain text of any block (table cells tab/newline separated), for search
// and the text/plain clipboard projection.
QString plainText(const Notebook::Block &block);

} // namespace QindaTK::NotebookText
