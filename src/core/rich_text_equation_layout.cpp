// SPDX-License-Identifier: LGPL-3.0-or-later
// Equation box layout and painting (WP-052, D-114). A small TeX-like box
// model: rows on a common baseline, fractions and large operators centred
// on the math axis, scripts shifted and scaled, radicals and stretched
// fences. Units are points; fonts resolve against the 72-dpi layout device,
// so a point is a layout unit on every screen.
#include "rich_text_equation.h"
#include "rich_text_shaping_p.h"

#include <QCache>
#include <QFontMetricsF>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>

namespace QindaTK::RichText::Equation {

namespace {

// AGENT-NOTE: proportions after TeX's defaults (script 70 %, axis a quarter
// em, rule a twentieth). tst_rich_text_equations checks the relations, not
// font-dependent coordinates.
constexpr double ScriptScale = 0.7;
constexpr double LargeOperatorScale = 1.4;
constexpr double AxisEm = 0.25;
constexpr double RuleEm = 0.05;

QFont mathFont(double size, bool italic)
{
    QFont font(QFont(), Shaping::layoutDevice());
    font.setFamilies(mathFamilies());
    font.setPointSizeF(size);
    font.setItalic(italic);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

void place(Box &into, const Box &item, double x, double y)
{
    for (Glyph glyph : item.glyphs) {
        glyph.origin += QPointF(x, y);
        into.glyphs.append(glyph);
    }
    for (const QRectF &rule : item.rules) into.rules.append(rule.translated(x, y));
    for (const QPolygonF &stroke : item.strokes) into.strokes.append(stroke.translated(x, y));
    into.strokeWidth = qMax(into.strokeWidth, item.strokeWidth);
}

bool italicIdentifier(const QString &text)
{
    if (text.size() != 1) return false;
    const char16_t c = text.front().unicode();
    return (c < 128 && text.front().isLetter()) || (c >= 0x03b1 && c <= 0x03c9)
        || c == 0x03d1 || c == 0x03d5 || c == 0x03d6 || c == 0x03f1 || c == 0x03f5;
}

bool relation(const QString &text)
{
    static const QString relations = QStringLiteral("=<>≤≥≠≈≡∼≃∝≪≫→←⇒⇐⇔↦∈∉⊂⊃⊆⊇∣:");
    return text.size() == 1 && relations.contains(text);
}

bool binary(const QString &text)
{
    static const QString operators = QStringLiteral("+-±∓×⋅÷∗∘∪∩∧∨");
    return text.size() == 1 && operators.contains(text);
}

bool integral(const QString &text) { return text == u"∫" || text == u"∬" || text == u"∮"; }

double spaceEm(const QString &command)
{
    if (command == u",") return 3.0 / 18;
    if (command == u":") return 4.0 / 18;
    if (command == u";") return 5.0 / 18;
    if (command == u"!") return -3.0 / 18;
    if (command == u"quad") return 1.0;
    if (command == u"qquad") return 2.0;
    return 0;
}

Box atom(const QString &text, double size, bool italic)
{
    Box box;
    if (text.isEmpty()) return box;
    const QFontMetricsF metrics(mathFont(size, italic));
    const QRectF ink = metrics.tightBoundingRect(text);
    box.width = metrics.horizontalAdvance(text) + (italic ? 0.06 * size : 0.0); // italic correction
    box.ascent = qMax(-ink.top(), metrics.xHeight());
    box.descent = qMax(0.0, ink.bottom());
    box.glyphs.append({text, QPointF(0, 0), size, italic});
    return box;
}

// A glyph centred on the math axis: large operators and stretched fences.
Box centred(const QString &text, double glyphSize, double size)
{
    Box box;
    if (text.isEmpty()) return box;
    const QFontMetricsF metrics(mathFont(glyphSize, false));
    const QRectF ink = metrics.tightBoundingRect(text);
    const double baseline = -AxisEm * size - (ink.top() + ink.bottom()) / 2;
    box.glyphs.append({text, QPointF(0, baseline), glyphSize, false});
    box.width = metrics.horizontalAdvance(text);
    box.ascent = -(baseline + ink.top());
    box.descent = qMax(0.0, baseline + ink.bottom());
    return box;
}

class Builder {
public:
    explicit Builder(double base) : m_base(base) {}

    Box build(const Node &node, double size, int depth) const
    {
        switch (node.kind) {
        case NodeKind::Row: return row(node, size, depth);
        case NodeKind::Identifier: return atom(node.text, size, italicIdentifier(node.text));
        case NodeKind::Number:
        case NodeKind::Function:
        case NodeKind::Text: return atom(node.text, size, false);
        case NodeKind::Operator:
            return atom(node.command.isEmpty() && node.text == u"-" ? QStringLiteral("−") : displayText(node), size, false);
        case NodeKind::LargeOperator: return centred(node.text, size * LargeOperatorScale, size);
        case NodeKind::Space: {
            Box box;
            box.width = spaceEm(node.command) * size;
            return box;
        }
        case NodeKind::Fraction: return fraction(node, size, depth);
        case NodeKind::Root: return root(node, size, depth);
        case NodeKind::Scripts: return scripts(node, size, depth);
        case NodeKind::Fenced: return fenced(node, size, depth);
        }
        return {};
    }

private:
    double scriptSize(double size) const { return qMax(size * ScriptScale, m_base * 0.5); }

    Box row(const Node &node, double size, int depth) const
    {
        Box box;
        double x = 0;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const Node &child = node.children.at(i);
            const Box item = build(child, size, depth);
            double before = 0;
            double after = 0;
            const bool unary = i == 0 || node.children.at(i - 1).kind == NodeKind::Operator;
            if (child.kind == NodeKind::Operator) {
                if (relation(child.text)) before = after = 0.28 * size;
                else if (binary(child.text) && !unary) before = after = 0.22 * size;
                else if (child.text == u"," || child.text == u";") after = 0.17 * size;
            }
            const Node &head = child.kind == NodeKind::Scripts ? child.children.front() : child;
            if ((head.kind == NodeKind::Function || head.kind == NodeKind::LargeOperator) && i + 1 < node.children.size())
                after += 0.17 * size;
            x += before;
            place(box, item, x, 0);
            x += item.width + after;
            box.ascent = qMax(box.ascent, item.ascent);
            box.descent = qMax(box.descent, item.descent);
        }
        box.width = qMax(0.0, x);
        return box;
    }

    Box fraction(const Node &node, double size, int depth) const
    {
        const double part = depth == 0 ? size : scriptSize(size);
        const Box num = build(node.children.at(0), part, depth + 1);
        const Box den = build(node.children.at(1), part, depth + 1);
        const double axis = AxisEm * size;
        const double rule = qMax(RuleEm * size, 0.4);
        const double gap = 0.15 * size;
        const double pad = 0.1 * size;
        Box box;
        box.width = qMax(num.width, den.width) + 2 * pad;
        const double numBase = -(axis + rule / 2 + gap + num.descent);
        const double denBase = -axis + rule / 2 + gap + den.ascent;
        place(box, num, (box.width - num.width) / 2, numBase);
        place(box, den, (box.width - den.width) / 2, denBase);
        box.rules.append(QRectF(0, -axis - rule / 2, box.width, rule));
        box.ascent = -numBase + num.ascent;
        box.descent = qMax(0.0, denBase + den.descent);
        return box;
    }

    Box root(const Node &node, double size, int depth) const
    {
        const Box radicand = build(node.children.at(0), size, depth);
        const double rule = qMax(RuleEm * size, 0.4);
        const double gap = 0.12 * size;
        const double sign = 0.6 * size;
        const double top = -(radicand.ascent + gap + rule);   // top edge of the overbar
        const double bottom = radicand.descent + 0.05 * size;
        const double tick = bottom - (bottom - top) * 0.4;
        Box index;
        double indent = 0;
        if (node.children.size() > 1) {
            index = build(node.children.at(1), qMax(size * 0.5, m_base * 0.4), depth + 1);
            indent = qMax(0.0, index.width - 0.35 * sign);
        }
        Box box;
        box.strokeWidth = rule;
        box.strokes.append(QPolygonF{QPointF(indent, tick), QPointF(indent + 0.2 * sign, tick - 0.08 * size),
                                     QPointF(indent + 0.5 * sign, bottom), QPointF(indent + sign, top + rule / 2)});
        box.rules.append(QRectF(indent + sign, top, radicand.width + 0.1 * size, rule));
        place(box, radicand, indent + sign + 0.05 * size, 0);
        double ascent = -top + 0.05 * size;
        if (node.children.size() > 1) {
            const double indexBase = tick - 0.15 * size - index.descent;
            place(box, index, 0, indexBase);
            ascent = qMax(ascent, -(indexBase - index.ascent));
        }
        box.width = indent + sign + radicand.width + 0.1 * size;
        box.ascent = ascent;
        box.descent = bottom;
        return box;
    }

    Box scripts(const Node &node, double size, int depth) const
    {
        const Node &baseNode = node.children.at(0);
        const Box base = build(baseNode, size, depth);
        const double small = scriptSize(size);
        std::size_t next = 1;
        const Box sub = node.hasSub ? build(node.children.at(next++), small, depth + 1) : Box{};
        const Box sup = node.hasSup ? build(node.children.at(next), small, depth + 1) : Box{};
        Box box;
        if (baseNode.kind == NodeKind::LargeOperator && !integral(baseNode.text)) {
            // Limits above and below, centred.
            box.width = qMax(base.width, qMax(sub.width, sup.width));
            const double supBase = -(base.ascent + 0.1 * size + sup.descent);
            const double subBase = base.descent + 0.1 * size + sub.ascent;
            place(box, base, (box.width - base.width) / 2, 0);
            box.ascent = base.ascent;
            box.descent = base.descent;
            if (node.hasSup) {
                place(box, sup, (box.width - sup.width) / 2, supBase);
                box.ascent = qMax(box.ascent, -supBase + sup.ascent);
            }
            if (node.hasSub) {
                place(box, sub, (box.width - sub.width) / 2, subBase);
                box.descent = qMax(box.descent, subBase + sub.descent);
            }
            return box;
        }
        double supBase = -qMax(0.42 * size, base.ascent - 0.5 * small);
        double subBase = qMax(0.2 * size, base.descent + 0.1 * size);
        if (node.hasSub && node.hasSup) {
            const double gap = (subBase - sub.ascent) - (supBase + sup.descent);
            if (gap < 0.1 * size) {
                const double extra = (0.1 * size - gap) / 2;
                supBase -= extra;
                subBase += extra;
            }
        }
        const double x = base.width + 0.03 * size;
        place(box, base, 0, 0);
        box.ascent = base.ascent;
        box.descent = base.descent;
        if (node.hasSup) {
            place(box, sup, x, supBase);
            box.ascent = qMax(box.ascent, -supBase + sup.ascent);
        }
        if (node.hasSub) {
            place(box, sub, x, subBase);
            box.descent = qMax(box.descent, subBase + sub.descent);
        }
        box.width = x + qMax(sub.width, sup.width);
        return box;
    }

    Box fenced(const Node &node, double size, int depth) const
    {
        const Box content = build(node.children.at(0), size, depth);
        const double axis = AxisEm * size;
        const double half = qMax(content.ascent - axis, content.descent + axis) + 0.05 * size;
        auto delimiter = [&](const QString &source) {
            const QString text = displayDelimiter(source);
            if (text.isEmpty()) return Box{};
            const QRectF ink = QFontMetricsF(mathFont(size, false)).tightBoundingRect(text);
            const double scale = ink.height() > 0 ? qMax(1.0, 2 * half / ink.height()) : 1.0;
            return centred(text, size * scale, size);
        };
        const Box open = delimiter(node.text);
        const Box close = delimiter(node.close);
        Box box;
        place(box, open, 0, 0);
        const double contentX = open.width + 0.05 * size;
        place(box, content, contentX, 0);
        place(box, close, contentX + content.width + 0.05 * size, 0);
        box.width = contentX + content.width + 0.05 * size + close.width;
        box.ascent = qMax(content.ascent, qMax(open.ascent, close.ascent));
        box.descent = qMax(content.descent, qMax(open.descent, close.descent));
        return box;
    }

    double m_base;
};

} // namespace

QStringList mathFamilies()
{
    return {QStringLiteral("STIX Two Math"), QStringLiteral("Latin Modern Math"), QStringLiteral("DejaVu Serif"),
            QStringLiteral("serif")};
}

Box layout(const Node &root, double sizePt)
{
    return Builder(sizePt).build(root, sizePt, 0);
}

void paint(QPainter *painter, const QRectF &rect, const Box &box, const QColor &ink)
{
    if (!(box.width > 0) || !(box.height() > 0) || rect.isEmpty()) return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / box.width, rect.height() / box.height());
    painter->translate(0, box.ascent);
    QPainterPath outlines;
    for (const Glyph &glyph : box.glyphs) outlines.addText(glyph.origin, mathFont(glyph.sizePt, glyph.italic), glyph.text);
    painter->fillPath(outlines, ink);
    for (const QRectF &rule : box.rules) painter->fillRect(rule, ink);
    QPen pen(ink, box.strokeWidth);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    for (const QPolygonF &stroke : box.strokes) painter->drawPolyline(stroke);
    painter->restore();
}

bool paintObject(QPainter *painter, const QRectF &rect, const RichObject &object, const QColor &ink)
{
    // AGENT-GUARD: tiles paint on the scene graph's render thread; the
    // cache is shared, so it is locked. Boxes scale with the object's rect,
    // so one reference size serves every zoom.
    static QMutex mutex;
    static QCache<QString, Box> cache(256);
    Box box;
    {
        const QMutexLocker lock(&mutex);
        if (const Box *cached = cache.object(object.source)) {
            box = *cached;
        } else {
            const Parsed parsed = parse(object.source);
            if (!parsed.root) return false;
            box = layout(*parsed.root, 12.0);
            cache.insert(object.source, new Box(box));
        }
    }
    paint(painter, rect, box, ink);
    return box.width > 0;
}

} // namespace QindaTK::RichText::Equation
