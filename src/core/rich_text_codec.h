// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"
#include <QJsonObject>
#include <QVariantMap>

namespace QindaTK {

// Native structured persistence: schema "qindatk.richtext", versions 1 and 2.
// Encoding is canonical (formats re-interned in document order, lists and
// objects sorted by ID, sorted JSON keys), so save -> reopen -> save is
// byte-identical. Decoding validates everything before replacing content.
// AGENT-CONTRACT: docs/rich-text.md#persistence documents the schema; an
// incompatible schema change bumps CurrentVersion and keeps decoding older
// versions. An additive optional member (table `columnIds`) stays in
// version 1: older decoders report it as ignored, and this decoder derives
// it when absent. Version 2 (D-104) adds named styles, merged cells, text
// frames and page columns; a document is written as version 2 only when it
// uses one of them (requiredVersion), so other files stay version 1.
// RichTextSessionAdapter::liveJson writes the same members and version.
class RichTextCodec {
public:
    static constexpr int CurrentVersion = 2;
    static constexpr qsizetype MaximumBytes = 64 * 1024 * 1024;
    static constexpr int MaximumParagraphs = 1000000;
    static constexpr int MaximumParagraphLength = 1000000;
    static constexpr int MaximumFormats = 65536;
    static constexpr int MaximumObjects = 100000;
    static QString schemaId() { return QStringLiteral("qindatk.richtext"); }
    static QString fragmentSchemaId() { return QStringLiteral("qindatk.richtext.fragment"); }

    // 2 when the document has styles, text frames, page columns or a table
    // with merged cells or a table style; otherwise 1.
    static int requiredVersion(const RichTextDocument &document);
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
