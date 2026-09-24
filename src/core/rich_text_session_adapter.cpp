// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_session_adapter.h"
#include "document_session.h"
#include "rich_text_codec.h"
#include "rich_text_codec_p.h"

#include <algorithm>

namespace QindaTK {
using namespace RichText;

RichTextSessionAdapter::RichTextSessionAdapter(QObject *parent)
    : QObject(parent)
{
}

void RichTextSessionAdapter::setSession(DocumentSession *session)
{
    if (m_session == session) return;
    if (m_session) disconnect(m_session, nullptr, this, nullptr);
    m_session = session;
    m_loaded = false;
    if (m_session)
        connect(m_session, &DocumentSession::changed, this, [this] {
            if (m_document && m_session) m_document->setReadOnly(m_session->isReadOnly());
        });
    emit sessionChanged();
    attach();
}

void RichTextSessionAdapter::setDocument(RichTextDocument *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    m_paragraphs.clear();
    m_tables.clear();
    m_formats = {};
    m_formatsGeneration = ~0ull;
    m_loaded = false;
    if (m_document)
        connect(m_document, &RichTextDocument::contentChanged, this, [this] {
            if (!m_loading) flush();
        });
    emit documentChanged();
    attach();
}

void RichTextSessionAdapter::setExtensionKey(const QString &key)
{
    if (m_key == key || key.isEmpty()) return;
    m_key = key;
    emit extensionKeyChanged();
    attach();
}

void RichTextSessionAdapter::attach()
{
    if (!m_session || !m_document) return;
    // An opened or recovered session is the source; a new one receives the document.
    if (m_session->extensions().contains(m_key)) reload();
    else flush();
}

bool RichTextSessionAdapter::reload()
{
    if (!m_session || !m_document) return false;
    const QJsonValue value = m_session->extensions().value(m_key);
    OperationError error;
    QStringList warnings;
    bool ok = value.isObject();
    if (ok) {
        m_loading = true;
        ok = RichTextCodec::fromJsonObject(value.toObject(), *m_document, &error, &warnings);
        m_loading = false;
    } else {
        error.message = tr("The session has no rich-text content");
    }
    m_paragraphs.clear();
    m_tables.clear();
    m_formats = {};
    m_formatsGeneration = ~0ull;
    m_loaded = ok;
    m_lastError = ok ? QString() : error.message;
    m_warnings = warnings;
    if (ok) m_document->setReadOnly(m_session->isReadOnly());
    emit stateChanged();
    return ok;
}

QJsonObject RichTextSessionAdapter::paragraphJson(const Paragraph &p)
{
    auto &slot = m_paragraphs[p.id];
    if (slot.first == p.revision && !slot.second.isEmpty()) return slot.second.value(QStringLiteral("paragraph")).toObject();
    return paragraphEntry(p).value(QStringLiteral("paragraph")).toObject();
}

QJsonObject RichTextSessionAdapter::paragraphEntry(const Paragraph &p)
{
    // The cached value is the whole {"paragraph": ...} block, so an unchanged
    // paragraph reuses the same shared JSON and DocumentSession's equality
    // check stops at a pointer comparison.
    auto &slot = m_paragraphs[p.id];
    if (slot.first == p.revision && !slot.second.isEmpty()) return slot.second;
    QJsonArray spans;
    for (const Span &span : p.spans) spans.append(QJsonArray{span.length, span.format});
    QJsonObject o{{QStringLiteral("id"), p.id.toString(QUuid::WithoutBraces)},
                  {QStringLiteral("text"), p.text},
                  {QStringLiteral("spans"), spans},
                  {QStringLiteral("mark"), p.markFormat}};
    const QJsonObject format = Codec::paragraphFormatJson(p.format);
    if (!format.isEmpty()) o.insert(QStringLiteral("format"), format);
    slot = {p.revision, QJsonObject{{QStringLiteral("paragraph"), o}}};
    return slot.second;
}

QJsonObject RichTextSessionAdapter::liveJson()
{
    const RichTextDocument &doc = *m_document;
    if (m_formatsGeneration != doc.resetGenerationValue() || m_formats.size() > doc.formatCount()) {
        m_formats = {};
        m_paragraphs.clear();
        m_tables.clear();
        m_formatsGeneration = doc.resetGenerationValue();
    }
    // The format pool only grows while a document is loaded, so earlier
    // entries (and every cached span index) stay valid.
    while (m_formats.size() < doc.formatCount())
        m_formats.append(Codec::charFormatJson(doc.charFormat(int(m_formats.size()))));
    QJsonArray blocks;
    for (const Block &block : doc.blocks()) {
        if (block.paragraph) {
            blocks.append(paragraphEntry(*block.paragraph));
            continue;
        }
        const Table &t = *block.table;
        auto &slot = m_tables[t.id];
        if (slot.first != t.revision || slot.second.isEmpty()) {
            QJsonArray columns;
            for (double weight : t.columnWeights) columns.append(weight);
            // AGENT-GUARD: without columnIds a reopened session derives new
            // column identities, so TableEditor IDs would change on reopen.
            QJsonArray columnIds;
            for (const QUuid &id : t.columnIds) columnIds.append(id.toString(QUuid::WithoutBraces));
            QJsonArray rows;
            for (const TableRow &row : t.rows) {
                QJsonArray cells;
                for (const TableCell &cell : row.cells) {
                    QJsonArray paragraphs;
                    for (const ParagraphPtr &p : cell.paragraphs) paragraphs.append(paragraphJson(*p));
                    QJsonObject c{{QStringLiteral("id"), cell.id.toString(QUuid::WithoutBraces)},
                                  {QStringLiteral("paragraphs"), paragraphs}};
                    if (cell.background.isValid()) c.insert(QStringLiteral("background"), Codec::colorName(cell.background));
                    if (cell.rowSpan > 1) c.insert(QStringLiteral("rowSpan"), cell.rowSpan);
                    if (cell.columnSpan > 1) c.insert(QStringLiteral("columnSpan"), cell.columnSpan);
                    cells.append(c);
                }
                QJsonObject r{{QStringLiteral("id"), row.id.toString(QUuid::WithoutBraces)}, {QStringLiteral("cells"), cells}};
                if (row.minHeightPt > 0) r.insert(QStringLiteral("minHeightPt"), row.minHeightPt);
                rows.append(r);
            }
            QJsonObject table{{QStringLiteral("id"), t.id.toString(QUuid::WithoutBraces)},
                              {QStringLiteral("columns"), columns}, {QStringLiteral("columnIds"), columnIds},
                              {QStringLiteral("rows"), rows},
                              {QStringLiteral("headerRows"), t.headerRows}, {QStringLiteral("borderPt"), t.borderPt},
                              {QStringLiteral("paddingPt"), t.cellPaddingPt}};
            if (!t.styleId.isEmpty()) table.insert(QStringLiteral("style"), t.styleId);
            slot = {t.revision, QJsonObject{{QStringLiteral("table"), table}}};
        }
        blocks.append(slot.second);
    }
    if (m_paragraphs.size() > 2 * doc.paragraphCount() + 1024)
        for (auto it = m_paragraphs.begin(); it != m_paragraphs.end();)
            it = doc.paragraph(it.key()) ? std::next(it) : m_paragraphs.erase(it);
    QStringList listIds = doc.lists().keys();
    std::sort(listIds.begin(), listIds.end());
    QJsonArray lists;
    for (const QString &id : std::as_const(listIds)) lists.append(Codec::listJson(doc.lists().value(id)));
    QStringList objectIds = doc.objects().keys();
    std::sort(objectIds.begin(), objectIds.end());
    QJsonArray objects;
    for (const QString &id : std::as_const(objectIds)) objects.append(Codec::objectJson(doc.objects().value(id)));
    const PageSetup page = doc.pageSetupValue();
    const DocumentDefaults defaults = doc.defaults();
    QJsonObject defaultsJson{{QStringLiteral("family"), defaults.family}, {QStringLiteral("sizePt"), defaults.sizePt}};
    if (defaults.color.isValid()) defaultsJson.insert(QStringLiteral("color"), Codec::colorName(defaults.color));
    QJsonObject root{{QStringLiteral("schema"), RichTextCodec::schemaId()},
                     {QStringLiteral("version"), RichTextCodec::requiredVersion(doc)},
                     {QStringLiteral("documentId"), doc.documentIdString()},
                     {QStringLiteral("page"), Codec::pageJson(page)},
                     {QStringLiteral("defaults"), defaultsJson},
                     {QStringLiteral("formats"), m_formats},
                     {QStringLiteral("lists"), lists},
                     {QStringLiteral("objects"), objects},
                     {QStringLiteral("blocks"), blocks}};
    // AGENT-CONTRACT: the same WP-051 members as RichTextCodec::toJsonObject.
    if (!doc.styleSheet().isEmpty()) root.insert(QStringLiteral("styles"), Codec::stylesJson(doc.styleSheet()));
    if (!doc.frameSet().frames.isEmpty()) root.insert(QStringLiteral("textFrames"), Codec::frameSetJson(doc.frameSet()));
    return root;
}

bool RichTextSessionAdapter::flush()
{
    if (!m_session || !m_document) return false;
    if (m_session->isReadOnly()) {
        m_lastError = tr("The session is read-only");
        emit stateChanged();
        return false;
    }
    QJsonObject extensions = m_session->extensions();
    extensions.insert(m_key, liveJson());
    m_session->setExtensions(extensions);
    m_loaded = true;
    m_lastError.clear();
    emit stateChanged();
    return true;
}

} // namespace QindaTK
