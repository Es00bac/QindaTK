// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_html.h"
#include "rich_text_spans.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

namespace QindaTK::RichTextHtml {
using namespace RichText;

namespace {

QString escaped(const QString &text) { return text.toHtmlEscaped(); }

QString spanStyle(const CharFormat &f)
{
    QStringList css;
    if (!f.family.isEmpty()) css << QStringLiteral("font-family:'%1'").arg(f.family);
    if (f.sizePt > 0) css << QStringLiteral("font-size:%1pt").arg(f.sizePt);
    if (f.weight > 0) css << QStringLiteral("font-weight:%1").arg(f.weight);
    if (f.italic == 1) css << QStringLiteral("font-style:italic");
    QStringList decoration;
    if (f.underline == 1) decoration << QStringLiteral("underline");
    if (f.strike == 1) decoration << QStringLiteral("line-through");
    if (!decoration.isEmpty()) css << QStringLiteral("text-decoration:") + decoration.join(u' ');
    if (f.color.isValid()) css << QStringLiteral("color:") + f.color.name(QColor::HexRgb);
    if (f.background.isValid()) css << QStringLiteral("background-color:") + f.background.name(QColor::HexRgb);
    if (f.verticalAlign == VerticalAlign::Superscript) css << QStringLiteral("vertical-align:super");
    if (f.verticalAlign == VerticalAlign::Subscript) css << QStringLiteral("vertical-align:sub");
    return css.join(u';');
}

QString inlineHtml(const Fragment &fragment, const Paragraph &p)
{
    QString html;
    int start = 0;
    for (const Span &span : p.spans) {
        const CharFormat &f = fragment.formats.value(span.format);
        QString piece;
        for (const QChar c : p.text.mid(start, span.length)) {
            if (c == QChar(LineBreakChar)) piece += QStringLiteral("<br>");
            else if (c == QChar(ObjectChar)) {
                const RichObject object = fragment.objects.value(f.objectId);
                piece += QStringLiteral("<img src=\"%1\" alt=\"%2\" width=\"%3\" height=\"%4\">")
                             .arg(escaped(object.source), escaped(object.altText))
                             .arg(qRound(object.widthPt * PxPerPt)).arg(qRound(object.heightPt * PxPerPt));
            } else {
                piece += escaped(QString(c));
            }
        }
        const QString style = spanStyle(f);
        if (!style.isEmpty()) piece = QStringLiteral("<span style=\"%1\">%2</span>").arg(style, piece);
        if (!f.href.isEmpty()) piece = QStringLiteral("<a href=\"%1\">%2</a>").arg(escaped(f.href), piece);
        html += piece;
        start += span.length;
    }
    return html;
}

QString blockStyle(const ParagraphFormat &f)
{
    static const char *align[] = {"start", "end", "left", "right", "center", "justify"};
    QStringList css;
    if (f.alignment != Alignment::Start) css << QStringLiteral("text-align:%1").arg(QLatin1String(align[int(f.alignment)]));
    if (f.direction == Direction::RightToLeft) css << QStringLiteral("direction:rtl");
    if (f.spaceBeforePt > 0) css << QStringLiteral("margin-top:%1pt").arg(f.spaceBeforePt);
    if (f.spaceAfterPt > 0) css << QStringLiteral("margin-bottom:%1pt").arg(f.spaceAfterPt);
    if (f.indentStartPt != 0) css << QStringLiteral("margin-inline-start:%1pt").arg(f.indentStartPt);
    return css.isEmpty() ? QString() : QStringLiteral(" style=\"%1\"").arg(css.join(u';'));
}

QString paragraphHtml(const Fragment &fragment, const Paragraph &p)
{
    const QString tag = p.format.headingLevel > 0 ? QStringLiteral("h%1").arg(p.format.headingLevel) : QStringLiteral("p");
    return QStringLiteral("<%1%2>%3</%1>").arg(tag, blockStyle(p.format), inlineHtml(fragment, p));
}

} // namespace

QString fromFragment(const Fragment &fragment, const QString &title)
{
    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\">");
    if (!title.isEmpty()) html += QStringLiteral("<title>%1</title>").arg(escaped(title));
    html += QStringLiteral("</head><body>");
    QString openList;
    auto closeList = [&] {
        if (!openList.isEmpty()) html += QStringLiteral("</%1>").arg(openList);
        openList.clear();
    };
    for (const Block &block : fragment.blocks) {
        if (block.table) {
            closeList();
            html += QStringLiteral("<table border=\"1\" style=\"border-collapse:collapse\">");
            for (int r = 0; r < block.table->rows.size(); ++r) {
                html += QStringLiteral("<tr>");
                const QString cellTag = r < block.table->headerRows ? QStringLiteral("th") : QStringLiteral("td");
                for (const TableCell &cell : block.table->rows.at(r).cells) {
                    html += QStringLiteral("<%1>").arg(cellTag);
                    for (const ParagraphPtr &p : cell.paragraphs) html += paragraphHtml(fragment, *p);
                    html += QStringLiteral("</%1>").arg(cellTag);
                }
                html += QStringLiteral("</tr>");
            }
            html += QStringLiteral("</table>");
            continue;
        }
        const Paragraph &p = *block.paragraph;
        const ListDefinition list = fragment.lists.value(p.format.listId);
        if (list.levels.isEmpty()) {
            closeList();
            html += paragraphHtml(fragment, p);
            continue;
        }
        const QString tag = isListKindOrdered(list.levels.value(p.format.listLevel).kind) ? QStringLiteral("ol") : QStringLiteral("ul");
        if (tag != openList) {
            closeList();
            html += QStringLiteral("<%1>").arg(tag);
            openList = tag;
        }
        html += QStringLiteral("<li%1>%2</li>").arg(blockStyle(p.format), inlineHtml(fragment, p));
    }
    closeList();
    return html + QStringLiteral("</body></html>");
}

namespace {

struct Importer {
    Fragment fragment;
    QHash<CharFormat, int> formatIndex;
    QHash<const QTextList *, QString> listIds;

    int intern(const CharFormat &format)
    {
        if (const auto found = formatIndex.constFind(format); found != formatIndex.constEnd()) return *found;
        const int index = int(fragment.formats.size());
        fragment.formats.append(format);
        formatIndex.insert(format, index);
        return index;
    }

    CharFormat charFormat(const QTextCharFormat &cf) const
    {
        CharFormat f;
        const QStringList families = cf.fontFamilies().toStringList();
        if (!families.isEmpty()) f.family = families.first();
        if (cf.hasProperty(QTextFormat::FontPointSize) && cf.fontPointSize() > 0) f.sizePt = cf.fontPointSize();
        if (cf.hasProperty(QTextFormat::FontWeight) && cf.fontWeight() != QFont::Normal) f.weight = cf.fontWeight();
        if (cf.fontItalic()) f.italic = 1;
        if (cf.fontUnderline() && !cf.isAnchor()) f.underline = 1;
        if (cf.fontStrikeOut()) f.strike = 1;
        if (cf.hasProperty(QTextFormat::ForegroundBrush) && !cf.isAnchor()) f.color = cf.foreground().color();
        if (cf.hasProperty(QTextFormat::BackgroundBrush)) f.background = cf.background().color();
        if (cf.isAnchor()) f.href = cf.anchorHref();
        if (cf.verticalAlignment() == QTextCharFormat::AlignSuperScript) f.verticalAlign = VerticalAlign::Superscript;
        if (cf.verticalAlignment() == QTextCharFormat::AlignSubScript) f.verticalAlign = VerticalAlign::Subscript;
        return f;
    }

    ParagraphPtr paragraph(const QTextBlock &block, bool inCell)
    {
        auto p = std::make_shared<Paragraph>();
        p->id = QUuid::createUuid();
        const QTextBlockFormat bf = block.blockFormat();
        const Qt::Alignment align = bf.alignment();
        p->format.alignment = align & Qt::AlignHCenter ? Alignment::Center : align & Qt::AlignJustify ? Alignment::Justify
            : align & Qt::AlignRight ? (align & Qt::AlignAbsolute ? Alignment::Right : Alignment::End) : Alignment::Start;
        if (bf.hasProperty(QTextFormat::LayoutDirection))
            p->format.direction = bf.layoutDirection() == Qt::RightToLeft ? Direction::RightToLeft : Direction::LeftToRight;
        p->format.headingLevel = qBound(0, bf.headingLevel(), 6);
        p->format.spaceBeforePt = qMax<qreal>(0, bf.topMargin() / PxPerPt);
        p->format.spaceAfterPt = qMax<qreal>(0, bf.bottomMargin() / PxPerPt);
        if (const QTextList *list = block.textList(); list && !inCell) {
            QString &id = listIds[list];
            if (id.isEmpty()) {
                id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                ListKind kind = ListKind::Disc;
                switch (list->format().style()) {
                case QTextListFormat::ListCircle: kind = ListKind::Circle; break;
                case QTextListFormat::ListSquare: kind = ListKind::Square; break;
                case QTextListFormat::ListDecimal: kind = ListKind::Decimal; break;
                case QTextListFormat::ListLowerAlpha: kind = ListKind::LowerAlpha; break;
                case QTextListFormat::ListUpperAlpha: kind = ListKind::UpperAlpha; break;
                case QTextListFormat::ListLowerRoman: kind = ListKind::LowerRoman; break;
                case QTextListFormat::ListUpperRoman: kind = ListKind::UpperRoman; break;
                default: break;
                }
                ListDefinition def{id, {}};
                for (int level = 0; level < 9; ++level) def.levels.append({kind, 1, 18.0});
                fragment.lists.insert(id, def);
            }
            p->format.listId = id;
            p->format.listLevel = qBound(0, list->format().indent() - 1, 8);
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment piece = it.fragment();
            const QTextCharFormat cf = piece.charFormat();
            CharFormat f = charFormat(cf);
            QString text = piece.text();
            for (int i = 0; i < text.size(); ++i) {
                if (text.at(i) == QChar::ObjectReplacementCharacter && cf.isImageFormat()) {
                    const QTextImageFormat image = cf.toImageFormat();
                    RichObject object;
                    object.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    object.source = image.name();
                    object.widthPt = image.width() > 0 ? image.width() / PxPerPt : 72;
                    object.heightPt = image.height() > 0 ? image.height() / PxPerPt : 72;
                    fragment.objects.insert(object.id, object);
                    CharFormat objectFormat = f;
                    objectFormat.objectId = object.id;
                    p->text += QChar(ObjectChar);
                    p->spans.append({1, intern(objectFormat)});
                } else if (text.at(i) != QChar::ObjectReplacementCharacter) {
                    const QChar c = text.at(i);
                    if (c.unicode() < 0x20 && c != u'\t') continue;
                    p->text += c == QChar::ParagraphSeparator ? QChar(LineBreakChar) : c;
                    p->spans.append({1, intern(f)});
                }
            }
        }
        p->spans = Spans::normalized(p->spans);
        return p;
    }

    void frame(QTextFrame *frame, QVector<Block> &out, bool inCell)
    {
        for (auto it = frame->begin(); !it.atEnd(); ++it) {
            if (QTextFrame *child = it.currentFrame()) {
                if (auto *table = qobject_cast<QTextTable *>(child); table && !inCell) out.append(Block{{}, this->table(table)});
                else this->frame(child, out, inCell);
            } else if (it.currentBlock().isValid()) {
                out.append(Block{paragraph(it.currentBlock(), inCell), {}});
            }
        }
    }

    TablePtr table(QTextTable *source)
    {
        auto t = std::make_shared<Table>();
        t->id = QUuid::createUuid();
        t->columnWeights = QVector<double>(source->columns(), 1.0);
        for (int c = 0; c < source->columns(); ++c) t->columnIds.append(QUuid::createUuid());
        t->headerRows = qBound(0, source->format().headerRowCount(), qMax(0, source->rows() - 1));
        for (int r = 0; r < source->rows(); ++r) {
            TableRow row{QUuid::createUuid(), {}, 0};
            for (int c = 0; c < source->columns(); ++c) {
                TableCell cell{QUuid::createUuid(), {}, {}};
                const QTextTableCell sourceCell = source->cellAt(r, c);
                // Merged cells (WP-051) are flattened: content stays in the anchor cell.
                if (sourceCell.row() == r && sourceCell.column() == c) {
                    QVector<Block> blocks;
                    for (auto it = sourceCell.begin(); !it.atEnd(); ++it) {
                        if (QTextFrame *nested = it.currentFrame()) frame(nested, blocks, true);
                        else if (it.currentBlock().isValid()) blocks.append(Block{paragraph(it.currentBlock(), true), {}});
                    }
                    for (const Block &block : std::as_const(blocks)) cell.paragraphs.append(block.paragraph);
                }
                if (cell.paragraphs.isEmpty()) {
                    auto empty = std::make_shared<Paragraph>();
                    empty->id = QUuid::createUuid();
                    cell.paragraphs.append(empty);
                }
                row.cells.append(cell);
            }
            t->rows.append(row);
        }
        return t;
    }
};

} // namespace

Fragment toFragment(const QString &html)
{
    QTextDocument document;
    document.setHtml(html);
    Importer importer;
    importer.fragment.formats.append(CharFormat{});
    importer.formatIndex.insert(CharFormat{}, 0);
    importer.frame(document.rootFrame(), importer.fragment.blocks, false);
    Fragment &fragment = importer.fragment;
    fragment.openStart = !fragment.blocks.isEmpty() && fragment.blocks.first().paragraph;
    fragment.openEnd = !fragment.blocks.isEmpty() && fragment.blocks.last().paragraph;
    return fragment;
}

Fragment fromPlainText(const QString &text)
{
    Fragment fragment;
    fragment.formats.append(CharFormat{});
    QString clean = text;
    clean.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    for (const QString &line : clean.split(u'\n')) {
        auto p = std::make_shared<Paragraph>();
        p->id = QUuid::createUuid();
        p->text = line;
        p->text.remove(QChar(ObjectChar));
        if (!p->text.isEmpty()) p->spans.append({int(p->text.size()), 0});
        fragment.blocks.append(Block{p, {}});
    }
    return fragment;
}

} // namespace QindaTK::RichTextHtml
