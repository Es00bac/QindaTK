// SPDX-License-Identifier: LGPL-3.0-or-later
// qindatk.richtext members of tracked formatting and tracked table structure
// (WP-052, D-117): a format change's former character formats ("formats")
// and paragraph formats ("paragraphs"), and a deleted row or column
// ("table", "row", "column") (docs/references-review.md#persistence).
#include "rich_text_codec_p.h"

#include <QJsonArray>
#include <cmath>

namespace QindaTK::RichText::Codec {

namespace {

struct FormatReader {
    PublishingError *error;
    QString base;

    bool fail(const QString &key, const QString &message)
    {
        if (error && error->path.isEmpty()) *error = {message, base + u'.' + key};
        return false;
    }
    bool number(const QJsonObject &o, const char *key, double *out, double min, double max)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < min || v.toDouble() > max)
            return fail(QLatin1String(key), QStringLiteral("Number out of range"));
        *out = v.toDouble();
        return true;
    }
    bool integer(const QJsonObject &o, const char *key, int *out, int min, int max)
    {
        double value = *out;
        if (!number(o, key, &value, min, max)) return false;
        if (value != std::floor(value)) return fail(QLatin1String(key), QStringLiteral("Integer expected"));
        *out = int(value);
        return true;
    }
    bool flag(const QJsonObject &o, const char *key, qint8 *out)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isBool()) return fail(QLatin1String(key), QStringLiteral("Boolean expected"));
        *out = qint8(v.toBool());
        return true;
    }
    bool flag(const QJsonObject &o, const char *key, bool *out)
    {
        qint8 value = *out;
        if (!flag(o, key, &value)) return false;
        *out = value == 1;
        return true;
    }
    bool color(const QJsonObject &o, const char *key, QColor *out)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const QColor c = QColor::fromString(v.toString());
        if (!v.isString() || !c.isValid() || !v.toString().startsWith(u'#'))
            return fail(QLatin1String(key), QStringLiteral("Invalid colour"));
        *out = c;
        return true;
    }
    template <typename Enum>
    bool name(const QJsonObject &o, const char *key, const QStringList &names, Enum *out)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const qsizetype index = names.indexOf(v.toString());
        if (!v.isString() || index < 0) return fail(QLatin1String(key), QStringLiteral("Unknown value"));
        *out = Enum(index);
        return true;
    }

    bool character(const QJsonObject &o, CharFormat *f)
    {
        f->family = o.value(QStringLiteral("family")).toString();
        f->href = o.value(QStringLiteral("href")).toString();
        f->styleId = o.value(QStringLiteral("style")).toString();
        return number(o, "sizePt", &f->sizePt, 1, 1638) && integer(o, "weight", &f->weight, 1, 1000)
            && flag(o, "italic", &f->italic) && flag(o, "underline", &f->underline) && flag(o, "strike", &f->strike)
            && color(o, "color", &f->color) && color(o, "background", &f->background)
            && name(o, "verticalAlign", verticalAlignNames(), &f->verticalAlign);
    }

    bool paragraph(const QJsonObject &o, ParagraphFormat *f)
    {
        f->data = o.value(QStringLiteral("data")).toObject();
        f->listId = o.value(QStringLiteral("list")).toString();
        f->styleId = o.value(QStringLiteral("style")).toString();
        const std::optional<quint32> overrides = overrideBits(o.value(QStringLiteral("overrides")));
        if (!overrides) return fail(QStringLiteral("overrides"), QStringLiteral("Unknown overridden property"));
        f->overrides = f->styleId.isEmpty() ? 0 : *overrides;
        return name(o, "align", alignmentNames(), &f->alignment) && name(o, "direction", directionNames(), &f->direction)
            && number(o, "indentStartPt", &f->indentStartPt, -1440, 1440)
            && number(o, "indentEndPt", &f->indentEndPt, -1440, 1440)
            && number(o, "firstLineIndentPt", &f->firstLineIndentPt, -1440, 1440)
            && number(o, "spaceBeforePt", &f->spaceBeforePt, 0, 1440) && number(o, "spaceAfterPt", &f->spaceAfterPt, 0, 1440)
            && number(o, "lineHeight", &f->lineHeight, 0.25, 10) && number(o, "lineHeightPt", &f->lineHeightPt, 0, 1440)
            && integer(o, "heading", &f->headingLevel, 0, 6) && integer(o, "listLevel", &f->listLevel, 0, 8)
            && flag(o, "keepWithNext", &f->keepWithNext) && flag(o, "keepTogether", &f->keepTogether)
            && flag(o, "pageBreakBefore", &f->pageBreakBefore);
    }
};

} // namespace

void insertChangeMembers(const TrackedChange &change, QJsonObject *o)
{
    QJsonArray formats;
    for (const CharFormat &format : change.formats) formats.append(charFormatJson(format.withoutMarks()));
    if (!formats.isEmpty()) o->insert(QStringLiteral("formats"), formats);
    QJsonArray paragraphs;
    for (const ParagraphFormatChange &entry : change.paragraphs)
        paragraphs.append(QJsonObject{{QStringLiteral("paragraph"), entry.paragraph.toString(QUuid::WithoutBraces)},
                                      {QStringLiteral("format"), paragraphFormatJson(entry.before)}});
    if (!paragraphs.isEmpty()) o->insert(QStringLiteral("paragraphs"), paragraphs);
    if (!change.table.isNull()) o->insert(QStringLiteral("table"), change.table.toString(QUuid::WithoutBraces));
    if (!change.row.isNull()) o->insert(QStringLiteral("row"), change.row.toString(QUuid::WithoutBraces));
    if (!change.column.isNull()) o->insert(QStringLiteral("column"), change.column.toString(QUuid::WithoutBraces));
}

bool readChangeMembers(const QJsonObject &o, TrackedChange *change, const QString &path, PublishingError *error)
{
    auto fail = [&](const QString &member, const QString &message) {
        if (error && error->path.isEmpty()) *error = {message, path + u'.' + member};
        return false;
    };
    auto uuid = [&](const char *key, QUuid *out) {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        *out = QUuid(v.toString());
        return !out->isNull() || fail(QLatin1String(key), QStringLiteral("Invalid identifier"));
    };
    if (!uuid("table", &change->table) || !uuid("row", &change->row) || !uuid("column", &change->column)) return false;
    if (!change->row.isNull() && !change->column.isNull())
        return fail(QStringLiteral("column"), QStringLiteral("A change deletes a row or a column"));
    const QJsonValue formats = o.value(QStringLiteral("formats"));
    if (!formats.isUndefined() && (!formats.isArray() || formats.toArray().size() > MaximumFormerFormats))
        return fail(QStringLiteral("formats"), QStringLiteral("Array expected"));
    const QJsonArray formatList = formats.toArray();
    for (int i = 0; i < formatList.size(); ++i) {
        FormatReader reader{error, path + QStringLiteral(".formats[%1]").arg(i)};
        if (!formatList.at(i).isObject()) return fail(QStringLiteral("formats[%1]").arg(i), QStringLiteral("Object expected"));
        CharFormat format;
        if (!reader.character(formatList.at(i).toObject(), &format)) return false;
        change->formats.append(format);
    }
    const QJsonValue paragraphs = o.value(QStringLiteral("paragraphs"));
    if (!paragraphs.isUndefined() && !paragraphs.isArray())
        return fail(QStringLiteral("paragraphs"), QStringLiteral("Array expected"));
    const QJsonArray paragraphList = paragraphs.toArray();
    for (int i = 0; i < paragraphList.size(); ++i) {
        const QString member = QStringLiteral("paragraphs[%1]").arg(i);
        const QJsonObject entry = paragraphList.at(i).toObject();
        ParagraphFormatChange change_;
        change_.paragraph = QUuid(entry.value(QStringLiteral("paragraph")).toString());
        if (change_.paragraph.isNull()) return fail(member + QStringLiteral(".paragraph"), QStringLiteral("Invalid identifier"));
        FormatReader reader{error, path + u'.' + member + QStringLiteral(".format")};
        if (!reader.paragraph(entry.value(QStringLiteral("format")).toObject(), &change_.before)) return false;
        change->paragraphs.append(change_);
    }
    return true;
}

} // namespace QindaTK::RichText::Codec
