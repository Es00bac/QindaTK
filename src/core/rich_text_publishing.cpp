// SPDX-License-Identifier: LGPL-3.0-or-later
// Document-independent rules of named styles, text frames and merged cells.
#include "rich_text_publishing.h"

#include <cmath>

namespace QindaTK::RichText {

namespace {

void mergeParagraph(ParagraphFormatPatch &into, const ParagraphFormatPatch &later)
{
    if (later.alignment) into.alignment = later.alignment;
    if (later.direction) into.direction = later.direction;
    if (later.indentStartPt) into.indentStartPt = later.indentStartPt;
    if (later.indentEndPt) into.indentEndPt = later.indentEndPt;
    if (later.firstLineIndentPt) into.firstLineIndentPt = later.firstLineIndentPt;
    if (later.spaceBeforePt) into.spaceBeforePt = later.spaceBeforePt;
    if (later.spaceAfterPt) into.spaceAfterPt = later.spaceAfterPt;
    if (later.lineHeight) into.lineHeight = later.lineHeight;
    if (later.lineHeightPt) into.lineHeightPt = later.lineHeightPt;
    if (later.headingLevel) into.headingLevel = later.headingLevel;
    if (later.keepWithNext) into.keepWithNext = later.keepWithNext;
    if (later.keepTogether) into.keepTogether = later.keepTogether;
}

bool inRange(const std::optional<double> &value, double min, double max)
{
    return !value || (std::isfinite(*value) && *value >= min && *value <= max);
}

bool validColor(const std::optional<QColor> &color) { return !color || color->isValid(); }

bool validFlag(const std::optional<qint8> &flag) { return !flag || *flag == 0 || *flag == 1; }

bool finite(double value, double limit) { return std::isfinite(value) && std::abs(value) <= limit; }

} // namespace

void TableStyle::merge(const TableStyle &later)
{
    if (later.borderPt) borderPt = later.borderPt;
    if (later.paddingPt) paddingPt = later.paddingPt;
    if (later.headerBackground) headerBackground = later.headerBackground;
    if (later.bandBackground) bandBackground = later.bandBackground;
}

QString styleKindName(StyleKind kind)
{
    switch (kind) {
    case StyleKind::Paragraph: return QStringLiteral("paragraph");
    case StyleKind::Character: return QStringLiteral("character");
    case StyleKind::Table: return QStringLiteral("table");
    }
    return {};
}

std::optional<StyleKind> styleKindFromName(const QString &name)
{
    if (name == u"paragraph") return StyleKind::Paragraph;
    if (name == u"character") return StyleKind::Character;
    if (name == u"table") return StyleKind::Table;
    return std::nullopt;
}

QStringList styleChain(const StyleSheet &styles, const QString &id)
{
    QStringList chain;
    QSet<QString> seen;
    QString current = id;
    while (!current.isEmpty()) {
        const auto found = styles.constFind(current);
        if (found == styles.constEnd() || seen.contains(current)) return {};
        seen.insert(current);
        chain.prepend(current);
        current = found->parentId;
    }
    return chain;
}

QSet<QString> styleFamily(const StyleSheet &styles, const QString &id)
{
    QSet<QString> family{id};
    for (auto it = styles.constBegin(); it != styles.constEnd(); ++it)
        if (it.key() != id && styleChain(styles, it.key()).contains(id)) family.insert(it.key());
    return family;
}

CharFormatPatch resolvedCharacter(const StyleSheet &styles, const QString &id)
{
    CharFormatPatch result;
    for (const QString &style : styleChain(styles, id)) result.merge(styles.value(style).character);
    return result;
}

ParagraphFormatPatch resolvedParagraph(const StyleSheet &styles, const QString &id)
{
    ParagraphFormatPatch result;
    for (const QString &style : styleChain(styles, id)) mergeParagraph(result, styles.value(style).paragraph);
    return result;
}

TableStyle resolvedTable(const StyleSheet &styles, const QString &id)
{
    TableStyle result;
    for (const QString &style : styleChain(styles, id)) result.merge(styles.value(style).table);
    return result;
}

CharFormat inheritCharacter(CharFormat direct, const CharFormatPatch &style)
{
    if (direct.family.isEmpty() && style.family) direct.family = *style.family;
    if (!(direct.sizePt > 0) && style.sizePt) direct.sizePt = *style.sizePt;
    if (direct.weight <= 0 && style.weight) direct.weight = *style.weight;
    if (direct.italic < 0 && style.italic) direct.italic = *style.italic;
    if (direct.underline < 0 && style.underline) direct.underline = *style.underline;
    if (direct.strike < 0 && style.strike) direct.strike = *style.strike;
    if (!direct.color.isValid() && style.color) direct.color = *style.color;
    if (!direct.background.isValid() && style.background) direct.background = *style.background;
    if (direct.verticalAlign == VerticalAlign::Normal && style.verticalAlign)
        direct.verticalAlign = *style.verticalAlign;
    return direct;
}

CharFormat effectiveCharFormat(const StyleSheet &styles, const CharFormat &direct,
                               const ParagraphFormat &paragraph)
{
    if (styles.isEmpty()) return direct;
    CharFormat format = direct;
    if (!direct.styleId.isEmpty()) format = inheritCharacter(format, resolvedCharacter(styles, direct.styleId));
    if (!paragraph.styleId.isEmpty())
        format = inheritCharacter(format, resolvedCharacter(styles, paragraph.styleId));
    return format;
}

ParagraphFormat materializedFormat(const StyleSheet &styles, const ParagraphFormat &format)
{
    if (format.styleId.isEmpty() || !styles.contains(format.styleId)) return format;
    const ParagraphFormat base = resolvedParagraph(styles, format.styleId).applied(ParagraphFormat{});
    ParagraphFormat result = format;
    const quint32 direct = format.overrides & AllStyleProperties;
    result.overrides = direct;
    if (!(direct & StyleAlignment)) result.alignment = base.alignment;
    if (!(direct & StyleDirection)) result.direction = base.direction;
    if (!(direct & StyleIndentStart)) result.indentStartPt = base.indentStartPt;
    if (!(direct & StyleIndentEnd)) result.indentEndPt = base.indentEndPt;
    if (!(direct & StyleFirstLineIndent)) result.firstLineIndentPt = base.firstLineIndentPt;
    if (!(direct & StyleSpaceBefore)) result.spaceBeforePt = base.spaceBeforePt;
    if (!(direct & StyleSpaceAfter)) result.spaceAfterPt = base.spaceAfterPt;
    if (!(direct & StyleLineHeight)) result.lineHeight = base.lineHeight;
    if (!(direct & StyleLineHeightPt)) result.lineHeightPt = base.lineHeightPt;
    if (!(direct & StyleHeadingLevel)) result.headingLevel = base.headingLevel;
    if (!(direct & StyleKeepWithNext)) result.keepWithNext = base.keepWithNext;
    if (!(direct & StyleKeepTogether)) result.keepTogether = base.keepTogether;
    return result;
}

QString styleProblem(const NamedStyle &style)
{
    const QString name = style.name.trimmed();
    if (style.id.isEmpty() || style.id.size() > 128) return QStringLiteral("Invalid style identifier");
    if (name.isEmpty() || name.size() > 128) return QStringLiteral("A style needs a name of 1 to 128 characters");
    if (style.parentId == style.id) return QStringLiteral("A style cannot inherit from itself");
    const CharFormatPatch &c = style.character;
    const ParagraphFormatPatch &p = style.paragraph;
    const bool characterOk = (!c.family || !c.family->isEmpty()) && inRange(c.sizePt, 1, 1638)
        && (!c.weight || (*c.weight >= 1 && *c.weight <= 1000)) && validFlag(c.italic)
        && validFlag(c.underline) && validFlag(c.strike) && validColor(c.color)
        && validColor(c.background) && !c.href && !c.styleId;
    const bool paragraphOk = inRange(p.indentStartPt, -1440, 1440) && inRange(p.indentEndPt, -1440, 1440)
        && inRange(p.firstLineIndentPt, -1440, 1440) && inRange(p.spaceBeforePt, 0, 1440)
        && inRange(p.spaceAfterPt, 0, 1440) && inRange(p.lineHeight, 0.25, 10)
        && inRange(p.lineHeightPt, 0, 1440) && (!p.headingLevel || (*p.headingLevel >= 0 && *p.headingLevel <= 6))
        && !p.listId && !p.listLevel && !p.pageBreakBefore && !p.styleId;
    const TableStyle &t = style.table;
    const bool tableOk = inRange(t.borderPt, 0, 72) && inRange(t.paddingPt, 0, 144)
        && validColor(t.headerBackground) && validColor(t.bandBackground);
    if (!characterOk || !paragraphOk || !tableOk) return QStringLiteral("Style property out of range");
    const bool fits = style.kind == StyleKind::Paragraph ? t.isEmpty()
        : style.kind == StyleKind::Character ? p.isEmpty() && t.isEmpty()
                                             : p.isEmpty() && c.isEmpty();
    return fits ? QString() : QStringLiteral("Property does not belong to a %1 style").arg(styleKindName(style.kind));
}

QString styleSheetProblem(const StyleSheet &styles)
{
    if (styles.size() > MaximumStyles) return QStringLiteral("Too many styles");
    QSet<QString> names;
    for (auto it = styles.constBegin(); it != styles.constEnd(); ++it) {
        const NamedStyle &style = it.value();
        if (it.key() != style.id) return QStringLiteral("Invalid style identifier");
        if (const QString problem = styleProblem(style); !problem.isEmpty()) return problem;
        const QString folded = style.name.trimmed().toCaseFolded();
        if (names.contains(folded)) return QStringLiteral("Style name already used: %1").arg(style.name.trimmed());
        names.insert(folded);
        if (style.parentId.isEmpty()) continue;
        const auto parent = styles.constFind(style.parentId);
        if (parent == styles.constEnd()) return QStringLiteral("Unknown parent style");
        if (parent->kind != style.kind) return QStringLiteral("A parent style must be of the same kind");
        if (styleChain(styles, style.id).isEmpty()) return QStringLiteral("Style inheritance would form a cycle");
    }
    return {};
}

const TextFrame *FrameSet::frame(const QString &id) const
{
    const int index = indexOf(id);
    return index < 0 ? nullptr : &frames.at(index);
}

int FrameSet::indexOf(const QString &id) const
{
    if (id.isEmpty()) return -1;
    for (int i = 0; i < frames.size(); ++i)
        if (frames.at(i).id == id) return i;
    return -1;
}

QString FrameSet::previous(const QString &id) const
{
    if (id.isEmpty()) return {};
    for (const TextFrame &f : frames)
        if (f.next == id) return f.id;
    return {};
}

QString FrameSet::threadHead(const QString &id) const
{
    QString head = id;
    for (int guard = 0; guard <= frames.size(); ++guard) {
        const QString before = previous(head);
        if (before.isEmpty() || before == id) return head;
        head = before;
    }
    return head;
}

QStringList FrameSet::storyChain() const
{
    QStringList chain;
    for (const TextFrame *f = frame(storyStart); f && !chain.contains(f->id); f = frame(f->next))
        chain.append(f->id);
    return chain;
}

int FrameSet::pageCount() const
{
    int count = 0;
    for (const TextFrame &f : frames) count = qMax(count, f.page + 1);
    return count;
}

QString frameSetProblem(const FrameSet &set)
{
    if (set.frames.size() > MaximumFrames) return QStringLiteral("Too many text frames");
    QSet<QString> ids;
    QSet<QString> targets;
    for (const TextFrame &f : set.frames) {
        if (f.id.isEmpty() || f.id.size() > 64 || ids.contains(f.id)) return QStringLiteral("Invalid text frame identifier");
        ids.insert(f.id);
        const double inner = qMin(f.widthPt, f.heightPt) - 2 * f.insetPt;
        const double column = (f.widthPt - 2 * f.insetPt - (f.columns - 1) * f.gutterPt) / qMax(1, f.columns);
        if (f.page < 0 || f.page >= MaximumFramePages || !finite(f.xPt, 14400) || !finite(f.yPt, 14400)
            || !finite(f.widthPt, 14400) || !finite(f.heightPt, 14400) || f.widthPt < 18 || f.heightPt < 18
            || f.columns < 1 || f.columns > 24 || !finite(f.gutterPt, 1440) || f.gutterPt < 0
            || !finite(f.insetPt, 1440) || f.insetPt < 0 || inner < 18 || column < 9)
            return QStringLiteral("Invalid text frame geometry");
    }
    for (const TextFrame &f : set.frames) {
        if (f.next.isEmpty()) continue;
        const TextFrame *next = set.frame(f.next);
        if (!next || f.next == f.id) return QStringLiteral("Unknown next text frame");
        if (targets.contains(f.next)) return QStringLiteral("A text frame can follow only one frame");
        targets.insert(f.next);
        if (next->page < f.page) return QStringLiteral("A thread cannot continue on an earlier page");
    }
    for (const TextFrame &f : set.frames) {
        int steps = 0;
        for (const TextFrame *walk = set.frame(f.next); walk; walk = set.frame(walk->next))
            if (walk->id == f.id || ++steps > set.frames.size()) return QStringLiteral("Text frame threads cannot form a cycle");
    }
    if (!set.storyStart.isEmpty() && (!set.frame(set.storyStart) || targets.contains(set.storyStart)))
        return QStringLiteral("The story must start in the first frame of a thread");
    return {};
}

QString mapTableGrid(const Table &table, TableGridMap *map)
{
    const int rows = int(table.rows.size());
    const int columns = int(table.columnWeights.size());
    map->rows = rows;
    map->columns = columns;
    map->anchor = QVector<int>(rows * columns, -1);
    QString problem;
    auto note = [&problem](const QString &message) { if (problem.isEmpty()) problem = message; };
    for (int r = 0; r < rows; ++r) {
        const TableRow &row = table.rows.at(r);
        if (row.cells.size() != columns) note(QStringLiteral("Invalid table row"));
        for (int c = 0; c < columns; ++c) {
            const int slot = r * columns + c;
            if (c >= row.cells.size()) {
                if (map->anchor.at(slot) < 0) map->anchor[slot] = slot;
                continue;
            }
            const TableCell &cell = row.cells.at(c);
            if (map->anchor.at(slot) >= 0) {
                if (!cell.paragraphs.isEmpty() || cell.rowSpan != 1 || cell.columnSpan != 1)
                    note(QStringLiteral("A merged cell covers a cell with content"));
                continue;
            }
            map->anchor[slot] = slot;
            if (cell.paragraphs.isEmpty()) note(QStringLiteral("Invalid table cell"));
            const int rowSpan = cell.rowSpan;
            const int columnSpan = cell.columnSpan;
            if (rowSpan < 1 || columnSpan < 1 || r + rowSpan > rows || c + columnSpan > columns) {
                note(QStringLiteral("A merged cell leaves the table"));
                continue;
            }
            if (r < table.headerRows && r + rowSpan > table.headerRows)
                note(QStringLiteral("A merged cell cannot cross the header rows"));
            for (int dr = 0; dr < rowSpan; ++dr)
                for (int dc = 0; dc < columnSpan; ++dc) {
                    const int covered = (r + dr) * columns + c + dc;
                    if (covered == slot) continue;
                    if (map->anchor.at(covered) >= 0) {
                        note(QStringLiteral("Merged cells overlap"));
                        continue;
                    }
                    map->anchor[covered] = slot;
                }
        }
    }
    return problem;
}

} // namespace QindaTK::RichText
