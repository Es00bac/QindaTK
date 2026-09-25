// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notebook_document_p.h"
#include "rich_text_html.h"
#include "rich_text_spans.h"

// AGENT-NOTE: notebooks exchange formatted text through the rich-text HTML
// codec (docs/publishing.md#clipboard): a selection becomes a RichText
// Fragment for fromFragment(), and pasted HTML is parsed by toFragment()
// into text, heading, list, table and image blocks.
namespace QindaTK {
using namespace Notebook;

namespace {
RichText::CharFormat charFormat(const QJsonObject &format)
{
    RichText::CharFormat result;
    if (format.value(QStringLiteral("bold")).toBool()) result.weight = 700;
    if (format.value(QStringLiteral("italic")).toBool()) result.italic = 1;
    if (format.value(QStringLiteral("underline")).toBool()) result.underline = 1;
    if (format.value(QStringLiteral("strike")).toBool()) result.strike = 1;
    if (format.value(QStringLiteral("code")).toBool()) result.family = QStringLiteral("monospace");
    result.href = format.value(QStringLiteral("href")).toString();
    return result;
}

QJsonObject runFormat(const RichText::CharFormat &format)
{
    QJsonObject result;
    if (format.weight >= 600) result.insert(QStringLiteral("bold"), true);
    if (format.italic == 1) result.insert(QStringLiteral("italic"), true);
    if (format.underline == 1) result.insert(QStringLiteral("underline"), true);
    if (format.strike == 1) result.insert(QStringLiteral("strike"), true);
    if (format.family.contains(QLatin1String("mono"), Qt::CaseInsensitive))
        result.insert(QStringLiteral("code"), true);
    if (!format.href.isEmpty()) result.insert(QStringLiteral("href"), format.href);
    return result;
}

int internFormat(RichText::Fragment *fragment, const RichText::CharFormat &format)
{
    const int found = int(fragment->formats.indexOf(format));
    if (found >= 0) return found;
    fragment->formats.append(format);
    return int(fragment->formats.size() - 1);
}

RichText::ParagraphPtr paragraph(RichText::Fragment *fragment, const NotebookText::Runs &runs, bool code)
{
    auto p = std::make_shared<RichText::Paragraph>();
    p->id = QUuid::createUuid();
    p->text = runs.text;
    p->text.replace(u'\n', QChar(RichText::LineBreakChar));
    for (const RichText::Span &span : runs.spans) {
        RichText::CharFormat format = charFormat(runs.formats.value(span.format));
        if (code) format.family = QStringLiteral("monospace");
        p->spans.append({span.length, internFormat(fragment, format)});
    }
    p->spans = RichText::Spans::normalized(p->spans);
    return p;
}

RichText::TablePtr table(RichText::Fragment *fragment, const Block &block)
{
    auto t = std::make_shared<RichText::Table>();
    t->id = QUuid::createUuid();
    const QJsonArray columns = block.data.value(QStringLiteral("columns")).toArray();
    QVector<QJsonArray> rows{columns};
    for (const QJsonValue &row : block.data.value(QStringLiteral("rows")).toArray()) rows.append(row.toArray());
    for (int c = 0; c < columns.size(); ++c) {
        t->columnWeights.append(1.0);
        t->columnIds.append(QUuid::createUuid());
    }
    t->headerRows = 1;
    for (const QJsonArray &cells : std::as_const(rows)) {
        RichText::TableRow row{QUuid::createUuid(), {}, 0};
        for (int c = 0; c < columns.size(); ++c) {
            NotebookText::Runs runs;
            runs.text = cells.at(c).toString();
            if (!runs.text.isEmpty()) runs.spans.append({int(runs.text.size()), 0});
            row.cells.append({QUuid::createUuid(), {paragraph(fragment, runs, false)}, {}, 1, 1});
        }
        t->rows.append(row);
    }
    return t;
}

// Text of a paragraph without object anchors, with soft breaks as newlines.
NotebookText::Runs runsOf(const RichText::Fragment &fragment, const RichText::Paragraph &p)
{
    NotebookText::Runs runs;
    QVector<RichText::Span> spans;
    int position = 0;
    for (const RichText::Span &span : p.spans) {
        const QJsonObject format = runFormat(fragment.formats.value(span.format));
        int index = int(runs.formats.indexOf(format));
        if (index < 0) { runs.formats.append(format); index = int(runs.formats.size() - 1); }
        for (int i = position; i < position + span.length && i < p.text.size(); ++i) {
            const QChar c = p.text.at(i);
            if (c == QChar(RichText::ObjectChar)) continue;
            runs.text += c == QChar(RichText::LineBreakChar) ? QChar(u'\n') : c;
            spans.append({1, index});
        }
        position += span.length;
    }
    runs.spans = RichText::Spans::normalized(spans);
    return runs;
}
} // namespace

QString NotebookDocument::selectionHtml(const QVariantMap &selection) const
{
    const QVector<Block> blocks = selectedBlocks(selection, nullptr, nullptr, nullptr);
    if (blocks.isEmpty()) return {};
    RichText::Fragment fragment;
    fragment.formats.append(RichText::CharFormat{});
    for (const Block &block : blocks) {
        if (block.type == QLatin1String("table")) {
            fragment.blocks.append({{}, table(&fragment, block)});
            continue;
        }
        NotebookText::Runs runs;
        if (NotebookText::isTextType(block.type)) {
            runs = NotebookText::read(block.data);
        } else {
            runs.text = NotebookText::plainText(block);
            if (!runs.text.isEmpty()) runs.spans.append({int(runs.text.size()), 0});
        }
        auto p = paragraph(&fragment, runs, block.type == QLatin1String("code"));
        if (block.type == QLatin1String("heading"))
            p->format.headingLevel = qBound(1, block.data.value(QStringLiteral("level")).toInt(), 6);
        if (block.type == QLatin1String("list")) {
            const bool ordered = block.data.value(QStringLiteral("ordered")).toBool();
            const QString id = ordered ? QStringLiteral("notebook-numbers") : QStringLiteral("notebook-bullets");
            fragment.lists.insert(id, {id, {RichText::ListLevel{ordered ? RichText::ListKind::Decimal
                                                                       : RichText::ListKind::Disc, 1, 18.0}}});
            p->format.listId = id;
        }
        fragment.blocks.append({p, {}});
    }
    return RichTextHtml::fromFragment(fragment, m_state.title);
}

QVariantMap NotebookDocument::insertHtml(const QString &pageId, const QString &blockId, int offset,
                                         const QString &html)
{
    if (html.size() > 8 * 1024 * 1024) { fail(QStringLiteral("Pasted HTML exceeds 8 Mi characters")); return {}; }
    const RichText::Fragment fragment = RichTextHtml::toFragment(html);
    QVector<Block> blocks;
    for (const RichText::Block &source : fragment.blocks) {
        if (source.table) {
            QVector<QJsonArray> rows;
            for (const RichText::TableRow &row : source.table->rows) {
                QJsonArray cells;
                for (const RichText::TableCell &cell : row.cells) {
                    QStringList texts;
                    for (const RichText::ParagraphPtr &p : cell.paragraphs) texts.append(runsOf(fragment, *p).text);
                    cells.append(texts.join(u'\n'));
                }
                rows.append(cells);
            }
            if (rows.isEmpty()) continue;
            QJsonArray columns;
            QJsonArray body;
            const bool header = source.table->headerRows > 0;
            for (int c = 0; c < rows.first().size(); ++c)
                columns.append(header ? rows.first().at(c).toString() : tr("Column %1").arg(c + 1));
            for (int r = header ? 1 : 0; r < rows.size(); ++r) body.append(rows.at(r));
            QString problem;
            const QJsonObject data = NotebookText::normalized(QStringLiteral("table"),
                QJsonObject{{QStringLiteral("columns"), columns}, {QStringLiteral("rows"), body}}, &problem);
            if (problem.isEmpty()) blocks.append({NotebookTree::newId(), QStringLiteral("table"), {}, data, false});
            continue;
        }
        const RichText::Paragraph &p = *source.paragraph;
        Block block{NotebookTree::newId(), QStringLiteral("text"), {}, {}, false};
        if (p.format.headingLevel > 0) {
            block.type = QStringLiteral("heading");
            block.data.insert(QStringLiteral("level"), qBound(1, p.format.headingLevel, 6));
        } else if (!p.format.listId.isEmpty()) {
            const auto levels = fragment.lists.value(p.format.listId).levels;
            block.type = QStringLiteral("list");
            block.data.insert(QStringLiteral("ordered"), !levels.isEmpty()
                && RichText::isListKindOrdered(levels.value(qBound(0, p.format.listLevel, int(levels.size()) - 1)).kind));
        }
        NotebookText::write(&block.data, runsOf(fragment, p));
        blocks.append(block);
        for (const RichText::Span &span : p.spans) {
            const QString objectId = fragment.formats.value(span.format).objectId;
            if (objectId.isEmpty() || !fragment.objects.contains(objectId)) continue;
            const RichText::RichObject object = fragment.objects.value(objectId);
            blocks.append({NotebookTree::newId(), QStringLiteral("image"), {},
                           QJsonObject{{QStringLiteral("resourceId"), object.source},
                                       {QStringLiteral("caption"), object.altText}}, false});
        }
    }
    if (blocks.isEmpty()) return caret(blockId, offset);
    const bool openStart = fragment.openStart && NotebookText::isTextType(blocks.first().type);
    const bool openEnd = fragment.openEnd && NotebookText::isTextType(blocks.last().type);
    return insertBlocksAt(pageId, blockId, offset, blocks, openStart, openEnd, tr("Paste formatted text"));
}

} // namespace QindaTK
