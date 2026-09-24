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
RichText::Fragment toFragment(const QString &html);

// Plain text split into paragraphs; spans use format 0 (inherit).
RichText::Fragment fromPlainText(const QString &text);

} // namespace QindaTK::RichTextHtml
