// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_document.h"
#include "rich_text_codec.h"

#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrl>

namespace QindaTK {
using namespace RichText;

void ChangeSet::merge(const ChangeSet &other)
{
    // `other` happened later: a re-inserted paragraph is no longer removed.
    for (const QUuid &id : other.paragraphs) removedParagraphs.remove(id);
    paragraphs.unite(other.paragraphs);
    removedParagraphs.unite(other.removedParagraphs);
    for (const QUuid &id : other.removedParagraphs) paragraphs.remove(id);
    tables.unite(other.tables);
    listDefinitions.unite(other.listDefinitions);
    structure = structure || other.structure;
    lists = lists || other.lists;
    objects = objects || other.objects;
    document = document || other.document;
    annotations = annotations || other.annotations;
    reset = reset || other.reset;
}

bool ChangeSet::isEmpty() const
{
    return paragraphs.isEmpty() && removedParagraphs.isEmpty() && tables.isEmpty()
        && listDefinitions.isEmpty() && !structure && !lists && !objects && !document && !annotations && !reset;
}

RichTextDocument::RichTextDocument(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QindaTK::RichText::ChangeSet>();
    qRegisterMetaType<QindaTK::RichText::Selection>();
    clear();
}

RichTextDocument::~RichTextDocument() = default;

void RichTextDocument::clear()
{
    auto paragraph = std::make_shared<Paragraph>();
    paragraph->id = QUuid::createUuid();
    resetContent({Block{paragraph, {}}}, {CharFormat{}}, {}, {}, PageSetup{},
                 DocumentDefaults{}, m_documentId);
}

SnapshotHeader RichTextDocument::header() const
{
    return {QStringLiteral("qindatk.richtext"), m_documentId, {m_revision}, m_resetGeneration};
}

int RichTextDocument::paragraphCount() const { return int(m_paragraphs.size()); }

int RichTextDocument::characterCount() const
{
    qsizetype count = 0;
    for (const Paragraph *p : paragraphsInOrder()) count += p->text.size();
    return int(qMin<qsizetype>(count, std::numeric_limits<int>::max()));
}

void RichTextDocument::setReadOnly(bool readOnly)
{
    if (m_readOnly == readOnly) return;
    if (readOnly && m_open) cancelTransaction();
    m_readOnly = readOnly;
    emit changed();
    emit historyChanged();
}

void RichTextDocument::ensureOrder() const
{
    if (!m_orderDirty) return;
    m_flat.clear();
    m_flat.reserve(m_paragraphs.size());
    for (int b = 0; b < m_blocks.size(); ++b) {
        const Block &block = m_blocks.at(b);
        if (block.paragraph) {
            Paragraph *p = block.paragraph.get();
            p->flatIndex = int(m_flat.size());
            p->blockIndex = b;
            p->table = nullptr;
            p->row = p->cell = -1;
            m_flat.append(p);
            continue;
        }
        Table *t = block.table.get();
        t->blockIndex = b;
        for (int r = 0; r < t->rows.size(); ++r)
            for (int c = 0; c < t->rows.at(r).cells.size(); ++c)
                for (const ParagraphPtr &pp : t->rows.at(r).cells.at(c).paragraphs) {
                    pp->flatIndex = int(m_flat.size());
                    pp->blockIndex = -1;
                    pp->table = t;
                    pp->row = r;
                    pp->cell = c;
                    m_flat.append(pp.get());
                }
    }
    m_orderDirty = false;
}

const QVector<Paragraph *> &RichTextDocument::paragraphsInOrder() const
{
    ensureOrder();
    return m_flat;
}

Paragraph *RichTextDocument::nextParagraph(const Paragraph *p) const
{
    ensureOrder();
    if (!p || p->flatIndex < 0 || p->flatIndex + 1 >= m_flat.size()) return nullptr;
    return m_flat.at(p->flatIndex + 1);
}

Paragraph *RichTextDocument::previousParagraph(const Paragraph *p) const
{
    ensureOrder();
    if (!p || p->flatIndex <= 0 || p->flatIndex > m_flat.size()) return nullptr;
    return m_flat.at(p->flatIndex - 1);
}

ContainerSpec RichTextDocument::containerOf(const Paragraph *p) const
{
    ensureOrder();
    if (!p || !p->table) return {};
    const TableRow &row = p->table->rows.at(p->row);
    return {p->table->id, row.id, row.cells.at(p->cell).id};
}

int RichTextDocument::compare(const Position &a, const Position &b) const
{
    const Paragraph *pa = paragraph(a.paragraph);
    const Paragraph *pb = paragraph(b.paragraph);
    if (!pa || !pb) return 0;
    ensureOrder();
    if (pa != pb) return pa->flatIndex < pb->flatIndex ? -1 : 1;
    return a.offset < b.offset ? -1 : a.offset > b.offset ? 1 : 0;
}

static bool splitsSurrogate(const QString &text, int offset)
{
    return offset > 0 && offset < text.size() && text.at(offset - 1).isHighSurrogate()
        && text.at(offset).isLowSurrogate();
}

bool RichTextDocument::isValidPosition(const Position &position) const
{
    const Paragraph *p = paragraph(position.paragraph);
    return p && position.offset >= 0 && position.offset <= p->text.size()
        && !splitsSurrogate(p->text, position.offset);
}

Position RichTextDocument::clampPosition(const Position &position) const
{
    const Paragraph *p = paragraph(position.paragraph);
    if (!p) return startPosition();
    Position result = position;
    result.offset = qBound(0, position.offset, int(p->text.size()));
    if (splitsSurrogate(p->text, result.offset)) --result.offset;
    return result;
}

Position RichTextDocument::startPosition() const
{
    const auto &flat = paragraphsInOrder();
    return flat.isEmpty() ? Position{} : Position{flat.first()->id, 0};
}

Position RichTextDocument::endPosition() const
{
    const auto &flat = paragraphsInOrder();
    return flat.isEmpty() ? Position{} : Position{flat.last()->id, int(flat.last()->text.size())};
}

Position RichTextDocument::paragraphEnd(const QUuid &id) const
{
    const Paragraph *p = paragraph(id);
    return p ? Position{id, int(p->text.size())} : Position{};
}

const CharFormat &RichTextDocument::charFormat(int index) const
{
    return index >= 0 && index < m_formats.size() ? m_formats.at(index) : m_formats.first();
}

int RichTextDocument::internFormat(const CharFormat &format)
{
    const auto found = m_formatIndex.constFind(format);
    if (found != m_formatIndex.constEnd()) return found.value();
    const int index = int(m_formats.size());
    m_formats.append(format);
    m_formatIndex.insert(format, index);
    return index;
}

static int spanFormatAt(const Paragraph &p, int offset)
{
    int start = 0;
    for (const Span &span : p.spans) {
        if (offset < start + span.length) return span.format;
        start += span.length;
    }
    return p.spans.isEmpty() ? p.markFormat : p.spans.last().format;
}

int RichTextDocument::formatIndexAt(const Position &position) const
{
    const Paragraph *p = paragraph(position.paragraph);
    if (!p) return 0;
    if (p->text.isEmpty()) return p->markFormat;
    const int offset = qBound(0, position.offset, int(p->text.size()));
    int index = spanFormatAt(*p, offset > 0 ? offset - 1 : 0);
    CharFormat format = charFormat(index);
    const CharFormat *next = offset > 0 && offset < p->text.size() ? &charFormat(spanFormatAt(*p, offset)) : nullptr;
    // Typing never copies an object identity, and only extends a link from inside it.
    const bool insideLink = !format.href.isEmpty() && next && next->href == format.href;
    // AGENT-NOTE (docs/references-review.md#marks): typed text never joins a
    // field result or a tracked change, and joins a comment only inside it.
    QStringList comments;
    for (const QString &id : std::as_const(format.commentIds))
        if (next && next->commentIds.contains(id)) comments.append(id);
    if (format.objectId.isEmpty() && (format.href.isEmpty() || insideLink) && format.fieldId.isEmpty()
        && format.insertionId.isEmpty() && format.deletionId.isEmpty() && comments == format.commentIds)
        return index;
    format.objectId.clear();
    if (!insideLink) format.href.clear();
    format.fieldId.clear();
    format.insertionId.clear();
    format.deletionId.clear();
    format.commentIds = comments;
    return const_cast<RichTextDocument *>(this)->internFormat(format);
}

const ListDefinition *RichTextDocument::list(const QString &id) const
{
    const auto found = m_lists.constFind(id);
    return found == m_lists.constEnd() ? nullptr : &found.value();
}

const RichObject *RichTextDocument::object(const QString &id) const
{
    const auto found = m_objects.constFind(id);
    return found == m_objects.constEnd() ? nullptr : &found.value();
}

QVariantMap RichTextDocument::pageSetupMap() const
{
    return {{QStringLiteral("widthPt"), m_page.widthPt},
            {QStringLiteral("heightPt"), m_page.heightPt},
            {QStringLiteral("marginTopPt"), m_page.marginTopPt},
            {QStringLiteral("marginRightPt"), m_page.marginRightPt},
            {QStringLiteral("marginBottomPt"), m_page.marginBottomPt},
            {QStringLiteral("marginLeftPt"), m_page.marginLeftPt},
            {QStringLiteral("columns"), m_page.columns},
            {QStringLiteral("columnGapPt"), m_page.columnGapPt},
            {QStringLiteral("bleedPt"), m_page.bleedPt},
            {QStringLiteral("headerText"), m_page.headerText},
            {QStringLiteral("footerText"), m_page.footerText}};
}

void RichTextDocument::registerBlock(const Block &block)
{
    if (block.paragraph) {
        m_paragraphs.insert(block.paragraph->id, block.paragraph.get());
        return;
    }
    m_tables.insert(block.table->id, block.table.get());
    for (const TableRow &row : block.table->rows)
        for (const TableCell &cell : row.cells)
            for (const ParagraphPtr &p : cell.paragraphs) m_paragraphs.insert(p->id, p.get());
}

void RichTextDocument::unregisterBlock(const Block &block)
{
    if (block.paragraph) {
        m_paragraphs.remove(block.paragraph->id);
        return;
    }
    m_tables.remove(block.table->id);
    for (const TableRow &row : block.table->rows)
        for (const TableCell &cell : row.cells)
            for (const ParagraphPtr &p : cell.paragraphs) m_paragraphs.remove(p->id);
}

void RichTextDocument::resetContent(QVector<Block> blocks, QVector<CharFormat> formats,
                                    QHash<QString, ListDefinition> lists,
                                    QHash<QString, RichObject> objects, const PageSetup &page,
                                    const DocumentDefaults &defaults, const DocumentId &id,
                                    StyleSheet styles, FrameSet frames, Annotations annotations)
{
    if (m_open) cancelTransaction();
    if (blocks.isEmpty() || !blocks.last().paragraph) {
        auto paragraph = std::make_shared<Paragraph>();
        paragraph->id = QUuid::createUuid();
        blocks.append(Block{paragraph, {}});
    }
    // AGENT-GUARD: index 0 is the default format and spans index this table;
    // prepending here would shift every span. The codec guarantees entry 0.
    Q_ASSERT(formats.isEmpty() || formats.first() == CharFormat{});
    if (formats.isEmpty()) formats.append(CharFormat{});
    m_blocks = std::move(blocks);
    m_formats = std::move(formats);
    m_formatIndex.clear();
    for (int i = 0; i < m_formats.size(); ++i)
        if (!m_formatIndex.contains(m_formats.at(i))) m_formatIndex.insert(m_formats.at(i), i);
    m_lists = std::move(lists);
    m_objects = std::move(objects);
    m_page = page;
    m_defaults = defaults;
    // AGENT-GUARD: the codec validates styles and frames before calling this;
    // hand-built content must satisfy styleSheetProblem/frameSetProblem too.
    m_styles = std::move(styles);
    m_frames = std::move(frames);
    m_annotations = std::move(annotations); // the codec checks annotationsProblem
    m_resolvedCharacter.clear();
    if (id.isValid()) m_documentId = id;
    m_paragraphs.clear();
    m_tables.clear();
    for (const Block &block : std::as_const(m_blocks)) {
        // The codec always supplies column IDs; hand-built tables get the
        // identities an older native file would (tableSnapshot needs one per column).
        if (block.table && block.table->columnIds.size() != block.table->columnWeights.size()) {
            block.table->columnIds.clear();
            for (int c = 0; c < block.table->columnWeights.size(); ++c)
                block.table->columnIds.append(derivedColumnId(block.table->id, c));
        }
        registerBlock(block);
    }
    for (Paragraph *p : std::as_const(m_paragraphs)) p->revision = ++m_contentCounter;
    for (Table *t : std::as_const(m_tables)) t->revision = ++m_contentCounter;
    m_orderDirty = true;
    m_history.clear();
    m_historyCursor = 0;
    m_receipts.clear();
    m_mergeAllowed = false;
    ++m_resetGeneration;
    ++m_revision;
    const Position start = startPosition();
    for (PositionAnchor *anchor : std::as_const(m_anchors)) anchor->position = start;
    ChangeSet change;
    change.reset = change.structure = change.lists = change.objects = change.document = true;
    emit contentChanged(change);
    emit changed();
    emit historyChanged();
}

void RichTextDocument::registerAnchor(PositionAnchor *anchor)
{
    if (anchor && !m_anchors.contains(anchor)) m_anchors.append(anchor);
}

void RichTextDocument::unregisterAnchor(PositionAnchor *anchor)
{
    m_anchors.removeAll(anchor);
}

void RichTextDocument::mapAnchorsForText(const QUuid &id, int offset, int removed, int inserted)
{
    for (PositionAnchor *anchor : std::as_const(m_anchors)) {
        Position &pos = anchor->position;
        if (pos.paragraph != id) continue;
        if (pos.offset >= offset + removed) pos.offset += inserted - removed;
        else if (pos.offset > offset) pos.offset = offset;
    }
}

Position RichTextDocument::fallbackAfterRemoval(const QVector<Block> &removed) const
{
    ensureOrder();
    QSet<const Paragraph *> gone;
    int last = -1;
    int first = int(m_flat.size());
    for (const Block &block : removed) {
        auto note = [&](const Paragraph *p) {
            gone.insert(p);
            last = qMax(last, p->flatIndex);
            first = qMin(first, p->flatIndex);
        };
        if (block.paragraph) note(block.paragraph.get());
        else
            for (const TableRow &row : block.table->rows)
                for (const TableCell &cell : row.cells)
                    for (const ParagraphPtr &p : cell.paragraphs) note(p.get());
    }
    for (int i = last + 1; i < m_flat.size(); ++i)
        if (!gone.contains(m_flat.at(i))) return {m_flat.at(i)->id, 0};
    for (int i = first - 1; i >= 0; --i)
        if (!gone.contains(m_flat.at(i))) return {m_flat.at(i)->id, int(m_flat.at(i)->text.size())};
    return {};
}

QString RichTextDocument::text() const
{
    return plainText(startPosition(), endPosition());
}

QStringList RichTextDocument::paragraphIds() const
{
    QStringList ids;
    for (const Paragraph *p : paragraphsInOrder()) ids.append(p->id.toString(QUuid::WithoutBraces));
    return ids;
}

QString RichTextDocument::paragraphText(const QString &paragraphId) const
{
    const Paragraph *p = paragraph(QUuid(paragraphId));
    return p ? p->text : QString();
}

QVariantMap RichTextDocument::paragraphFormatMap(const QString &paragraphId) const
{
    const Paragraph *p = paragraph(QUuid(paragraphId));
    return p ? RichTextCodec::paragraphFormatToMap(p->format) : QVariantMap{};
}

static bool isGenericFamily(const QString &family)
{
    static const QStringList generic{QStringLiteral("sans-serif"), QStringLiteral("serif"),
                                     QStringLiteral("monospace"), QStringLiteral("cursive"),
                                     QStringLiteral("fantasy"), QStringLiteral("system-ui")};
    return generic.contains(family.toLower());
}

QStringList RichTextDocument::missingFontFamilies() const
{
    QSet<int> used;
    for (const Paragraph *p : paragraphsInOrder()) {
        used.insert(p->markFormat);
        for (const Span &span : p->spans) used.insert(span.format);
    }
    QStringList families{m_defaults.family};
    for (int index : std::as_const(used)) families.append(charFormat(index).family);
    for (const NamedStyle &style : m_styles)
        if (style.character.family) families.append(*style.character.family);
    QStringList missing;
    for (const QString &family : std::as_const(families))
        if (!family.isEmpty() && !isGenericFamily(family) && !missing.contains(family)
            && !QFontDatabase::hasFamily(family))
            missing.append(family);
    missing.sort();
    return missing;
}

QString RichTextDocument::toJson() const
{
    return QString::fromUtf8(RichTextCodec::encode(*this));
}

bool RichTextDocument::loadJson(const QString &json)
{
    OperationError error;
    if (RichTextCodec::decode(json.toUtf8(), *this, &error)) return true;
    reject(error.code, error.message);
    return false;
}

bool RichTextDocument::saveToFile(const QString &path)
{
    QSaveFile file(path);
    const QByteArray bytes = RichTextCodec::encode(*this);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        reject(ErrorCode::BackendFailure, QStringLiteral("Cannot write %1").arg(path));
        return false;
    }
    return true;
}

bool RichTextDocument::loadFromFile(const QString &path)
{
    // Accepts a local path or a file:/qrc: URL (QML's Qt.resolvedUrl).
    const QUrl url(path);
    QFile file(url.scheme() == u"file" ? url.toLocalFile()
               : url.scheme() == u"qrc" ? u':' + url.path() : path);
    if (!file.open(QIODevice::ReadOnly)) {
        reject(ErrorCode::Unavailable, QStringLiteral("Cannot read %1").arg(path));
        return false;
    }
    if (file.size() > RichTextCodec::MaximumBytes) {
        reject(ErrorCode::InvalidArgument, QStringLiteral("Document exceeds the size limit"));
        return false;
    }
    OperationError error;
    if (RichTextCodec::decode(file.readAll(), *this, &error)) return true;
    reject(error.code, error.message);
    return false;
}

void RichTextDocument::setTrackChanges(bool enabled)
{
    if (m_trackChanges == enabled) return;
    m_trackChanges = enabled;
    emit changed();
}

void RichTextDocument::setReviewAuthor(const QString &author)
{
    if (m_reviewAuthor == author.trimmed()) return;
    m_reviewAuthor = author.trimmed();
    emit changed();
}

OperationError RichTextDocument::reject(ErrorCode code, const QString &message)
{
    static const char *const names[] = {"", "invalidArgument", "unsupported", "unavailable", "cancelled",
                                        "staleRevision", "staleGeneration", "validation", "backendFailure", "conflict"};
    m_lastErrorCode = QLatin1String(names[qBound(0, int(code), 9)]);
    m_lastError = message;
    emit rejected(message);
    emit changed();
    return {code, message, {}, QStringLiteral("richtext")};
}

} // namespace QindaTK
