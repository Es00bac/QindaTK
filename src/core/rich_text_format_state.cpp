// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_format_state.h"
#include "rich_text_area.h"
#include "rich_text_codec_p.h"
#include "rich_text_shaping_p.h"

#include <QFontDatabase>

namespace QindaTK {
using namespace RichText;

namespace {

// Accumulates one attribute over runs: first value, and whether any differs.
template <typename T>
struct Tally {
    T value{};
    bool seen = false;
    bool mixed = false;
    void add(const T &next)
    {
        if (!seen) { value = next; seen = true; }
        else if (!(next == value)) mixed = true;
    }
    int checkState() const { return mixed ? 1 : (seen && value ? 2 : 0); }
};

bool familyAvailable(const QString &family)
{
    static const QStringList generic{QStringLiteral("sans-serif"), QStringLiteral("serif"),
                                     QStringLiteral("monospace"), QStringLiteral("cursive"),
                                     QStringLiteral("fantasy"), QStringLiteral("system-ui")};
    return generic.contains(family.toLower()) || QFontDatabase::hasFamily(family);
}

} // namespace

RichTextFormatState::RichTextFormatState(RichTextArea *area)
    : QObject(area), m_area(area)
{
}

void RichTextFormatState::refresh()
{
    RichTextDocument *doc = m_area->document();
    m_editable = doc && !m_area->readOnly() && !doc->readOnly();
    m_hasSelection = m_area->hasSelection();
    Tally<QString> family, link, alignment, list;
    Tally<double> size;
    Tally<bool> bold, italic, underline, strike, superscript, subscript;
    Tally<QColor> color, highlight;
    Tally<int> heading;
    if (doc) {
        const Selection selection = m_area->selection();
        const bool ordered = doc->compare(selection.anchor, selection.focus) <= 0;
        const Position start = ordered ? selection.anchor : selection.focus;
        const Position end = ordered ? selection.focus : selection.anchor;
        const DocumentDefaults defaults = doc->defaults();
        auto addFormat = [&](CharFormat f, const ParagraphFormat &pf) {
            if (!m_hasSelection && m_area->m_pendingFormat) f = m_area->m_pendingFormat->applied(f);
            f = doc->effectiveCharFormat(f, pf); // named styles, before heading and document defaults
            const Shaping::HeadingStyle style = Shaping::headingStyle(pf.headingLevel);
            family.add(f.family.isEmpty() ? defaults.family : f.family);
            size.add(f.sizePt > 0 ? f.sizePt : style.sizePt > 0 ? style.sizePt : defaults.sizePt);
            bold.add((f.weight > 0 ? f.weight : style.weight > 0 ? style.weight : 400) >= 600);
            italic.add(f.italic == 1);
            underline.add(f.underline == 1);
            strike.add(f.strike == 1);
            superscript.add(f.verticalAlign == VerticalAlign::Superscript);
            subscript.add(f.verticalAlign == VerticalAlign::Subscript);
            color.add(f.color);
            highlight.add(f.background);
            link.add(f.href);
        };
        auto addParagraph = [&](const Paragraph &p) {
            alignment.add(Codec::alignmentNames().at(int(p.format.alignment)));
            heading.add(p.format.headingLevel);
            const ListDefinition *def = doc->list(p.format.listId);
            list.add(!def ? QStringLiteral("none")
                          : isListKindOrdered(def->levels.value(p.format.listLevel).kind) ? QStringLiteral("number")
                                                                                        : QStringLiteral("bullet"));
        };
        const Paragraph *first = doc->paragraph(start.paragraph);
        const Paragraph *last = doc->paragraph(end.paragraph);
        if (first && last) {
            if (!m_hasSelection) {
                addFormat(doc->charFormat(doc->formatIndexAt(start)), first->format);
                addParagraph(*first);
            } else {
                const auto &flat = doc->paragraphsInOrder();
                for (int i = first->flatIndex; i <= last->flatIndex; ++i) {
                    const Paragraph &p = *flat.at(i);
                    addParagraph(p);
                    const int from = &p == first ? start.offset : 0;
                    const int to = &p == last ? end.offset : int(p.text.size());
                    if (p.text.isEmpty() || from == to) {
                        if (p.text.isEmpty()) addFormat(doc->charFormat(p.markFormat), p.format);
                        continue;
                    }
                    int offset = 0;
                    for (const Span &span : p.spans) {
                        if (offset + span.length > from && offset < to) addFormat(doc->charFormat(span.format), p.format);
                        offset += span.length;
                    }
                }
            }
        }
    }
    m_family = family.value;
    m_familyMixed = family.mixed;
    m_fontMissing = !family.mixed && !family.value.isEmpty() && !familyAvailable(family.value);
    m_size = size.value;
    m_sizeMixed = size.mixed;
    m_bold = bold.checkState();
    m_italic = italic.checkState();
    m_underline = underline.checkState();
    m_strike = strike.checkState();
    m_superscript = superscript.checkState();
    m_subscript = subscript.checkState();
    m_color = color.mixed ? QColor() : color.value;
    m_colorMixed = color.mixed;
    m_highlight = highlight.mixed ? QColor() : highlight.value;
    m_highlightMixed = highlight.mixed;
    m_link = link.mixed ? QString() : link.value;
    m_linkMixed = link.mixed;
    m_alignment = alignment.seen ? alignment.value : QStringLiteral("start");
    m_alignmentMixed = alignment.mixed;
    m_heading = heading.value;
    m_headingMixed = heading.mixed;
    m_list = list.mixed ? QStringLiteral("mixed") : list.seen ? list.value : QStringLiteral("none");
    emit changed();
}

} // namespace QindaTK
