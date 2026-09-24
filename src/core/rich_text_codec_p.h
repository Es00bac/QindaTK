// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_publishing.h"
#include "rich_text_review.h"
#include "rich_text_types.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

// Shared name tables and JSON helpers of the qindatk.richtext codec.
namespace QindaTK::RichText::Codec {

inline const QStringList &alignmentNames()
{
    static const QStringList names{QStringLiteral("start"), QStringLiteral("end"),
                                   QStringLiteral("left"), QStringLiteral("right"),
                                   QStringLiteral("center"), QStringLiteral("justify")};
    return names;
}
inline const QStringList &directionNames()
{
    static const QStringList names{QStringLiteral("auto"), QStringLiteral("ltr"), QStringLiteral("rtl")};
    return names;
}
inline const QStringList &verticalAlignNames()
{
    static const QStringList names{QStringLiteral("normal"), QStringLiteral("super"), QStringLiteral("sub")};
    return names;
}
inline const QStringList &placementNames()
{
    static const QStringList names{QStringLiteral("inline"), QStringLiteral("floatStart"),
                                   QStringLiteral("floatEnd"), QStringLiteral("page")};
    return names;
}
inline const QStringList &listKindNames()
{
    static const QStringList names{QStringLiteral("disc"), QStringLiteral("circle"),
                                   QStringLiteral("square"), QStringLiteral("decimal"),
                                   QStringLiteral("lowerAlpha"), QStringLiteral("upperAlpha"),
                                   QStringLiteral("lowerRoman"), QStringLiteral("upperRoman")};
    return names;
}

inline QString colorName(const QColor &color)
{
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

// Paragraph format keys of the StyleProperty bits, in bit order.
inline const QStringList &stylePropertyNames()
{
    static const QStringList names{QStringLiteral("align"), QStringLiteral("direction"),
                                   QStringLiteral("indentStartPt"), QStringLiteral("indentEndPt"),
                                   QStringLiteral("firstLineIndentPt"), QStringLiteral("spaceBeforePt"),
                                   QStringLiteral("spaceAfterPt"), QStringLiteral("lineHeight"),
                                   QStringLiteral("lineHeightPt"), QStringLiteral("heading"),
                                   QStringLiteral("keepWithNext"), QStringLiteral("keepTogether")};
    return names;
}
// Character keys a style or a direct-formatting reset can name.
inline const QStringList &characterPropertyNames()
{
    static const QStringList names{QStringLiteral("family"), QStringLiteral("sizePt"),
                                   QStringLiteral("weight"), QStringLiteral("italic"),
                                   QStringLiteral("underline"), QStringLiteral("strike"),
                                   QStringLiteral("color"), QStringLiteral("background"),
                                   QStringLiteral("verticalAlign")};
    return names;
}

QJsonObject charFormatJson(const CharFormat &format);
QJsonObject paragraphFormatJson(const ParagraphFormat &format);
QJsonObject objectJson(const RichObject &object);
QJsonObject listJson(const ListDefinition &list);
QJsonObject pageJson(const PageSetup &page);

// Publishing members (rich_text_codec_publishing.cpp). A reader returns
// false with `error` set to a message and `path` to the member below the
// object it read (for example "character.sizePt"); `warnings` receives
// unknown members prefixed with `base`.
struct PublishingError {
    QString message;
    QString path;
};
QStringList overrideNames(quint32 bits);
QJsonObject characterPatchJson(const CharFormatPatch &patch);
QJsonObject paragraphPatchJson(const ParagraphFormatPatch &patch);
QJsonObject tableStyleJson(const TableStyle &style);
QJsonObject styleJson(const NamedStyle &style);
QJsonArray stylesJson(const StyleSheet &styles);        // sorted by ID
QJsonObject frameSetJson(const FrameSet &frames);
bool styleFromJson(const QJsonObject &o, NamedStyle *style, PublishingError *error,
                   QStringList *warnings = nullptr, const QString &base = {});
bool stylesFromJson(const QJsonArray &array, StyleSheet *styles, PublishingError *error,
                    QStringList *warnings = nullptr);    // paths start at "styles[i]"
bool frameSetFromJson(const QJsonObject &o, FrameSet *frames, PublishingError *error,
                      QStringList *warnings = nullptr);  // paths start at "textFrames"
std::optional<quint32> overrideBits(const QJsonValue &names);

// WP-052 members (rich_text_codec_review.cpp, D-117): "fields", "sources",
// "comments" and "changes" arrays sorted by ID (each only when non-empty),
// and the format marks "field", "comments", "insertion" and "deletion".
void insertAnnotationMembers(const Annotations &annotations, QJsonObject *root);
bool annotationsFromJson(const QJsonObject &root, Annotations *annotations, PublishingError *error,
                         QStringList *warnings = nullptr);
void insertMarks(const CharFormat &format, QJsonObject *json);
// With `known`, a mark naming no record of its kind is dropped with a warning.
bool readMarks(const QJsonObject &json, const Annotations *known, CharFormat *format, const QString &path,
               PublishingError *error, QStringList *warnings = nullptr);

} // namespace QindaTK::RichText::Codec
