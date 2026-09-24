// SPDX-License-Identifier: LGPL-3.0-or-later
// Paragraph shaping of RichTextLayout: formats -> QTextLayout -> line boxes.
#include "rich_text_layout.h"
#include "rich_text_shaping_p.h"
#include "rich_text_spans.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QPaintDevice>

namespace QindaTK {
using namespace RichText;

namespace RichText::Shaping {

namespace {
// AGENT-NOTE: QFont point sizes resolve against a paint device's DPI. A
// 72-dpi device makes "points" equal layout pixels, so layout is identical
// on every screen (QT_FONT_DPI, Xft.dpi) and sizes stay fractional.
class LayoutDevice final : public QPaintDevice {
public:
    QPaintEngine *paintEngine() const override { return nullptr; }
protected:
    int metric(PaintDeviceMetric metric) const override
    {
        switch (metric) {
        case PdmDpiX: case PdmDpiY: case PdmPhysicalDpiX: case PdmPhysicalDpiY: return 72;
        case PdmWidth: case PdmHeight: return 100000;
        case PdmWidthMM: case PdmHeightMM: return 35278;
        case PdmNumColors: return 0x7fffffff;
        case PdmDepth: return 32;
        case PdmDevicePixelRatio: return 1;
        case PdmDevicePixelRatioScaled: return int(QPaintDevice::devicePixelRatioFScale());
        default: return 0;
        }
    }
};
} // namespace

const QPaintDevice *layoutDevice()
{
    static const LayoutDevice device;
    return &device;
}

HeadingStyle headingStyle(int level)
{
    static const HeadingStyle styles[] = {{0, 0}, {20, 700}, {16, 700}, {13.5, 700},
                                          {12, 700}, {11, 700}, {11, 600}};
    return styles[qBound(0, level, 6)];
}

bool paragraphIsRightToLeft(const Paragraph &p)
{
    if (p.format.direction == Direction::RightToLeft) return true;
    if (p.format.direction == Direction::LeftToRight) return false;
    for (const QChar c : p.text) {
        const QChar::Direction d = c.direction();
        if (d == QChar::DirL) return false;
        if (d == QChar::DirR || d == QChar::DirAL) return true;
    }
    return false;
}

QFont resolvedFont(const RichTextDocument &doc, const CharFormat &direct, const ParagraphFormat &pf)
{
    // Named styles first (docs/publishing.md#styles), then heading and document defaults.
    const CharFormat f = doc.effectiveCharFormat(direct, pf);
    const DocumentDefaults defaults = doc.defaults();
    const HeadingStyle heading = headingStyle(pf.headingLevel);
    QFont font(QFont(), layoutDevice());
    font.setFamilies({f.family.isEmpty() ? defaults.family : f.family});
    const double sizePt = f.sizePt > 0 ? f.sizePt : heading.sizePt > 0 ? heading.sizePt : defaults.sizePt;
    font.setPointSizeF(sizePt * PxPerPt);
    font.setWeight(QFont::Weight(qBound(1, f.weight > 0 ? f.weight : heading.weight > 0 ? heading.weight : 400, 1000)));
    font.setItalic(f.italic == 1);
    font.setUnderline(f.underline == 1);
    font.setStrikeOut(f.strike == 1);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QTextCharFormat resolvedFormat(const RichTextDocument &doc, const CharFormat &direct, const ParagraphFormat &pf)
{
    const CharFormat f = doc.effectiveCharFormat(direct, pf);
    QTextCharFormat format;
    format.setFont(resolvedFont(doc, direct, pf), QTextCharFormat::FontPropertiesAll);
    if (f.color.isValid()) format.setForeground(f.color);
    else if (doc.defaults().color.isValid()) format.setForeground(doc.defaults().color);
    if (f.background.isValid()) format.setBackground(f.background);
    if (f.verticalAlign == VerticalAlign::Superscript) format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    if (f.verticalAlign == VerticalAlign::Subscript) format.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    return format;
}

qreal objectCharAdvance(const QTextCharFormat &format)
{
    static QHash<QString, qreal> cache;
    const QString key = format.font().key();
    if (const auto found = cache.constFind(key); found != cache.constEnd()) return found.value();
    QTextLayout probe(QString(QChar(ObjectChar)), format.font(), layoutDevice());
    QTextOption option;
    option.setUseDesignMetrics(true);
    probe.setTextOption(option);
    probe.beginLayout();
    QTextLine line = probe.createLine();
    line.setLineWidth(1e6);
    probe.endLayout();
    const qreal advance = line.cursorToX(1) - line.cursorToX(0);
    if (cache.size() > 4096) cache.clear();
    cache.insert(key, advance);
    return advance;
}

} // namespace RichText::Shaping

using namespace RichText::Shaping;

RichTextLayout::RichTextLayout(QObject *parent)
    : QObject(parent)
{
}

RichTextLayout::~RichTextLayout() = default;

void RichTextLayout::setDocument(RichTextDocument *document)
{
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (m_document)
        connect(m_document, &RichTextDocument::contentChanged, this, &RichTextLayout::onContentChanged);
    invalidateAll();
}

void RichTextLayout::setMode(Mode mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    invalidateAll();
}

void RichTextLayout::setFlowWidth(qreal width)
{
    width = qMax<qreal>(24, width);
    if (qFuzzyCompare(m_flowWidth, width)) return;
    m_flowWidth = width;
    if (m_mode == Mode::Continuous) invalidateAll();
}

void RichTextLayout::setPreedit(const Position &at, const QString &text,
                                const QList<QTextLayout::FormatRange> &formats, int cursor)
{
    m_preeditCursor = text.isEmpty() ? -1 : (cursor < 0 ? int(text.size()) : qMin(cursor, int(text.size())));
    const Position previous = m_preeditAt;
    const bool hadPreedit = !m_preeditText.isEmpty();
    if (previous == at && m_preeditText == text && !hadPreedit && text.isEmpty()) return;
    m_preeditAt = text.isEmpty() ? Position{} : at;
    m_preeditText = text;
    m_preeditFormats = formats;
    ChangeSet change;
    if (hadPreedit) change.paragraphs.insert(previous.paragraph);
    if (!text.isEmpty()) change.paragraphs.insert(at.paragraph);
    if (!change.isEmpty()) onContentChanged(change);
}

void RichTextLayout::touchLru(ParagraphMetrics &metrics)
{
    metrics.lastUse = ++m_useCounter;
}

int RichTextLayout::layoutOffset(const ParagraphMetrics &metrics, int modelOffset) const
{
    return metrics.preeditOffset >= 0 && modelOffset > metrics.preeditOffset
        ? modelOffset + metrics.preeditLength : modelOffset;
}

int RichTextLayout::modelOffset(const ParagraphMetrics &metrics, int layoutOffset) const
{
    if (metrics.preeditOffset < 0 || layoutOffset <= metrics.preeditOffset) return layoutOffset;
    return qMax(metrics.preeditOffset, layoutOffset - metrics.preeditLength);
}

static QString contextKey(const QUuid &id, qreal width, const QVector<Exclusion> &exclusions,
                          int preeditOffset, const QString &preedit, const QVector<WidthBreak> &widths)
{
    // AGENT-GUARD: the key starts with the paragraph ID (38 characters);
    // onContentChanged drops a paragraph's contexts by that prefix.
    QString key = id.toString() + u'/' + QString::number(width, 'g', 10);
    for (const Exclusion &e : exclusions)
        key += QStringLiteral("|%1,%2,%3,%4").arg(e.top).arg(e.bottom).arg(e.left).arg(e.right);
    for (const WidthBreak &w : widths) key += QStringLiteral("|w%1:%2").arg(w.line).arg(w.width, 0, 'g', 10);
    if (preeditOffset >= 0) key += QStringLiteral("|ime%1:").arg(preeditOffset) + preedit;
    return key;
}

ParagraphMetrics &RichTextLayout::ensureParagraph(const Paragraph &p, qreal width,
                                                  const QVector<Exclusion> &exclusions, bool needShape,
                                                  const QVector<WidthBreak> &widths)
{
    const bool preedit = !m_preeditText.isEmpty() && m_preeditAt.paragraph == p.id;
    const bool special = preedit || !exclusions.isEmpty() || !widths.isEmpty();
    const int preeditOffset = preedit ? m_preeditAt.offset : -1;
    ParagraphMetrics &metrics = special
        ? m_contextMetrics[contextKey(p.id, width, exclusions, preeditOffset, m_preeditText, widths)]
        : m_metrics[p.id];
    const bool valid = metrics.revision == p.revision && qFuzzyCompare(metrics.width + 1, width + 1)
        && metrics.exclusions == exclusions && metrics.widths == widths && metrics.preeditOffset == preeditOffset
        && (!preedit || metrics.preeditLength == m_preeditText.size()) && !metrics.lines.isEmpty();
    if (!valid) {
        metrics = ParagraphMetrics{};
        metrics.revision = p.revision;
        metrics.width = width;
        metrics.widths = widths;
        metrics.exclusions = exclusions;
        metrics.preeditOffset = preeditOffset;
        metrics.preeditLength = preedit ? int(m_preeditText.size()) : 0;
        buildLayout(p, metrics);
    } else if (needShape && !metrics.shaped) {
        const QVector<LineBox> previous = metrics.lines;
        buildLayout(p, metrics);
        Q_ASSERT(metrics.lines.size() == previous.size());
    }
    touchLru(metrics);
    return metrics;
}

ParagraphMetrics &RichTextLayout::itemMetrics(const Paragraph &p, const PageItem &item)
{
    // A continued paragraph is keyed by its first region's width (placeParagraph).
    return ensureParagraph(p, item.widths.isEmpty() ? item.width : item.widths.first().width,
                           item.exclusions, false, item.widths);
}

QTextLayout *RichTextLayout::shaped(const Paragraph &p, ParagraphMetrics &metrics)
{
    if (!metrics.shaped) buildLayout(p, metrics);
    touchLru(metrics);
    return metrics.shaped.get();
}

QTextLayout *RichTextLayout::buildLayout(const Paragraph &p, ParagraphMetrics &m)
{
    Q_ASSERT(m_document);
    const RichTextDocument &doc = *m_document;
    const ParagraphFormat &pf = p.format;
    m.rightToLeft = paragraphIsRightToLeft(p);
    const HeadingStyle heading = headingStyle(pf.headingLevel);
    m.keepWithNext = pf.keepWithNext || pf.headingLevel > 0;
    m.spaceBefore = pf.spaceBeforePt * PxPerPt;
    m.spaceAfter = pf.spaceAfterPt * PxPerPt;
    qreal listIndent = 0;
    if (const ListDefinition *list = doc.list(pf.listId))
        for (int level = 0; level <= pf.listLevel && level < list->levels.size(); ++level)
            listIndent += list->levels.at(level).indentPt * PxPerPt;
    m.indentStart = pf.indentStartPt * PxPerPt + listIndent;
    const qreal indentEnd = pf.indentEndPt * PxPerPt;
    Q_UNUSED(heading);

    QString text = p.text;
    QList<QTextLayout::FormatRange> formats;
    auto shift = [&m](int offset) { return m.preeditOffset >= 0 && offset > m.preeditOffset ? offset + m.preeditLength : offset; };
    int start = 0;
    for (const Span &span : p.spans) {
        const CharFormat &cf = doc.charFormat(span.format);
        QTextCharFormat format = resolvedFormat(doc, cf, pf);
        if (!cf.objectId.isEmpty()) {
            // AGENT-GUARD: absolute letter spacing cannot size an object here.
            // QTextEngine scales it by int(font dpi / screen dpi), which is 0
            // on the 72-dpi layout device, so the spacing silently vanishes
            // (measured on Qt 6.11.1; tst_rich_text_layout
            // objectsTakeTheirWidthInTheLine fails if it comes back).
            // Percentage spacing is not dpi-scaled.
            const RichObject *object = doc.object(cf.objectId);
            const qreal width = object && object->placement == Placement::Inline ? object->widthPt * PxPerPt : 0;
            const qreal advance = objectCharAdvance(format);
            if (width > 0 && advance > 0) {
                format.setFontLetterSpacingType(QFont::PercentageSpacing);
                format.setFontLetterSpacing(100 * width / advance);
            } else {
                // Anchored and page objects take no inline space: their layout
                // character is U+2060 WORD JOINER (default-ignorable, zero
                // advance, no break opportunity), one for one with the model.
                for (int i = start; i < start + span.length; ++i)
                    if (text.at(i) == QChar(ObjectChar)) text[i] = QChar(WordJoiner);
            }
            format.setForeground(QColor(Qt::transparent));
        }
        const int from = shift(start);
        formats.append({from, shift(start + span.length) - from, format});
        start += span.length;
    }
    if (m.preeditOffset >= 0) {
        text.insert(m.preeditOffset, m_preeditText);
        const QTextCharFormat around = resolvedFormat(doc, doc.charFormat(doc.formatIndexAt({p.id, m.preeditOffset})), pf);
        QTextCharFormat composing = around;
        composing.setFontUnderline(true);
        formats.append({m.preeditOffset, int(m_preeditText.size()), composing});
        for (QTextLayout::FormatRange range : m_preeditFormats) {
            range.start += m.preeditOffset;
            QTextCharFormat merged = composing;
            merged.merge(range.format);
            range.format = merged;
            formats.append(range);
        }
    }
    const QFont markFont = resolvedFont(doc, doc.charFormat(p.markFormat), pf);
    auto layout = std::make_shared<QTextLayout>(text, markFont, layoutDevice());
    QTextOption option;
    option.setTextDirection(m.rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
    Qt::Alignment align = Qt::AlignLeading;
    switch (pf.alignment) {
    case Alignment::Start: align = m.rightToLeft ? Qt::AlignRight : Qt::AlignLeft; break;
    case Alignment::End: align = m.rightToLeft ? Qt::AlignLeft : Qt::AlignRight; break;
    case Alignment::Left: align = Qt::AlignLeft; break;
    case Alignment::Right: align = Qt::AlignRight; break;
    case Alignment::Center: align = Qt::AlignHCenter; break;
    case Alignment::Justify: align = Qt::AlignJustify; break;
    }
    option.setAlignment(align | Qt::AlignAbsolute);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setUseDesignMetrics(true);
    option.setTabStopDistance(36 * PxPerPt);
    layout->setTextOption(option);
    layout->setFormats(formats);
    layout->setCacheEnabled(true);

    // Object heights per layout offset, so a line grows to hold them.
    QHash<int, qreal> objectHeights;
    start = 0;
    for (const Span &span : p.spans) {
        const CharFormat &cf = doc.charFormat(span.format);
        if (!cf.objectId.isEmpty())
            if (const RichObject *object = doc.object(cf.objectId); object && object->placement == Placement::Inline)
                for (int i = start; i < start + span.length; ++i) objectHeights.insert(shift(i), object->heightPt * PxPerPt);
        start += span.length;
    }

    // The full width of line `index`: its width break, else the paragraph width.
    auto widthOf = [&m](int index) {
        qreal width = m.width;
        for (const WidthBreak &w : m.widths)
            if (w.line <= index) width = w.width;
        return width;
    };
    m.lines.clear();
    qreal y = 0;
    qreal estimate = QFontMetricsF(markFont).height();
    layout->beginLayout();
    for (int index = 0;; ++index) {
        QTextLine line = layout->createLine();
        if (!line.isValid()) break;
        const qreal fullWidth = widthOf(index);
        const qreal startIndent = m.indentStart + (index == 0 ? pf.firstLineIndentPt * PxPerPt : 0);
        qreal left = m.rightToLeft ? indentEnd : startIndent;
        qreal right = m.rightToLeft ? startIndent : indentEnd;
        for (const Exclusion &e : m.exclusions) {
            if (e.bottom <= y || e.top >= y + estimate) continue;
            if (e.left <= 0.5) left = qMax(left, e.right);
            else right = qMax(right, fullWidth - e.left);
        }
        const qreal width = qMax<qreal>(1, fullWidth - left - right);
        line.setLineWidth(width);
        qreal ascent = line.ascent();
        const qreal descent = line.descent();
        for (int i = line.textStart(); i < line.textStart() + line.textLength(); ++i)
            ascent = qMax(ascent, objectHeights.value(i, 0));
        // "Single" spacing is the font's line spacing (leading included);
        // the glyph box is centred in a taller or shorter line box.
        const qreal glyphBox = ascent + descent;
        const qreal natural = glyphBox + qMax<qreal>(0, line.leading());
        const qreal height = pf.lineHeightPt > 0 ? pf.lineHeightPt * PxPerPt : natural * pf.lineHeight;
        const qreal halfLeading = (height - glyphBox) / 2;
        line.setPosition(QPointF(left, y + halfLeading + ascent - line.ascent()));
        m.lines.append({line.textStart(), line.textLength(), y, height, ascent, left, width});
        y += height;
        estimate = height;
    }
    layout->endLayout();
    m.height = y;
    m.shaped = layout;
    ++m_stats.paragraphsShaped;
    // Keep at most MaximumShapedLayouts glyph layouts; metrics stay cached.
    // Trimming drops the least recently used quarter in one sorted pass.
    if (++m_shapedCount > MaximumShapedLayouts) {
        QVector<ParagraphMetrics *> shaped;
        shaped.reserve(m_shapedCount);
        for (auto &[key, entry] : m_metrics)
            if (entry.shaped && &entry != &m) shaped.append(&entry);
        for (auto &[key, entry] : m_contextMetrics)
            if (entry.shaped && &entry != &m) shaped.append(&entry);
        std::sort(shaped.begin(), shaped.end(), [](const ParagraphMetrics *a, const ParagraphMetrics *b) {
            return a->lastUse < b->lastUse; });
        const qsizetype drop = shaped.size() - (MaximumShapedLayouts * 3) / 4;
        for (qsizetype i = 0; i < drop; ++i) shaped.at(i)->shaped.reset();
        m_shapedCount = int(shaped.size() - qMax<qsizetype>(0, drop)) + 1;
    }
    return layout.get();
}

QStringList RichTextLayout::missingFontFamilies() const
{
    return m_document ? m_document->missingFontFamilies() : QStringList{};
}

} // namespace QindaTK
