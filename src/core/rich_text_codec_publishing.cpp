// SPDX-License-Identifier: LGPL-3.0-or-later
// qindatk.richtext members added by WP-051: named styles, paragraph
// overrides and text frames (merged cells are written with the table).
#include "rich_text_codec_p.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace QindaTK::RichText::Codec {

namespace {

// Typed readers of optional members; the first failure wins.
struct Parse {
    PublishingError *error = nullptr;
    QStringList *warnings = nullptr;

    bool fail(const QString &path, const QString &message)
    {
        if (error && error->message.isEmpty()) *error = {message, path};
        return false;
    }

    void known(const QJsonObject &o, const QString &path, std::initializer_list<const char *> keys)
    {
        if (!warnings) return;
        for (auto it = o.begin(); it != o.end(); ++it) {
            bool found = false;
            for (const char *key : keys) found = found || it.key() == QLatin1String(key);
            if (!found) warnings->append(QStringLiteral("%1.%2 ignored").arg(path, it.key()));
        }
    }

    static QString at(const QString &path, const char *key) { return path + u'.' + QLatin1String(key); }

    bool number(const QJsonObject &o, const char *key, std::optional<double> *out, double min, double max,
                const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isDouble() || !(v.toDouble() >= min && v.toDouble() <= max))
            return fail(at(path, key), QStringLiteral("Number out of range"));
        *out = v.toDouble();
        return true;
    }

    bool integer(const QJsonObject &o, const char *key, std::optional<int> *out, int min, int max,
                 const QString &path)
    {
        std::optional<double> value;
        if (!number(o, key, &value, min, max, path)) return false;
        if (!value) return true;
        if (*value != std::floor(*value)) return fail(at(path, key), QStringLiteral("Integer expected"));
        *out = int(*value);
        return true;
    }

    bool flag(const QJsonObject &o, const char *key, std::optional<bool> *out, const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isBool()) return fail(at(path, key), QStringLiteral("Boolean expected"));
        *out = v.toBool();
        return true;
    }

    bool tri(const QJsonObject &o, const char *key, std::optional<qint8> *out, const QString &path)
    {
        std::optional<bool> value;
        if (!flag(o, key, &value, path)) return false;
        if (value) *out = qint8(*value);
        return true;
    }

    bool color(const QJsonObject &o, const char *key, std::optional<QColor> *out, const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const QColor c = QColor::fromString(v.toString());
        if (!v.isString() || !c.isValid() || !v.toString().startsWith(u'#'))
            return fail(at(path, key), QStringLiteral("Invalid colour"));
        *out = c;
        return true;
    }

    template <typename Enum>
    bool name(const QJsonObject &o, const char *key, const QStringList &names, std::optional<Enum> *out,
              const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const qsizetype index = names.indexOf(v.toString());
        if (!v.isString() || index < 0) return fail(at(path, key), QStringLiteral("Unknown value"));
        *out = Enum(index);
        return true;
    }

    bool object(const QJsonObject &o, const char *key, QJsonObject *out, const QString &path)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isObject()) return fail(at(path, key), QStringLiteral("Object expected"));
        *out = v.toObject();
        return true;
    }

    bool character(const QJsonObject &c, CharFormatPatch *patch, const QString &path)
    {
        known(c, path, {"family", "sizePt", "weight", "italic", "underline", "strike", "color",
                        "background", "verticalAlign"});
        const QJsonValue family = c.value(QStringLiteral("family"));
        if (!family.isUndefined()) {
            if (!family.isString() || family.toString().isEmpty())
                return fail(at(path, "family"), QStringLiteral("Invalid font family"));
            patch->family = family.toString();
        }
        return number(c, "sizePt", &patch->sizePt, 1, 1638, path)
            && integer(c, "weight", &patch->weight, 1, 1000, path)
            && tri(c, "italic", &patch->italic, path) && tri(c, "underline", &patch->underline, path)
            && tri(c, "strike", &patch->strike, path) && color(c, "color", &patch->color, path)
            && color(c, "background", &patch->background, path)
            && name(c, "verticalAlign", verticalAlignNames(), &patch->verticalAlign, path);
    }

    bool paragraph(const QJsonObject &p, ParagraphFormatPatch *patch, const QString &path)
    {
        known(p, path, {"align", "direction", "indentStartPt", "indentEndPt", "firstLineIndentPt",
                        "spaceBeforePt", "spaceAfterPt", "lineHeight", "lineHeightPt", "heading",
                        "keepWithNext", "keepTogether"});
        return name(p, "align", alignmentNames(), &patch->alignment, path)
            && name(p, "direction", directionNames(), &patch->direction, path)
            && number(p, "indentStartPt", &patch->indentStartPt, -1440, 1440, path)
            && number(p, "indentEndPt", &patch->indentEndPt, -1440, 1440, path)
            && number(p, "firstLineIndentPt", &patch->firstLineIndentPt, -1440, 1440, path)
            && number(p, "spaceBeforePt", &patch->spaceBeforePt, 0, 1440, path)
            && number(p, "spaceAfterPt", &patch->spaceAfterPt, 0, 1440, path)
            && number(p, "lineHeight", &patch->lineHeight, 0.25, 10, path)
            && number(p, "lineHeightPt", &patch->lineHeightPt, 0, 1440, path)
            && integer(p, "heading", &patch->headingLevel, 0, 6, path)
            && flag(p, "keepWithNext", &patch->keepWithNext, path)
            && flag(p, "keepTogether", &patch->keepTogether, path);
    }

    bool table(const QJsonObject &t, TableStyle *style, const QString &path)
    {
        known(t, path, {"borderPt", "paddingPt", "headerBackground", "bandBackground"});
        return number(t, "borderPt", &style->borderPt, 0, 72, path)
            && number(t, "paddingPt", &style->paddingPt, 0, 144, path)
            && color(t, "headerBackground", &style->headerBackground, path)
            && color(t, "bandBackground", &style->bandBackground, path);
    }
};

} // namespace

QStringList overrideNames(quint32 bits)
{
    QStringList names;
    for (int i = 0; i < stylePropertyNames().size(); ++i)
        if (bits & (1u << i)) names.append(stylePropertyNames().at(i));
    return names;
}

std::optional<quint32> overrideBits(const QJsonValue &value)
{
    if (value.isUndefined()) return 0u;
    if (!value.isArray()) return std::nullopt;
    quint32 bits = 0;
    for (const QJsonValue &name : value.toArray()) {
        const qsizetype index = stylePropertyNames().indexOf(name.toString());
        if (!name.isString() || index < 0) return std::nullopt;
        bits |= 1u << index;
    }
    return bits;
}

QJsonObject characterPatchJson(const CharFormatPatch &patch)
{
    QJsonObject o;
    if (patch.family) o.insert(QStringLiteral("family"), *patch.family);
    if (patch.sizePt) o.insert(QStringLiteral("sizePt"), *patch.sizePt);
    if (patch.weight) o.insert(QStringLiteral("weight"), *patch.weight);
    if (patch.italic) o.insert(QStringLiteral("italic"), *patch.italic == 1);
    if (patch.underline) o.insert(QStringLiteral("underline"), *patch.underline == 1);
    if (patch.strike) o.insert(QStringLiteral("strike"), *patch.strike == 1);
    if (patch.color) o.insert(QStringLiteral("color"), colorName(*patch.color));
    if (patch.background) o.insert(QStringLiteral("background"), colorName(*patch.background));
    if (patch.verticalAlign)
        o.insert(QStringLiteral("verticalAlign"), verticalAlignNames().at(int(*patch.verticalAlign)));
    return o;
}

QJsonObject paragraphPatchJson(const ParagraphFormatPatch &patch)
{
    QJsonObject o;
    if (patch.alignment) o.insert(QStringLiteral("align"), alignmentNames().at(int(*patch.alignment)));
    if (patch.direction) o.insert(QStringLiteral("direction"), directionNames().at(int(*patch.direction)));
    auto number = [&o](const char *key, const std::optional<double> &value) {
        if (value) o.insert(QLatin1String(key), *value);
    };
    number("indentStartPt", patch.indentStartPt);
    number("indentEndPt", patch.indentEndPt);
    number("firstLineIndentPt", patch.firstLineIndentPt);
    number("spaceBeforePt", patch.spaceBeforePt);
    number("spaceAfterPt", patch.spaceAfterPt);
    number("lineHeight", patch.lineHeight);
    number("lineHeightPt", patch.lineHeightPt);
    if (patch.headingLevel) o.insert(QStringLiteral("heading"), *patch.headingLevel);
    if (patch.keepWithNext) o.insert(QStringLiteral("keepWithNext"), *patch.keepWithNext);
    if (patch.keepTogether) o.insert(QStringLiteral("keepTogether"), *patch.keepTogether);
    return o;
}

QJsonObject tableStyleJson(const TableStyle &style)
{
    QJsonObject o;
    if (style.borderPt) o.insert(QStringLiteral("borderPt"), *style.borderPt);
    if (style.paddingPt) o.insert(QStringLiteral("paddingPt"), *style.paddingPt);
    if (style.headerBackground) o.insert(QStringLiteral("headerBackground"), colorName(*style.headerBackground));
    if (style.bandBackground) o.insert(QStringLiteral("bandBackground"), colorName(*style.bandBackground));
    return o;
}

QJsonObject styleJson(const NamedStyle &style)
{
    QJsonObject o{{QStringLiteral("id"), style.id},
                  {QStringLiteral("name"), style.name},
                  {QStringLiteral("kind"), styleKindName(style.kind)}};
    if (!style.parentId.isEmpty()) o.insert(QStringLiteral("parent"), style.parentId);
    if (!style.character.isEmpty()) o.insert(QStringLiteral("character"), characterPatchJson(style.character));
    if (!style.paragraph.isEmpty()) o.insert(QStringLiteral("paragraph"), paragraphPatchJson(style.paragraph));
    if (!style.table.isEmpty()) o.insert(QStringLiteral("table"), tableStyleJson(style.table));
    return o;
}

QJsonArray stylesJson(const StyleSheet &styles)
{
    QStringList ids = styles.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray array;
    for (const QString &id : std::as_const(ids)) array.append(styleJson(styles.value(id)));
    return array;
}

QJsonObject frameSetJson(const FrameSet &set)
{
    QJsonArray frames;
    const TextFrame defaults;
    for (const TextFrame &f : set.frames) {
        QJsonObject o{{QStringLiteral("id"), f.id},
                      {QStringLiteral("page"), f.page},
                      {QStringLiteral("xPt"), f.xPt},
                      {QStringLiteral("yPt"), f.yPt},
                      {QStringLiteral("widthPt"), f.widthPt},
                      {QStringLiteral("heightPt"), f.heightPt}};
        if (f.columns != defaults.columns) o.insert(QStringLiteral("columns"), f.columns);
        if (f.gutterPt != defaults.gutterPt) o.insert(QStringLiteral("gutterPt"), f.gutterPt);
        if (f.insetPt != defaults.insetPt) o.insert(QStringLiteral("insetPt"), f.insetPt);
        if (!f.next.isEmpty()) o.insert(QStringLiteral("next"), f.next);
        frames.append(o);
    }
    QJsonObject o{{QStringLiteral("frames"), frames}};
    if (!set.storyStart.isEmpty()) o.insert(QStringLiteral("story"), set.storyStart);
    return o;
}

bool styleFromJson(const QJsonObject &o, NamedStyle *style, PublishingError *error, QStringList *warnings,
                   const QString &base)
{
    Parse parse{error, warnings};
    parse.known(o, base, {"id", "name", "kind", "parent", "character", "paragraph", "table"});
    const QJsonValue id = o.value(QStringLiteral("id"));
    const QJsonValue name = o.value(QStringLiteral("name"));
    const QJsonValue parent = o.value(QStringLiteral("parent"));
    if (!id.isString()) return parse.fail(base + QStringLiteral(".id"), QStringLiteral("Invalid style identifier"));
    if (!name.isString()) return parse.fail(base + QStringLiteral(".name"), QStringLiteral("Invalid style name"));
    if (!parent.isUndefined() && !parent.isString())
        return parse.fail(base + QStringLiteral(".parent"), QStringLiteral("Invalid parent style"));
    const std::optional<StyleKind> kind = styleKindFromName(o.value(QStringLiteral("kind")).toString());
    if (!kind) return parse.fail(base + QStringLiteral(".kind"), QStringLiteral("Unknown style kind"));
    NamedStyle result;
    result.id = id.toString();
    result.name = name.toString();
    result.kind = *kind;
    result.parentId = parent.toString();
    QJsonObject character, paragraph, table;
    if (!parse.object(o, "character", &character, base) || !parse.object(o, "paragraph", &paragraph, base)
        || !parse.object(o, "table", &table, base)
        || !parse.character(character, &result.character, base + QStringLiteral(".character"))
        || !parse.paragraph(paragraph, &result.paragraph, base + QStringLiteral(".paragraph"))
        || !parse.table(table, &result.table, base + QStringLiteral(".table")))
        return false;
    if (const QString problem = styleProblem(result); !problem.isEmpty()) return parse.fail(base, problem);
    *style = result;
    return true;
}

bool stylesFromJson(const QJsonArray &array, StyleSheet *styles, PublishingError *error, QStringList *warnings)
{
    Parse parse{error, warnings};
    if (array.size() > MaximumStyles) return parse.fail(QStringLiteral("styles"), QStringLiteral("Too many styles"));
    StyleSheet result;
    for (int i = 0; i < array.size(); ++i) {
        const QString path = QStringLiteral("styles[%1]").arg(i);
        if (!array.at(i).isObject()) return parse.fail(path, QStringLiteral("Object expected"));
        NamedStyle style;
        if (!styleFromJson(array.at(i).toObject(), &style, error, warnings, path)) return false;
        if (result.contains(style.id)) return parse.fail(path + QStringLiteral(".id"), QStringLiteral("Duplicate style identifier"));
        result.insert(style.id, style);
    }
    if (const QString problem = styleSheetProblem(result); !problem.isEmpty())
        return parse.fail(QStringLiteral("styles"), problem);
    *styles = result;
    return true;
}

bool frameSetFromJson(const QJsonObject &o, FrameSet *frames, PublishingError *error, QStringList *warnings)
{
    Parse parse{error, warnings};
    const QString base = QStringLiteral("textFrames");
    parse.known(o, base, {"story", "frames"});
    const QJsonValue story = o.value(QStringLiteral("story"));
    if (!story.isUndefined() && !story.isString())
        return parse.fail(base + QStringLiteral(".story"), QStringLiteral("Invalid story frame"));
    const QJsonArray array = o.value(QStringLiteral("frames")).toArray();
    if (array.size() > MaximumFrames) return parse.fail(base + QStringLiteral(".frames"), QStringLiteral("Too many text frames"));
    FrameSet result;
    result.storyStart = story.toString();
    for (int i = 0; i < array.size(); ++i) {
        const QString path = base + QStringLiteral(".frames[%1]").arg(i);
        const QJsonObject f = array.at(i).toObject();
        if (!array.at(i).isObject()) return parse.fail(path, QStringLiteral("Object expected"));
        parse.known(f, path, {"id", "page", "xPt", "yPt", "widthPt", "heightPt", "columns", "gutterPt",
                              "insetPt", "next"});
        for (const char *required : {"id", "xPt", "yPt", "widthPt", "heightPt"})
            if (!f.contains(QLatin1String(required)))
                return parse.fail(Parse::at(path, required), QStringLiteral("Missing member"));
        TextFrame frame;
        frame.id = f.value(QStringLiteral("id")).toString();
        frame.next = f.value(QStringLiteral("next")).toString();
        std::optional<int> page, columns;
        std::optional<double> x, y, width, height, gutter, inset;
        if (!parse.integer(f, "page", &page, 0, MaximumFramePages - 1, path)
            || !parse.number(f, "xPt", &x, -14400, 14400, path) || !parse.number(f, "yPt", &y, -14400, 14400, path)
            || !parse.number(f, "widthPt", &width, 18, 14400, path)
            || !parse.number(f, "heightPt", &height, 18, 14400, path)
            || !parse.integer(f, "columns", &columns, 1, 24, path)
            || !parse.number(f, "gutterPt", &gutter, 0, 1440, path)
            || !parse.number(f, "insetPt", &inset, 0, 1440, path))
            return false;
        frame.page = page.value_or(0);
        frame.xPt = *x;
        frame.yPt = *y;
        frame.widthPt = *width;
        frame.heightPt = *height;
        frame.columns = columns.value_or(frame.columns);
        frame.gutterPt = gutter.value_or(frame.gutterPt);
        frame.insetPt = inset.value_or(frame.insetPt);
        result.frames.append(frame);
    }
    if (const QString problem = frameSetProblem(result); !problem.isEmpty()) return parse.fail(base, problem);
    *frames = result;
    return true;
}

} // namespace QindaTK::RichText::Codec
