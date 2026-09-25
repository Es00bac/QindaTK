// SPDX-License-Identifier: LGPL-3.0-or-later
// Validating decoder of the qindatk.richtext schema.
#include "rich_text_codec.h"
#include "rich_text_codec_p.h"
#include "rich_text_spans.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Codec;

namespace {

struct Reader {
    OperationError error;
    QStringList warnings;
    QSet<QUuid> ids;
    QVector<CharFormat> formats;
    QHash<QString, RichObject> objects;
    QHash<QString, ListDefinition> lists;
    StyleSheet styles;
    Annotations annotations;
    bool fragment = false;          // marks are kept for the pasting document to check
    QSet<QString> anchoredObjects;
    int paragraphs = 0;

    bool styleOfKind(const QString &id, StyleKind kind) const
    {
        const auto found = styles.constFind(id);
        return found != styles.constEnd() && found->kind == kind;
    }

    bool fail(const QString &path, const QString &message)
    {
        if (!error) error = {ErrorCode::InvalidArgument, message, path, QStringLiteral("richtext")};
        return false;
    }

    void checkKeys(const QJsonObject &o, const QString &path, std::initializer_list<const char *> known)
    {
        for (auto it = o.begin(); it != o.end(); ++it) {
            bool found = false;
            for (const char *key : known) found = found || it.key() == QLatin1String(key);
            if (!found) warnings.append(QStringLiteral("%1.%2 ignored").arg(path, it.key()));
        }
    }

    bool number(const QJsonObject &o, const char *key, double *out, double min, double max,
                const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isDouble() || !(v.toDouble() >= min && v.toDouble() <= max))
            return fail(path + u'.' + QLatin1String(key), QStringLiteral("Number out of range"));
        *out = v.toDouble();
        return true;
    }

    bool integer(const QJsonObject &o, const char *key, int *out, int min, int max, const QString &path)
    {
        double value = *out;
        if (!number(o, key, &value, min, max, path)) return false;
        if (value != std::floor(value)) return fail(path + u'.' + QLatin1String(key), QStringLiteral("Integer expected"));
        *out = int(value);
        return true;
    }

    bool color(const QJsonObject &o, const char *key, QColor *out, const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const QColor c = QColor::fromString(v.toString());
        if (!v.isString() || !c.isValid() || !v.toString().startsWith(u'#'))
            return fail(path + u'.' + QLatin1String(key), QStringLiteral("Invalid colour"));
        *out = c;
        return true;
    }

    template <typename Enum>
    bool name(const QJsonObject &o, const char *key, const QStringList &names, Enum *out, const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const qsizetype index = names.indexOf(v.toString());
        if (!v.isString() || index < 0) return fail(path + u'.' + QLatin1String(key), QStringLiteral("Unknown value"));
        *out = Enum(index);
        return true;
    }

    bool flag(const QJsonObject &o, const char *key, bool *out, const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isBool()) return fail(path + u'.' + QLatin1String(key), QStringLiteral("Boolean expected"));
        *out = v.toBool();
        return true;
    }

    bool uuid(const QJsonValue &v, QUuid *out, const QString &path)
    {
        const QUuid id(v.toString());
        if (!v.isString() || id.isNull()) return fail(path, QStringLiteral("Invalid identifier"));
        if (ids.contains(id)) return fail(path, QStringLiteral("Duplicate identifier"));
        ids.insert(id);
        *out = id;
        return true;
    }

    bool charFormat(const QJsonValue &v, int index, CharFormat *out)
    {
        const QString path = QStringLiteral("formats[%1]").arg(index);
        if (!v.isObject()) return fail(path, QStringLiteral("Object expected"));
        const QJsonObject o = v.toObject();
        checkKeys(o, path, {"family", "sizePt", "weight", "italic", "underline", "strike", "color",
                            "background", "href", "verticalAlign", "object", "style", "field", "comments",
                            "insertion", "deletion"});
        CharFormat f;
        f.family = o.value(QStringLiteral("family")).toString();
        f.href = o.value(QStringLiteral("href")).toString();
        f.objectId = o.value(QStringLiteral("object")).toString();
        f.styleId = o.value(QStringLiteral("style")).toString();
        // AGENT-NOTE: a session's live encoding writes the whole format pool,
        // which can keep an unused format naming a style whose creation was
        // undone (text in use always names an existing style). Such a
        // reference is dropped with a warning instead of refusing the file.
        if (!f.styleId.isEmpty() && !styleOfKind(f.styleId, StyleKind::Character)) {
            warnings.append(QStringLiteral("%1.style ignored: unknown character style").arg(path));
            f.styleId.clear();
        }
        PublishingError marks;
        if (!readMarks(o, fragment ? nullptr : &annotations, &f, path, &marks, &warnings))
            return fail(marks.path, marks.message);
        bool italic = false, underline = false, strike = false;
        const bool ok = number(o, "sizePt", &f.sizePt, 1, 1638, path)
            && integer(o, "weight", &f.weight, 1, 1000, path)
            && flag(o, "italic", &italic, path) && flag(o, "underline", &underline, path)
            && flag(o, "strike", &strike, path) && color(o, "color", &f.color, path)
            && color(o, "background", &f.background, path)
            && name(o, "verticalAlign", verticalAlignNames(), &f.verticalAlign, path);
        if (!ok) return false;
        f.italic = o.contains(QStringLiteral("italic")) ? qint8(italic) : qint8(-1);
        f.underline = o.contains(QStringLiteral("underline")) ? qint8(underline) : qint8(-1);
        f.strike = o.contains(QStringLiteral("strike")) ? qint8(strike) : qint8(-1);
        *out = f;
        return true;
    }

    bool paragraphFormat(const QJsonObject &o, ParagraphFormat *f, const QString &path)
    {
        checkKeys(o, path, {"align", "direction", "indentStartPt", "indentEndPt", "firstLineIndentPt",
                            "spaceBeforePt", "spaceAfterPt", "lineHeight", "lineHeightPt", "heading",
                            "list", "listLevel", "keepWithNext", "keepTogether", "pageBreakBefore", "style",
                            "overrides"});
        f->listId = o.value(QStringLiteral("list")).toString();
        f->styleId = o.value(QStringLiteral("style")).toString();
        if (!f->styleId.isEmpty() && !styleOfKind(f->styleId, StyleKind::Paragraph))
            return fail(path + QStringLiteral(".style"), QStringLiteral("Unknown paragraph style"));
        const std::optional<quint32> overrides = overrideBits(o.value(QStringLiteral("overrides")));
        if (!overrides) return fail(path + QStringLiteral(".overrides"), QStringLiteral("Unknown overridden property"));
        f->overrides = f->styleId.isEmpty() ? 0 : *overrides;
        return name(o, "align", alignmentNames(), &f->alignment, path)
            && name(o, "direction", directionNames(), &f->direction, path)
            && number(o, "indentStartPt", &f->indentStartPt, -1440, 1440, path)
            && number(o, "indentEndPt", &f->indentEndPt, -1440, 1440, path)
            && number(o, "firstLineIndentPt", &f->firstLineIndentPt, -1440, 1440, path)
            && number(o, "spaceBeforePt", &f->spaceBeforePt, 0, 1440, path)
            && number(o, "spaceAfterPt", &f->spaceAfterPt, 0, 1440, path)
            && number(o, "lineHeight", &f->lineHeight, 0.25, 10, path)
            && number(o, "lineHeightPt", &f->lineHeightPt, 0, 1440, path)
            && integer(o, "heading", &f->headingLevel, 0, 6, path)
            && integer(o, "listLevel", &f->listLevel, 0, 8, path)
            && flag(o, "keepWithNext", &f->keepWithNext, path)
            && flag(o, "keepTogether", &f->keepTogether, path)
            && flag(o, "pageBreakBefore", &f->pageBreakBefore, path);
    }

    ParagraphPtr paragraph(const QJsonValue &v, const QString &path, bool inCell)
    {
        if (++paragraphs > RichTextCodec::MaximumParagraphs) return fail(path, QStringLiteral("Too many paragraphs")), nullptr;
        const QJsonObject o = v.toObject();
        if (!v.isObject()) return fail(path, QStringLiteral("Paragraph object expected")), nullptr;
        checkKeys(o, path, {"id", "text", "spans", "mark", "format"});
        auto p = std::make_shared<Paragraph>();
        if (!uuid(o.value(QStringLiteral("id")), &p->id, path + QStringLiteral(".id"))) return nullptr;
        const QJsonValue text = o.value(QStringLiteral("text"));
        if (!text.isString() || text.toString().size() > RichTextCodec::MaximumParagraphLength)
            return fail(path + QStringLiteral(".text"), QStringLiteral("Invalid text")), nullptr;
        p->text = text.toString();
        for (int i = 0; i < p->text.size(); ++i) {
            const QChar c = p->text.at(i);
            const bool paired = c.isHighSurrogate() ? i + 1 < p->text.size() && p->text.at(i + 1).isLowSurrogate()
                : c.isLowSurrogate() ? i > 0 && p->text.at(i - 1).isHighSurrogate() : true;
            if (!paired || c == u'\n' || c == u'\r' || c == QChar::ParagraphSeparator)
                return fail(path + QStringLiteral(".text"), QStringLiteral("Invalid character")), nullptr;
        }
        for (const QJsonValue &span : o.value(QStringLiteral("spans")).toArray()) {
            const QJsonArray pair = span.toArray();
            const int length = pair.at(0).toInt(-1);
            const int format = pair.at(1).toInt(-1);
            if (pair.size() != 2 || length <= 0 || format < 0 || format >= formats.size())
                return fail(path + QStringLiteral(".spans"), QStringLiteral("Invalid span")), nullptr;
            p->spans.append({length, format});
        }
        p->spans = Spans::normalized(p->spans);
        if (Spans::totalLength(p->spans) != p->text.size())
            return fail(path + QStringLiteral(".spans"), QStringLiteral("Spans do not cover the text")), nullptr;
        p->markFormat = o.value(QStringLiteral("mark")).toInt(0);
        if (p->markFormat < 0 || p->markFormat >= formats.size()
            || !formats.at(p->markFormat).objectId.isEmpty())
            return fail(path + QStringLiteral(".mark"), QStringLiteral("Invalid mark format")), nullptr;
        if (!paragraphFormat(o.value(QStringLiteral("format")).toObject(), &p->format, path + QStringLiteral(".format")))
            return nullptr;
        // A styled paragraph takes its style's values except where overridden.
        p->format = materializedFormat(styles, p->format);
        if (!p->format.listId.isEmpty() && !lists.contains(p->format.listId))
            return fail(path + QStringLiteral(".format.list"), QStringLiteral("Unknown list")), nullptr;
        if (inCell && p->format.pageBreakBefore)
            return fail(path + QStringLiteral(".format"), QStringLiteral("Page break inside a table")), nullptr;
        for (int i = 0; i < p->text.size(); ++i) {
            const QString objectId = formats.at(Spans::formatAt(p->spans, i)).objectId;
            const bool anchor = p->text.at(i) == QChar(ObjectChar);
            if (anchor != !objectId.isEmpty())
                return fail(path + QStringLiteral(".text"), QStringLiteral("Object anchor mismatch")), nullptr;
            if (!anchor) continue;
            const auto object = objects.constFind(objectId);
            if (object == objects.constEnd() || anchoredObjects.contains(objectId)
                || (inCell && object->placement != Placement::Inline))
                return fail(path + QStringLiteral(".text"), QStringLiteral("Invalid object anchor")), nullptr;
            anchoredObjects.insert(objectId);
        }
        return p;
    }

    TablePtr table(const QJsonValue &v, const QString &path)
    {
        const QJsonObject o = v.toObject();
        checkKeys(o, path, {"id", "columns", "columnIds", "rows", "headerRows", "borderPt", "paddingPt", "style"});
        auto t = std::make_shared<Table>();
        if (!uuid(o.value(QStringLiteral("id")), &t->id, path + QStringLiteral(".id"))) return nullptr;
        t->styleId = o.value(QStringLiteral("style")).toString();
        if (!t->styleId.isEmpty() && !styleOfKind(t->styleId, StyleKind::Table))
            return fail(path + QStringLiteral(".style"), QStringLiteral("Unknown table style")), nullptr;
        for (const QJsonValue &weight : o.value(QStringLiteral("columns")).toArray()) {
            if (!(weight.toDouble() > 0 && weight.toDouble() <= 1e6))
                return fail(path + QStringLiteral(".columns"), QStringLiteral("Invalid column weight")), nullptr;
            t->columnWeights.append(weight.toDouble());
        }
        if (t->columnWeights.isEmpty() || t->columnWeights.size() > 64)
            return fail(path + QStringLiteral(".columns"), QStringLiteral("Invalid column count")), nullptr;
        if (o.contains(QStringLiteral("columnIds"))) {
            const QJsonArray columnIds = o.value(QStringLiteral("columnIds")).toArray();
            if (columnIds.size() != t->columnWeights.size())
                return fail(path + QStringLiteral(".columnIds"), QStringLiteral("Invalid column identity count")), nullptr;
            for (int c = 0; c < columnIds.size(); ++c) {
                QUuid id;
                if (!uuid(columnIds.at(c), &id, path + QStringLiteral(".columnIds[%1]").arg(c))) return nullptr;
                t->columnIds.append(id);
            }
        } else {
            // Native files written before columnIds existed: derive stable
            // identities on load; the next save stores them.
            for (int c = 0; c < t->columnWeights.size(); ++c) {
                const QUuid id = derivedColumnId(t->id, c);
                if (ids.contains(id)) return fail(path + QStringLiteral(".columnIds"), QStringLiteral("Derived column identity collision")), nullptr;
                ids.insert(id);
                t->columnIds.append(id);
            }
        }
        if (!integer(o, "headerRows", &t->headerRows, 0, 10000, path)
            || !number(o, "borderPt", &t->borderPt, 0, 72, path)
            || !number(o, "paddingPt", &t->cellPaddingPt, 0, 144, path))
            return nullptr;
        const QJsonArray rows = o.value(QStringLiteral("rows")).toArray();
        for (int r = 0; r < rows.size(); ++r) {
            const QString rowPath = path + QStringLiteral(".rows[%1]").arg(r);
            const QJsonObject row = rows.at(r).toObject();
            checkKeys(row, rowPath, {"id", "cells", "minHeightPt"});
            TableRow tableRow;
            if (!uuid(row.value(QStringLiteral("id")), &tableRow.id, rowPath + QStringLiteral(".id"))
                || !number(row, "minHeightPt", &tableRow.minHeightPt, 0, 14400, rowPath))
                return nullptr;
            const QJsonArray cells = row.value(QStringLiteral("cells")).toArray();
            if (cells.size() != t->columnWeights.size())
                return fail(rowPath + QStringLiteral(".cells"), QStringLiteral("Cell count differs from columns")), nullptr;
            for (int c = 0; c < cells.size(); ++c) {
                const QString cellPath = rowPath + QStringLiteral(".cells[%1]").arg(c);
                const QJsonObject cell = cells.at(c).toObject();
                checkKeys(cell, cellPath, {"id", "paragraphs", "background", "rowSpan", "columnSpan"});
                TableCell tableCell;
                if (!uuid(cell.value(QStringLiteral("id")), &tableCell.id, cellPath + QStringLiteral(".id"))
                    || !color(cell, "background", &tableCell.background, cellPath)
                    || !integer(cell, "rowSpan", &tableCell.rowSpan, 1, 10000, cellPath)
                    || !integer(cell, "columnSpan", &tableCell.columnSpan, 1, 64, cellPath))
                    return nullptr;
                const QJsonArray paragraphs = cell.value(QStringLiteral("paragraphs")).toArray();
                for (int i = 0; i < paragraphs.size(); ++i) {
                    ParagraphPtr p = paragraph(paragraphs.at(i), cellPath + QStringLiteral(".paragraphs[%1]").arg(i), true);
                    if (!p) return nullptr;
                    tableCell.paragraphs.append(p);
                }
                // An empty cell is valid only where a merged cell covers it (checked below).
                tableRow.cells.append(tableCell);
            }
            t->rows.append(tableRow);
        }
        if (t->rows.isEmpty() || t->headerRows >= t->rows.size())
            return fail(path + QStringLiteral(".rows"), QStringLiteral("Invalid rows")), nullptr;
        TableGridMap map;
        if (const QString problem = mapTableGrid(*t, &map); !problem.isEmpty())
            return fail(path + QStringLiteral(".rows"), problem), nullptr;
        return t;
    }

    bool common(const QJsonObject &root)
    {
        // Styles come first: formats, paragraphs and tables refer to them.
        const QJsonValue styleArray = root.value(QStringLiteral("styles"));
        if (!styleArray.isUndefined()) {
            PublishingError problem;
            if (!styleArray.isArray()) return fail(QStringLiteral("styles"), QStringLiteral("Array expected"));
            if (!stylesFromJson(styleArray.toArray(), &styles, &problem, &warnings))
                return fail(problem.path, problem.message);
        }
        // WP-052 records come before the formats whose marks name them.
        if (PublishingError problem; !fragment && !annotationsFromJson(root, &annotations, &problem, &warnings))
            return fail(problem.path, problem.message);
        const QJsonArray formatArray = root.value(QStringLiteral("formats")).toArray();
        if (formatArray.isEmpty() || !formatArray.first().toObject().isEmpty()
            || formatArray.size() > RichTextCodec::MaximumFormats)
            return fail(QStringLiteral("formats"), QStringLiteral("Format table must start with {}"));
        for (int i = 0; i < formatArray.size(); ++i) {
            CharFormat f;
            if (!charFormat(formatArray.at(i), i, &f)) return false;
            formats.append(f);
        }
        const QJsonArray listArray = root.value(QStringLiteral("lists")).toArray();
        for (int i = 0; i < listArray.size(); ++i) {
            const QString path = QStringLiteral("lists[%1]").arg(i);
            const QJsonObject o = listArray.at(i).toObject();
            ListDefinition def;
            def.id = o.value(QStringLiteral("id")).toString();
            const QJsonArray levels = o.value(QStringLiteral("levels")).toArray();
            if (def.id.isEmpty() || lists.contains(def.id) || levels.isEmpty() || levels.size() > 9)
                return fail(path, QStringLiteral("Invalid list definition"));
            for (const QJsonValue &levelValue : levels) {
                const QJsonObject level = levelValue.toObject();
                ListLevel entry;
                if (!name(level, "kind", listKindNames(), &entry.kind, path)
                    || !integer(level, "start", &entry.start, 0, 1000000, path)
                    || !number(level, "indentPt", &entry.indentPt, 0, 720, path))
                    return false;
                def.levels.append(entry);
            }
            lists.insert(def.id, def);
        }
        const QJsonArray objectArray = root.value(QStringLiteral("objects")).toArray();
        if (objectArray.size() > RichTextCodec::MaximumObjects)
            return fail(QStringLiteral("objects"), QStringLiteral("Too many objects"));
        for (int i = 0; i < objectArray.size(); ++i) {
            const QString path = QStringLiteral("objects[%1]").arg(i);
            const QJsonObject o = objectArray.at(i).toObject();
            checkKeys(o, path, {"id", "kind", "placement", "widthPt", "heightPt", "offsetXPt",
                                "offsetYPt", "gapPt", "source", "alt"});
            RichObject object;
            object.id = o.value(QStringLiteral("id")).toString();
            object.kind = o.value(QStringLiteral("kind")).toString(object.kind);
            object.source = o.value(QStringLiteral("source")).toString();
            object.altText = o.value(QStringLiteral("alt")).toString();
            if (object.id.isEmpty() || objects.contains(object.id)
                || !name(o, "placement", placementNames(), &object.placement, path)
                || !number(o, "widthPt", &object.widthPt, 0.01, 14400, path)
                || !number(o, "heightPt", &object.heightPt, 0.01, 14400, path)
                || !number(o, "offsetXPt", &object.offsetXPt, -14400, 14400, path)
                || !number(o, "offsetYPt", &object.offsetYPt, -14400, 14400, path)
                || !number(o, "gapPt", &object.gapPt, 0, 720, path))
                return fail(path, QStringLiteral("Invalid object"));
            objects.insert(object.id, object);
        }
        return true;
    }

    bool blocks(const QJsonArray &array, QVector<Block> *out)
    {
        for (int i = 0; i < array.size(); ++i) {
            const QString path = QStringLiteral("blocks[%1]").arg(i);
            const QJsonObject o = array.at(i).toObject();
            if (o.contains(QStringLiteral("paragraph"))) {
                ParagraphPtr p = paragraph(o.value(QStringLiteral("paragraph")), path + QStringLiteral(".paragraph"), false);
                if (!p) return false;
                out->append(Block{p, {}});
            } else if (o.contains(QStringLiteral("table"))) {
                TablePtr t = table(o.value(QStringLiteral("table")), path + QStringLiteral(".table"));
                if (!t) return false;
                out->append(Block{{}, t});
            } else {
                return fail(path, QStringLiteral("Unknown block"));
            }
        }
        for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
            if (!anchoredObjects.contains(it.key()))
                return fail(QStringLiteral("objects"), QStringLiteral("Object without an anchor: %1").arg(it.key()));
        return true;
    }
};

bool setError(OperationError *error, const OperationError &value)
{
    if (error) *error = value;
    return false;
}

} // namespace

bool RichTextCodec::decode(const QByteArray &bytes, RichTextDocument &document, OperationError *error,
                           QStringList *warnings)
{
    if (bytes.size() > MaximumBytes)
        return setError(error, {ErrorCode::InvalidArgument, QStringLiteral("Document exceeds the size limit"), {}, QStringLiteral("richtext")});
    QJsonParseError parse;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject())
        return setError(error, {ErrorCode::InvalidArgument, QStringLiteral("Malformed JSON: %1").arg(parse.errorString()), {}, QStringLiteral("richtext")});
    return fromJsonObject(json.object(), document, error, warnings);
}

bool RichTextCodec::fromJsonObject(const QJsonObject &root, RichTextDocument &document,
                                   OperationError *error, QStringList *warnings)
{
    Reader reader;
    if (root.value(QStringLiteral("schema")).toString() != schemaId()
        || root.value(QStringLiteral("version")).toInt() < 1
        || root.value(QStringLiteral("version")).toInt() > CurrentVersion)
        return setError(error, {ErrorCode::Unsupported, QStringLiteral("Unsupported rich-text schema or version"), QStringLiteral("schema"), QStringLiteral("richtext")});
    reader.checkKeys(root, QStringLiteral("root"), {"schema", "version", "documentId", "page", "defaults",
                                                    "formats", "lists", "objects", "blocks", "styles",
                                                    "textFrames", "fields", "sources", "comments", "changes"});
    PageSetup page;
    const QJsonObject pageJson = root.value(QStringLiteral("page")).toObject();
    const QJsonArray margins = pageJson.value(QStringLiteral("marginsPt")).toArray();
    DocumentDefaults defaults;
    const QJsonObject defaultsJson = root.value(QStringLiteral("defaults")).toObject();
    defaults.family = defaultsJson.value(QStringLiteral("family")).toString(defaults.family);
    QVector<Block> blocks;
    const bool ok = reader.number(pageJson, "widthPt", &page.widthPt, 72, 14400, QStringLiteral("page"))
        && reader.number(pageJson, "heightPt", &page.heightPt, 72, 14400, QStringLiteral("page"))
        && reader.integer(pageJson, "columns", &page.columns, 1, 24, QStringLiteral("page"))
        && reader.number(pageJson, "columnGapPt", &page.columnGapPt, 0, 1440, QStringLiteral("page"))
        && reader.number(pageJson, "bleedPt", &page.bleedPt, 0, 72, QStringLiteral("page"))
        && (margins.isEmpty() || margins.size() == 4 || reader.fail(QStringLiteral("page.marginsPt"), QStringLiteral("Four margins expected")))
        && reader.number(defaultsJson, "sizePt", &defaults.sizePt, 1, 1638, QStringLiteral("defaults"))
        && reader.color(defaultsJson, "color", &defaults.color, QStringLiteral("defaults"))
        && reader.common(root) && reader.blocks(root.value(QStringLiteral("blocks")).toArray(), &blocks);
    if (!ok) return setError(error, reader.error);
    if (margins.size() == 4) {
        page.marginTopPt = margins.at(0).toDouble(-1);
        page.marginRightPt = margins.at(1).toDouble(-1);
        page.marginBottomPt = margins.at(2).toDouble(-1);
        page.marginLeftPt = margins.at(3).toDouble(-1);
    }
    if (page.marginTopPt < 0 || page.marginRightPt < 0 || page.marginBottomPt < 0
        || page.marginLeftPt < 0 || page.contentWidthPt() < 36 || page.contentHeightPt() < 36
        || page.columnWidthPt() < 36)
        return setError(error, {ErrorCode::InvalidArgument, QStringLiteral("Page content area is too small"), QStringLiteral("page"), QStringLiteral("richtext")});
    FrameSet frames;
    if (root.contains(QStringLiteral("textFrames"))) {
        PublishingError problem;
        if (!root.value(QStringLiteral("textFrames")).isObject()
            || !frameSetFromJson(root.value(QStringLiteral("textFrames")).toObject(), &frames, &problem, &reader.warnings))
            return setError(error, {ErrorCode::InvalidArgument, problem.message.isEmpty() ? QStringLiteral("Object expected") : problem.message,
                                    problem.path.isEmpty() ? QStringLiteral("textFrames") : problem.path, QStringLiteral("richtext")});
    }
    if (blocks.isEmpty() || !blocks.last().paragraph)
        return setError(error, {ErrorCode::InvalidArgument, QStringLiteral("The body must end with a paragraph"), QStringLiteral("blocks"), QStringLiteral("richtext")});
    const DocumentId id = DocumentId::parse(root.value(QStringLiteral("documentId")).toString());
    document.resetContent(std::move(blocks), reader.formats, reader.lists, reader.objects, page,
                          defaults, id.isValid() ? id : DocumentId::create(), reader.styles, frames,
                          reader.annotations);
    if (warnings) *warnings = reader.warnings;
    return true;
}

std::optional<Fragment> RichTextCodec::fragmentFromJson(const QJsonObject &root, OperationError *error)
{
    Reader reader;
    if (root.value(QStringLiteral("schema")).toString() != fragmentSchemaId()
        || root.value(QStringLiteral("version")).toInt() < 1
        || root.value(QStringLiteral("version")).toInt() > CurrentVersion) {
        setError(error, {ErrorCode::Unsupported, QStringLiteral("Unsupported fragment schema"), QStringLiteral("schema"), QStringLiteral("richtext")});
        return std::nullopt;
    }
    Fragment fragment;
    reader.fragment = true;
    if (!reader.common(root) || !reader.blocks(root.value(QStringLiteral("blocks")).toArray(), &fragment.blocks)
        || fragment.blocks.isEmpty()) {
        setError(error, reader.error ? reader.error : OperationError{ErrorCode::InvalidArgument, QStringLiteral("Empty fragment"), {}, QStringLiteral("richtext")});
        return std::nullopt;
    }
    fragment.formats = reader.formats;
    fragment.objects = reader.objects;
    fragment.lists = reader.lists;
    fragment.styles = reader.styles;
    fragment.openStart = root.value(QStringLiteral("openStart")).toBool(true) && fragment.blocks.first().paragraph;
    fragment.openEnd = root.value(QStringLiteral("openEnd")).toBool(true) && fragment.blocks.last().paragraph;
    return fragment;
}

} // namespace QindaTK
