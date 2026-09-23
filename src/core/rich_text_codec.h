// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <QJsonObject>
#include <QVariantMap>

namespace QindaTK {

// Native structured persistence: schema "qindatk.richtext", version 1.
// Encoding is canonical (formats re-interned in document order, lists and
// objects sorted by ID, sorted JSON keys), so save -> reopen -> save is
// byte-identical. Decoding validates everything before replacing content.
// AGENT-CONTRACT: docs/rich-text.md#persistence documents the schema; a
// schema change bumps CurrentVersion and keeps decoding older versions.
class RichTextCodec {
public:
    static constexpr int CurrentVersion = 1;
    static constexpr qsizetype MaximumBytes = 64 * 1024 * 1024;
    static constexpr int MaximumParagraphs = 1000000;
    static constexpr int MaximumParagraphLength = 1000000;
    static constexpr int MaximumFormats = 65536;
    static constexpr int MaximumObjects = 100000;
    static QString schemaId() { return QStringLiteral("qindatk.richtext"); }
    static QString fragmentSchemaId() { return QStringLiteral("qindatk.richtext.fragment"); }

    static QJsonObject toJsonObject(const RichTextDocument &document);
    static QByteArray encode(const RichTextDocument &document);
    // Replaces the document's content only when the input is fully valid.
    // Unknown members are reported in `warnings` (they are not preserved).
    static bool decode(const QByteArray &bytes, RichTextDocument &document,
                       OperationError *error = nullptr, QStringList *warnings = nullptr);
    static bool fromJsonObject(const QJsonObject &root, RichTextDocument &document,
                               OperationError *error = nullptr, QStringList *warnings = nullptr);

    static QJsonObject fragmentToJson(const RichText::Fragment &fragment);
    static std::optional<RichText::Fragment> fragmentFromJson(const QJsonObject &root,
                                                              OperationError *error = nullptr);

    static QVariantMap paragraphFormatToMap(const RichText::ParagraphFormat &format);
    static QVariantMap charFormatToMap(const RichText::CharFormat &format);
    static QString placementName(RichText::Placement placement);
    static QString listKindName(RichText::ListKind kind);
    static std::optional<RichText::ListKind> listKindFromName(const QString &name);
};

} // namespace QindaTK
