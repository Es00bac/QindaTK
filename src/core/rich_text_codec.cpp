// SPDX-License-Identifier: LGPL-3.0-or-later
// Canonical encoder of the qindatk.richtext schema (decoder: rich_text_codec_read.cpp).
#include "rich_text_codec.h"
#include "rich_text_codec_p.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <functional>

namespace QindaTK {
using namespace RichText;

namespace RichText::Codec {

QJsonObject charFormatJson(const CharFormat &f)
{
    QJsonObject o;
    if (!f.family.isEmpty()) o.insert(QStringLiteral("family"), f.family);
    if (f.sizePt > 0) o.insert(QStringLiteral("sizePt"), f.sizePt);
    if (f.weight > 0) o.insert(QStringLiteral("weight"), f.weight);
    if (f.italic >= 0) o.insert(QStringLiteral("italic"), f.italic == 1);
    if (f.underline >= 0) o.insert(QStringLiteral("underline"), f.underline == 1);
    if (f.strike >= 0) o.insert(QStringLiteral("strike"), f.strike == 1);
    if (f.color.isValid()) o.insert(QStringLiteral("color"), colorName(f.color));
    if (f.background.isValid()) o.insert(QStringLiteral("background"), colorName(f.background));
    if (!f.href.isEmpty()) o.insert(QStringLiteral("href"), f.href);
    if (f.verticalAlign != VerticalAlign::Normal)
        o.insert(QStringLiteral("verticalAlign"), verticalAlignNames().at(int(f.verticalAlign)));
    if (!f.objectId.isEmpty()) o.insert(QStringLiteral("object"), f.objectId);
    if (!f.styleId.isEmpty()) o.insert(QStringLiteral("style"), f.styleId);
    insertMarks(f, &o);
    return o;
}

QJsonObject paragraphFormatJson(const ParagraphFormat &f)
{
    const ParagraphFormat d;
    QJsonObject o;
    if (f.alignment != d.alignment) o.insert(QStringLiteral("align"), alignmentNames().at(int(f.alignment)));
    if (f.direction != d.direction) o.insert(QStringLiteral("direction"), directionNames().at(int(f.direction)));
    auto number = [&o](const char *key, double value, double fallback) {
        if (value != fallback) o.insert(QLatin1String(key), value);
    };
    number("indentStartPt", f.indentStartPt, d.indentStartPt);
    number("indentEndPt", f.indentEndPt, d.indentEndPt);
    number("firstLineIndentPt", f.firstLineIndentPt, d.firstLineIndentPt);
    number("spaceBeforePt", f.spaceBeforePt, d.spaceBeforePt);
    number("spaceAfterPt", f.spaceAfterPt, d.spaceAfterPt);
    number("lineHeight", f.lineHeight, d.lineHeight);
    number("lineHeightPt", f.lineHeightPt, d.lineHeightPt);
    if (f.headingLevel != 0) o.insert(QStringLiteral("heading"), f.headingLevel);
    if (!f.listId.isEmpty()) o.insert(QStringLiteral("list"), f.listId);
    if (f.listLevel != 0) o.insert(QStringLiteral("listLevel"), f.listLevel);
    if (f.keepWithNext) o.insert(QStringLiteral("keepWithNext"), true);
    if (f.keepTogether) o.insert(QStringLiteral("keepTogether"), true);
    if (f.pageBreakBefore) o.insert(QStringLiteral("pageBreakBefore"), true);
    if (!f.styleId.isEmpty()) o.insert(QStringLiteral("style"), f.styleId);
    if (!f.styleId.isEmpty() && f.overrides)
        o.insert(QStringLiteral("overrides"), QJsonArray::fromStringList(overrideNames(f.overrides)));
    return o;
}

QJsonObject objectJson(const RichObject &object)
{
    QJsonObject o{{QStringLiteral("id"), object.id},
                  {QStringLiteral("kind"), object.kind},
                  {QStringLiteral("placement"), placementNames().at(int(object.placement))},
                  {QStringLiteral("widthPt"), object.widthPt},
                  {QStringLiteral("heightPt"), object.heightPt}};
    if (object.offsetXPt != 0) o.insert(QStringLiteral("offsetXPt"), object.offsetXPt);
    if (object.offsetYPt != 0) o.insert(QStringLiteral("offsetYPt"), object.offsetYPt);
    if (object.gapPt != RichObject{}.gapPt) o.insert(QStringLiteral("gapPt"), object.gapPt);
    if (!object.source.isEmpty()) o.insert(QStringLiteral("source"), object.source);
    if (!object.altText.isEmpty()) o.insert(QStringLiteral("alt"), object.altText);
    return o;
}

QJsonObject pageJson(const PageSetup &page)
{
    QJsonObject o{{QStringLiteral("widthPt"), page.widthPt},
                  {QStringLiteral("heightPt"), page.heightPt},
                  {QStringLiteral("marginsPt"), QJsonArray{page.marginTopPt, page.marginRightPt,
                                                           page.marginBottomPt, page.marginLeftPt}}};
    if (page.columns != PageSetup{}.columns) o.insert(QStringLiteral("columns"), page.columns);
    if (page.columnGapPt != PageSetup{}.columnGapPt) o.insert(QStringLiteral("columnGapPt"), page.columnGapPt);
    if (page.bleedPt != PageSetup{}.bleedPt) o.insert(QStringLiteral("bleedPt"), page.bleedPt); // WP-053
    return o;
}

QJsonObject listJson(const ListDefinition &list)
{
    QJsonArray levels;
    for (const ListLevel &level : list.levels) {
        QJsonObject l{{QStringLiteral("kind"), listKindNames().at(int(level.kind))}};
        if (level.start != 1) l.insert(QStringLiteral("start"), level.start);
        if (level.indentPt != ListLevel{}.indentPt) l.insert(QStringLiteral("indentPt"), level.indentPt);
        levels.append(l);
    }
    return {{QStringLiteral("id"), list.id}, {QStringLiteral("levels"), levels}};
}

} // namespace RichText::Codec

using namespace RichText::Codec;

namespace {

// Walks blocks in document order, re-interning formats by first use.
struct Writer {
    explicit Writer(std::function<const CharFormat &(int)> lookup) : format(std::move(lookup)) {}
    std::function<const CharFormat &(int)> format;
    QHash<int, int> remap;
    QJsonArray formats{QJsonObject{}};
    QHash<CharFormat, int> canonical{{CharFormat{}, 0}};

    int map(int index)
    {
        const auto found = remap.constFind(index);
        if (found != remap.constEnd()) return found.value();
        const CharFormat &value = format(index);
        int mapped = canonical.value(value, -1);
        if (mapped < 0) {
            mapped = int(formats.size());
            formats.append(charFormatJson(value));
            canonical.insert(value, mapped);
        }
        remap.insert(index, mapped);
        return mapped;
    }

    QJsonObject paragraph(const Paragraph &p)
    {
        QJsonArray spans;
        for (const Span &span : p.spans) spans.append(QJsonArray{span.length, map(span.format)});
        QJsonObject o{{QStringLiteral("id"), p.id.toString(QUuid::WithoutBraces)},
                      {QStringLiteral("text"), p.text},
                      {QStringLiteral("spans"), spans},
                      {QStringLiteral("mark"), map(p.markFormat)}};
        const QJsonObject format = paragraphFormatJson(p.format);
        if (!format.isEmpty()) o.insert(QStringLiteral("format"), format);
        return o;
    }

    QJsonObject table(const Table &t)
    {
        QJsonArray columns;
        for (double weight : t.columnWeights) columns.append(weight);
        QJsonArray columnIds;
        for (const QUuid &id : t.columnIds) columnIds.append(id.toString(QUuid::WithoutBraces));
        QJsonArray rows;
        for (const TableRow &row : t.rows) {
            QJsonArray cells;
            for (const TableCell &cell : row.cells) {
                QJsonArray paragraphs;
                for (const ParagraphPtr &p : cell.paragraphs) paragraphs.append(paragraph(*p));
                QJsonObject c{{QStringLiteral("id"), cell.id.toString(QUuid::WithoutBraces)},
                              {QStringLiteral("paragraphs"), paragraphs}};
                if (cell.background.isValid())
                    c.insert(QStringLiteral("background"), colorName(cell.background));
                if (cell.rowSpan > 1) c.insert(QStringLiteral("rowSpan"), cell.rowSpan);
                if (cell.columnSpan > 1) c.insert(QStringLiteral("columnSpan"), cell.columnSpan);
                cells.append(c);
            }
            QJsonObject r{{QStringLiteral("id"), row.id.toString(QUuid::WithoutBraces)},
                          {QStringLiteral("cells"), cells}};
            if (row.minHeightPt > 0) r.insert(QStringLiteral("minHeightPt"), row.minHeightPt);
            rows.append(r);
        }
        QJsonObject o{{QStringLiteral("id"), t.id.toString(QUuid::WithoutBraces)},
                      {QStringLiteral("columns"), columns},
                      {QStringLiteral("columnIds"), columnIds},
                      {QStringLiteral("rows"), rows}};
        if (t.headerRows > 0) o.insert(QStringLiteral("headerRows"), t.headerRows);
        if (t.borderPt != Table{}.borderPt) o.insert(QStringLiteral("borderPt"), t.borderPt);
        if (t.cellPaddingPt != Table{}.cellPaddingPt) o.insert(QStringLiteral("paddingPt"), t.cellPaddingPt);
        if (!t.styleId.isEmpty()) o.insert(QStringLiteral("style"), t.styleId);
        return o;
    }

    QJsonArray blocks(const QVector<Block> &list)
    {
        QJsonArray result;
        for (const Block &block : list) {
            if (block.paragraph) result.append(QJsonObject{{QStringLiteral("paragraph"), paragraph(*block.paragraph)}});
            else result.append(QJsonObject{{QStringLiteral("table"), table(*block.table)}});
        }
        return result;
    }
};

QJsonArray sortedObjects(const QHash<QString, RichObject> &objects)
{
    QStringList ids = objects.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray result;
    for (const QString &id : std::as_const(ids)) result.append(objectJson(objects.value(id)));
    return result;
}

QJsonArray sortedLists(const QHash<QString, ListDefinition> &lists)
{
    QStringList ids = lists.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray result;
    for (const QString &id : std::as_const(ids)) result.append(listJson(lists.value(id)));
    return result;
}

} // namespace

namespace {

bool usesVersion2(const Table &table)
{
    if (!table.styleId.isEmpty()) return true;
    for (const TableRow &row : table.rows)
        for (const TableCell &cell : row.cells)
            if (cell.rowSpan > 1 || cell.columnSpan > 1) return true;
    return false;
}

} // namespace

int RichTextCodec::requiredVersion(const RichTextDocument &document)
{
    const PageSetup page = document.pageSetupValue();
    if (!document.styleSheet().isEmpty() || !document.frameSet().frames.isEmpty()
        || !document.annotations().isEmpty() // WP-052 fields, sources, comments, changes (D-117)
        || page.columns != PageSetup{}.columns || page.columnGapPt != PageSetup{}.columnGapPt
        || page.bleedPt != PageSetup{}.bleedPt)
        return 2;
    for (const Block &block : document.blocks())
        if (block.table && usesVersion2(*block.table)) return 2;
    return 1;
}

QJsonObject RichTextCodec::toJsonObject(const RichTextDocument &document)
{
    // Entry 0 of `formats` is the default format (see Writer's initializers).
    Writer writer([&document](int i) -> const CharFormat & { return document.charFormat(i); });
    const QJsonArray blocks = writer.blocks(document.blocks());
    const PageSetup page = document.pageSetupValue();
    const DocumentDefaults defaults = document.defaults();
    QJsonObject defaultsJson{{QStringLiteral("family"), defaults.family},
                             {QStringLiteral("sizePt"), defaults.sizePt}};
    if (defaults.color.isValid()) defaultsJson.insert(QStringLiteral("color"), colorName(defaults.color));
    // Only lists that paragraphs use are written; unused definitions are dropped.
    QHash<QString, ListDefinition> used;
    for (const Paragraph *p : document.paragraphsInOrder())
        if (const ListDefinition *list = document.list(p->format.listId)) used.insert(list->id, *list);
    QJsonObject root{{QStringLiteral("schema"), schemaId()},
                     {QStringLiteral("version"), requiredVersion(document)},
                     {QStringLiteral("documentId"), document.documentIdString()},
                     {QStringLiteral("page"), pageJson(page)},
                     {QStringLiteral("defaults"), defaultsJson},
                     {QStringLiteral("formats"), writer.formats},
                     {QStringLiteral("lists"), sortedLists(used)},
                     {QStringLiteral("objects"), sortedObjects(document.objects())},
                     {QStringLiteral("blocks"), blocks}};
    // WP-051 members are written only when used (docs/publishing.md#persistence).
    if (!document.styleSheet().isEmpty()) root.insert(QStringLiteral("styles"), stylesJson(document.styleSheet()));
    if (!document.frameSet().frames.isEmpty())
        root.insert(QStringLiteral("textFrames"), frameSetJson(document.frameSet()));
    insertAnnotationMembers(document.annotations(), &root);
    return root;
}

QByteArray RichTextCodec::encode(const RichTextDocument &document)
{
    return QJsonDocument(toJsonObject(document)).toJson(QJsonDocument::Compact);
}

QJsonObject RichTextCodec::fragmentToJson(const Fragment &fragment)
{
    Writer writer([&fragment](int i) -> const CharFormat & {
        static const CharFormat empty;
        return i >= 0 && i < fragment.formats.size() ? fragment.formats.at(i) : empty;
    });
    const QJsonArray blocks = writer.blocks(fragment.blocks);
    bool version2 = !fragment.styles.isEmpty();
    for (const Block &block : fragment.blocks) version2 = version2 || (block.table && usesVersion2(*block.table));
    for (const CharFormat &format : fragment.formats) version2 = version2 || format.hasMarks();
    QJsonObject root{{QStringLiteral("schema"), fragmentSchemaId()},
                     {QStringLiteral("version"), version2 ? 2 : 1},
                     {QStringLiteral("openStart"), fragment.openStart},
                     {QStringLiteral("openEnd"), fragment.openEnd},
                     {QStringLiteral("formats"), writer.formats},
                     {QStringLiteral("lists"), sortedLists(fragment.lists)},
                     {QStringLiteral("objects"), sortedObjects(fragment.objects)},
                     {QStringLiteral("blocks"), blocks}};
    if (!fragment.styles.isEmpty()) root.insert(QStringLiteral("styles"), stylesJson(fragment.styles));
    return root;
}

QVariantMap RichTextCodec::paragraphFormatToMap(const ParagraphFormat &format)
{
    QVariantMap map = paragraphFormatJson(format).toVariantMap();
    map.insert(QStringLiteral("align"), alignmentNames().at(int(format.alignment)));
    map.insert(QStringLiteral("heading"), format.headingLevel);
    return map;
}

QVariantMap RichTextCodec::charFormatToMap(const CharFormat &format)
{
    return charFormatJson(format).toVariantMap();
}

QString RichTextCodec::placementName(Placement placement)
{
    return placementNames().at(int(placement));
}

QString RichTextCodec::listKindName(ListKind kind)
{
    return listKindNames().at(int(kind));
}

std::optional<ListKind> RichTextCodec::listKindFromName(const QString &name)
{
    const qsizetype index = listKindNames().indexOf(name);
    if (index < 0) return std::nullopt;
    return ListKind(index);
}

} // namespace QindaTK
