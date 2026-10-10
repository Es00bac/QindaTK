// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_document.h"

namespace QindaTK::RichTextHtml {

// HTML projection of a fragment for other applications (clipboard, export).
// Runs keep family/size/weight/italic/underline/strike/colour/links/super-
// and subscript (named styles resolved); lists become <ul>/<ol>, tables
// <table> with rowspan/colspan, objects <img> with their source and alt text.
// Page geometry, text frames and float placement are dropped.
QString fromFragment(const RichText::Fragment &fragment, const QString &title = {});

// Imports HTML through QTextDocument's parser into a fragment: paragraphs,
// character runs, headings, alignment, lists, tables (with merged cells) and
// inline images. Unsupported constructs are flattened to text, never dropped.
// AGENT-GUARD (D-319, docs/rich-text.md#html-import): pasted and imported
// HTML is hostile input. The parser never loads a resource (no file read, no
// style sheet, no network), and an image keeps its source only when
// isImportableImageSource() accepts it; any other image stays as a
// placeholder object with its alt text and an empty source, so the engine
// never fetches a remote, `file:`, protocol-relative or relative image.
RichText::Fragment toFragment(const QString &html);

// The image sources HTML import keeps: `data:image/...` raster URIs (not
// SVG, which can reference other files) and URIs whose scheme the
// application registered (for example "asset" when it renders
// RichTextAssetRenderer's asset: sources). Process-wide and thread-safe.
// Registering http, https or file is the application's explicit choice.
void registerImageScheme(const QString &scheme);
void unregisterImageScheme(const QString &scheme);
QStringList registeredImageSchemes();
bool isImportableImageSource(const QString &source);

// Plain text split into paragraphs; spans use format 0 (inherit).
RichText::Fragment fromPlainText(const QString &text);

} // namespace QindaTK::RichTextHtml
