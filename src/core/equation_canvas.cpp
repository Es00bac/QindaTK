// SPDX-License-Identifier: LGPL-3.0-or-later
#include "equation_canvas.h"

#include <QKeyEvent>
#include <QPainter>

namespace QindaTK {
using namespace RichText::Equation;

namespace {
constexpr double PxPerPt = 96.0 / 72.0;
} // namespace

EquationCanvas::EquationCanvas(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    relayout();
}

void EquationCanvas::setSource(const QString &source)
{
    if (source == m_source) return;
    m_source = source;
    ParseError problem;
    // A source that does not parse keeps the last good tree on screen.
    m_error = m_editor.setSource(source, &problem) ? QString() : problem.message;
    relayout();
    emit sourceChanged();
    emit caretChanged();
    update();
}

QString EquationCanvas::linearText() const { return RichText::Equation::linearText(denormalized(m_editor.root())); }

void EquationCanvas::setReadOnly(bool readOnly)
{
    if (m_readOnly == readOnly) return;
    m_readOnly = readOnly;
    emit appearanceChanged();
}

void EquationCanvas::setFontSizePt(double size)
{
    if (!(size >= 4 && size <= 144) || qFuzzyCompare(size, m_sizePt)) return;
    m_sizePt = size;
    relayout();
    emit appearanceChanged();
    update();
}

void EquationCanvas::setPadding(double padding)
{
    if (!(padding >= 0) || qFuzzyCompare(padding + 1, m_padding + 1)) return;
    m_padding = padding;
    relayout();
    emit appearanceChanged();
    update();
}

#define QTK_COLOR_SETTER(name, member)                  \
    void EquationCanvas::name(const QColor &color)      \
    {                                                   \
        if (member == color) return;                    \
        member = color;                                 \
        emit appearanceChanged();                       \
        update();                                       \
    }
QTK_COLOR_SETTER(setInkColor, m_ink)
QTK_COLOR_SETTER(setCaretColor, m_caret)
QTK_COLOR_SETTER(setSelectionColor, m_selection)
QTK_COLOR_SETTER(setPlaceholderColor, m_placeholder)
#undef QTK_COLOR_SETTER

double EquationCanvas::contentWidth() const { return m_box.width * scale() + 2 * m_padding; }
double EquationCanvas::contentHeight() const { return m_box.height() * scale() + 2 * m_padding; }

double EquationCanvas::scale() const { return PxPerPt; }

QPointF EquationCanvas::origin() const
{
    const double boxHeight = m_box.height() * scale();
    const double top = qMax(m_padding, (height() - boxHeight) / 2);
    return QPointF(m_padding, top + m_box.ascent * scale());
}

void EquationCanvas::relayout()
{
    m_box = layout(m_editor.root(), m_sizePt, true);
    setImplicitSize(contentWidth(), contentHeight());
}

bool EquationCanvas::apply(bool changed)
{
    if (changed) {
        m_source = m_editor.source();
        m_error.clear();
        relayout();
        emit sourceChanged();
        emit edited(m_source);
    }
    emit caretChanged();
    update();
    return changed;
}

bool EquationCanvas::moveCaret(const QString &direction, bool extend)
{
    static const QHash<QString, Editor::Move> moves{
        {QStringLiteral("left"), Editor::Move::Left}, {QStringLiteral("right"), Editor::Move::Right},
        {QStringLiteral("up"), Editor::Move::Up}, {QStringLiteral("down"), Editor::Move::Down},
        {QStringLiteral("home"), Editor::Move::Home}, {QStringLiteral("end"), Editor::Move::End}};
    const auto found = moves.constFind(direction);
    if (found == moves.constEnd()) return false;
    const bool moved = m_editor.move(*found, extend);
    apply(false);
    return moved;
}

bool EquationCanvas::insertText(const QString &text) { return !m_readOnly && apply(m_editor.insertText(text)); }
bool EquationCanvas::insertSymbol(const QString &symbolId) { return !m_readOnly && apply(m_editor.insertSymbol(symbolId)); }

bool EquationCanvas::backspace()
{
    if (m_readOnly) return false;
    const QString before = m_editor.source();
    const bool done = m_editor.backspace();
    apply(done && m_editor.source() != before);
    return done;
}

bool EquationCanvas::deleteForward()
{
    if (m_readOnly) return false;
    const QString before = m_editor.source();
    const bool done = m_editor.deleteForward();
    apply(done && m_editor.source() != before);
    return done;
}

void EquationCanvas::selectAll()
{
    m_editor.selectAll();
    apply(false);
}

bool EquationCanvas::setCaretAt(qreal x, qreal y, bool extend)
{
    if (m_box.stops.isEmpty()) return false;
    const QPointF o = origin();
    const QPointF point((x - o.x()) / scale(), (y - o.y()) / scale());
    m_editor.setCaret(Editor::caretAt(m_box, point), extend);
    apply(false);
    return true;
}

QRectF EquationCanvas::caretRectangle() const
{
    const QPointF o = origin();
    for (const Stop &stop : m_box.stops)
        if (stop.row == m_editor.caret().row && stop.index == m_editor.caret().index)
            return QRectF(o.x() + stop.x * scale(), o.y() + stop.top * scale(), 0, (stop.bottom - stop.top) * scale());
    return {};
}

void EquationCanvas::paint(QPainter *painter)
{
    const QPointF o = origin();
    const double s = scale();
    painter->setRenderHint(QPainter::Antialiasing);
    if (m_editor.hasSelection()) {
        double from = 0, to = 0, top = 0, bottom = 0;
        for (const Stop &stop : m_box.stops) {
            if (!(stop.row == m_editor.caret().row)) continue;
            if (stop.index == m_editor.anchor().index) from = stop.x;
            if (stop.index == m_editor.caret().index) to = stop.x, top = stop.top, bottom = stop.bottom;
        }
        painter->fillRect(QRectF(o.x() + qMin(from, to) * s, o.y() + top * s, std::abs(to - from) * s, (bottom - top) * s),
                          m_selection);
    }
    QPen dashed(m_placeholder, 1, Qt::DashLine);
    painter->setPen(dashed);
    painter->setBrush(Qt::NoBrush);
    for (const QRectF &slot : m_box.placeholders)
        painter->drawRect(QRectF(o.x() + slot.x() * s, o.y() + slot.y() * s, slot.width() * s, slot.height() * s));
    if (m_box.width > 0 && m_box.height() > 0)
        RichText::Equation::paint(painter, QRectF(o.x(), o.y() - m_box.ascent * s, m_box.width * s, m_box.height() * s), m_box, m_ink);
    if (hasActiveFocus() && !m_readOnly) {
        const QRectF caret = caretRectangle();
        if (caret.height() > 0)
            painter->fillRect(QRectF(caret.x() - 0.75, caret.y(), 1.5, caret.height()), m_caret);
    }
}

void EquationCanvas::keyPressEvent(QKeyEvent *event)
{
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    bool handled = true;
    switch (event->key()) {
    case Qt::Key_Left: moveCaret(QStringLiteral("left"), shift); break;
    case Qt::Key_Right: moveCaret(QStringLiteral("right"), shift); break;
    case Qt::Key_Up: moveCaret(QStringLiteral("up")); break;
    case Qt::Key_Down: moveCaret(QStringLiteral("down")); break;
    case Qt::Key_Home: moveCaret(QStringLiteral("home"), shift); break;
    case Qt::Key_End: moveCaret(QStringLiteral("end"), shift); break;
    case Qt::Key_Backspace: backspace(); break;
    case Qt::Key_Delete: deleteForward(); break;
    default:
        if (event->matches(QKeySequence::SelectAll)) selectAll();
        else if (!event->text().isEmpty() && event->text().at(0).isPrint()
                 && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            insertText(event->text());
        else handled = false;
    }
    event->setAccepted(handled);
}

void EquationCanvas::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    setCaretAt(event->position().x(), event->position().y(), event->modifiers() & Qt::ShiftModifier);
    event->accept();
}

void EquationCanvas::mouseMoveEvent(QMouseEvent *event)
{
    setCaretAt(event->position().x(), event->position().y(), true);
    event->accept();
}

} // namespace QindaTK
