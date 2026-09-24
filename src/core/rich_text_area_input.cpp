// SPDX-License-Identifier: LGPL-3.0-or-later
// RichTextArea input: keys, input method, pointer and wheel.
#include "rich_text_area.h"
#include "rich_text_format_state.h"
#include "rich_text_graphemes.h"

#include <QAccessible>
#include <QDateTime>
#include <QCursor>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QStyleHints>
#include <QTextBoundaryFinder>

namespace QindaTK {
using namespace RichText;

void RichTextArea::setSelection(const Selection &selection, bool ensureVisible)
{
    if (!m_document) return;
    m_anchor.position = m_document->clampPosition(selection.anchor);
    m_focus.position = m_document->clampPosition(selection.focus);
    m_focus.position.affinity = selection.focus.affinity;
    selectionUpdated(ensureVisible);
}

void RichTextArea::selectionUpdated(bool ensureVisible)
{
    m_navigatedPage = -1;
    m_pendingFormat.reset();
    m_formatState->refresh();
    restartBlink();
    if (ensureVisible) ensureCaretVisible();
    emit caretChanged();
    emit selectionChanged();
    emit repaintRequested(-1);
    if (QAccessible::isActive()) {
        QAccessibleTextCursorEvent cursor(this, cursorPosition());
        QAccessible::updateAccessibility(&cursor);
        QAccessibleTextSelectionEvent selected(this, selectionStart(), selectionEnd());
        QAccessible::updateAccessibility(&selected);
    }
    if (hasActiveFocus()) QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
}

void RichTextArea::moveCaret(const Position &target, bool extend, bool keepGoal)
{
    if (!m_document || !target.isValid()) return;
    if (!keepGoal) m_goalX = -1;
    m_focus.position = m_document->clampPosition(target);
    m_focus.position.affinity = target.affinity;
    if (!extend) m_anchor.position = m_focus.position;
    selectionUpdated(true);
}

void RichTextArea::onHistoryNavigated(const Selection &selection)
{
    if (!hasActiveFocus() && !m_commandInFlight) return;
    if (selection.anchor.isValid() && m_document->isValidPosition(selection.anchor)
        && m_document->isValidPosition(selection.focus))
        setSelection(selection);
}

void RichTextArea::selectWordAt(const Position &position)
{
    const Paragraph *p = m_document->paragraph(position.paragraph);
    if (!p) return;
    QTextBoundaryFinder words(QTextBoundaryFinder::Word, p->text);
    words.setPosition(position.offset);
    int start = words.isAtBoundary() && (words.boundaryReasons() & QTextBoundaryFinder::StartOfItem)
        ? position.offset : words.toPreviousBoundary();
    words.setPosition(position.offset);
    int end = words.toNextBoundary();
    if (start < 0) start = 0;
    if (end < 0) end = int(p->text.size());
    setSelection({{p->id, start}, {p->id, end}});
}

void RichTextArea::selectParagraphAt(const Position &position)
{
    setSelection({m_document->paragraphStart(position.paragraph), m_document->paragraphEnd(position.paragraph)});
}

void RichTextArea::selectAll()
{
    if (m_document) setSelection({m_document->startPosition(), m_document->endPosition()});
}

void RichTextArea::keyPressEvent(QKeyEvent *event)
{
    if (!m_document) {
        event->ignore();
        return;
    }
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool word = event->modifiers() & Qt::ControlModifier;
    auto handled = [event] { event->accept(); };
    if (event == QKeySequence::Undo) return invokeCommand(QStringLiteral("edit.undo")), handled();
    if (event == QKeySequence::Redo) return invokeCommand(QStringLiteral("edit.redo")), handled();
    if (event == QKeySequence::Copy) return invokeCommand(QStringLiteral("edit.copy")), handled();
    if (event == QKeySequence::Cut) return invokeCommand(QStringLiteral("edit.cut")), handled();
    if (event == QKeySequence::Paste) return invokeCommand(QStringLiteral("edit.paste")), handled();
    if (event == QKeySequence::SelectAll) return selectAll(), handled();
    if (event == QKeySequence::Bold) return invokeCommand(QStringLiteral("text.bold")), handled();
    if (event == QKeySequence::Italic) return invokeCommand(QStringLiteral("text.italic")), handled();
    if (event == QKeySequence::Underline) return invokeCommand(QStringLiteral("text.underline")), handled();
    // The other shortcuts the command table advertises (rich_text_area_commands.cpp).
    if (word && !shift && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
        return invokeCommand(QStringLiteral("insert.pageBreak")), handled();
    if (word && !shift && event->key() == Qt::Key_K) {
        if (commandState(QStringLiteral("text.link")).value(QStringLiteral("enabled")).toBool()) emit linkRequested();
        return handled();
    }
    const Position caret = m_focus.position;
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const bool right = event->key() == Qt::Key_Right;
        if (!shift && hasSelection() && !word) {
            // Collapse to the visual side of the selection, as text fields do.
            const bool ordered = m_document->compare(m_anchor.position, m_focus.position) <= 0;
            const bool rtl = m_layout->caretBox(caret).rightToLeft;
            const bool toEnd = right != rtl;
            moveCaret(toEnd == ordered ? m_focus.position : m_anchor.position, false);
            return handled();
        }
        Position target = m_layout->visualMove(caret, right);
        if (word) {
            const Paragraph *p = m_document->paragraph(caret.paragraph);
            const bool forward = right != m_layout->caretBox(caret).rightToLeft;
            QTextBoundaryFinder words(QTextBoundaryFinder::Word, p->text);
            words.setPosition(caret.offset);
            const int next = forward ? words.toNextBoundary() : words.toPreviousBoundary();
            if (next >= 0 && next != caret.offset) target = {p->id, next};
        }
        moveCaret(target, shift);
        return handled();
    }
    case Qt::Key_Up:
    case Qt::Key_Down: {
        qreal goal = m_goalX;
        const Position target = m_layout->lineMove(caret, event->key() == Qt::Key_Up ? -1 : 1, &goal);
        m_goalX = goal;
        moveCaret(target, shift, true);
        return handled();
    }
    case Qt::Key_Home:
    case Qt::Key_End: {
        const bool end = event->key() == Qt::Key_End;
        const Position target = word ? (end ? m_document->endPosition() : m_document->startPosition())
                                     : m_layout->lineEdge(caret, end);
        moveCaret(target, shift);
        return handled();
    }
    case Qt::Key_PageUp:
    case Qt::Key_PageDown: {
        const qreal delta = (event->key() == Qt::Key_PageUp ? -1 : 1) * height() * 0.9;
        const QRectF rect = cursorRectangle();
        setContentY(m_contentY + delta);
        moveCaret(positionAt(QPointF(rect.x(), qBound<qreal>(1, rect.center().y(), height() - 1))), shift);
        return handled();
    }
    case Qt::Key_Backspace:
        deleteBackward(word);
        return handled();
    case Qt::Key_Delete:
        deleteForward(word);
        return handled();
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (shift) insertText(QString(QChar(LineBreakChar)));
        else newParagraph();
        return handled();
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        if (tabKey(event->key() == Qt::Key_Backtab || shift)) return handled();
        event->ignore();
        return;
    case Qt::Key_Escape:
        event->ignore();
        return;
    default:
        break;
    }
    const QString text = event->text();
    if (!text.isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
        && (text.at(0).isPrint() || text.at(0).isSurrogate())) {
        insertText(text);
        return handled();
    }
    event->ignore();
}

void RichTextArea::clearPreedit()
{
    if (m_preedit.isEmpty()) return;
    m_preedit.clear();
    m_preeditCursor = -1;
    m_layout->setPreedit({}, {}, {});
    emit preeditChanged();
}

void RichTextArea::commitPreedit()
{
    if (m_preedit.isEmpty()) return;
    const QString text = m_preedit;
    clearPreedit();
    insertText(text);
    QGuiApplication::inputMethod()->reset();
}

void RichTextArea::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_document || m_readOnly || m_document->readOnly()) {
        event->ignore();
        return;
    }
    const bool gettingInput = !event->commitString().isEmpty() || event->preeditString() != m_preedit
        || event->replacementLength() > 0;
    // Composition over a selection replaces it; the replacement and the
    // commit land in one undo step (merge key "ime").
    if (gettingInput && hasSelection()) {
        edit(tr("Typing"), QStringLiteral("ime"), [this](RichTextDocument &doc) {
            return doc.deleteRange(m_anchor.position, m_focus.position);
        });
    }
    if (!event->commitString().isEmpty() || event->replacementLength() > 0) {
        clearPreedit();
        const Position caret = m_focus.position;
        const Paragraph *p = m_document->paragraph(caret.paragraph);
        const int from = qBound(0, caret.offset + event->replacementStart(), int(p->text.size()));
        const int to = qBound(from, from + event->replacementLength(), int(p->text.size()));
        const QString commit = event->commitString();
        edit(tr("Typing"), QStringLiteral("ime"), [&](RichTextDocument &doc) -> std::optional<Position> {
            Position at{caret.paragraph, from};
            if (to > from) {
                const auto removed = doc.deleteRange(at, {caret.paragraph, to});
                if (!removed) return std::nullopt;
                at = *removed;
            }
            if (commit.isEmpty()) return at;
            int format = doc.formatIndexAt(at);
            if (m_pendingFormat) format = doc.internFormat(m_pendingFormat->applied(doc.charFormat(format)));
            return doc.insertText(at, commit, format);
        });
    }
    for (const QInputMethodEvent::Attribute &attribute : event->attributes())
        if (attribute.type == QInputMethodEvent::Selection && event->preeditString().isEmpty()) {
            const Position caret = m_focus.position;
            const Selection wanted{{caret.paragraph, attribute.start}, {caret.paragraph, attribute.start + attribute.length}};
            if (!(wanted.anchor == m_anchor.position) || !(wanted.focus == m_focus.position)) setSelection(wanted);
        }
    if (event->preeditString() != m_preedit) {
        m_preedit = event->preeditString();
        m_preeditCursor = int(m_preedit.size());
        QList<QTextLayout::FormatRange> formats;
        for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
            if (attribute.type == QInputMethodEvent::Cursor) m_preeditCursor = attribute.start;
            if (attribute.type == QInputMethodEvent::TextFormat) {
                const QTextCharFormat format = qvariant_cast<QTextFormat>(attribute.value).toCharFormat();
                if (format.isValid()) formats.append({attribute.start, attribute.length, format});
            }
        }
        m_layout->setPreedit(m_focus.position, m_preedit, formats, m_preeditCursor);
        emit preeditChanged();
        emit caretChanged();
        emit repaintRequested(-1);
    }
    restartBlink();
    // AGENT-GUARD: input contexts (IBus, Fcitx) send empty events on focus
    // changes; only real input scrolls to the caret, or a view navigated with
    // goToPage jumps back (tst_rich_text_editing pageNavigationKeepsTheSelection).
    if (gettingInput) ensureCaretVisible();
    QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImAnchorRectangle);
    event->accept();
}

QVariant RichTextArea::inputMethodQuery(Qt::InputMethodQuery query) const
{
    const Paragraph *p = m_document ? m_document->paragraph(m_focus.position.paragraph) : nullptr;
    switch (query) {
    case Qt::ImEnabled: return !m_readOnly && m_document && !m_document->readOnly();
    case Qt::ImReadOnly: return m_readOnly || !m_document || m_document->readOnly();
    case Qt::ImHints: return int(Qt::ImhMultiLine);
    case Qt::ImCursorRectangle: return cursorRectangle();
    case Qt::ImAnchorRectangle: {
        if (!m_layout->isLaidOut(m_anchor.position)) return cursorRectangle();
        const CaretBox anchor = m_layout->caretBox(m_anchor.position);
        if (anchor.page < 0) return cursorRectangle();
        return QRectF(pageOrigin(anchor.page) + anchor.rect.topLeft() * m_zoom, QSizeF(1, anchor.rect.height() * m_zoom));
    }
    case Qt::ImFont: {
        QFont font;
        if (m_document) font.setPointSizeF(m_document->defaults().sizePt * m_zoom);
        return font;
    }
    case Qt::ImCursorPosition: return m_focus.position.offset;
    case Qt::ImAnchorPosition:
        return m_anchor.position.paragraph == m_focus.position.paragraph ? m_anchor.position.offset : m_focus.position.offset;
    case Qt::ImAbsolutePosition: return cursorPosition();
    case Qt::ImSurroundingText: return p ? p->text : QString();
    case Qt::ImCurrentSelection: return selectedText();
    case Qt::ImMaximumTextLength: return QVariant();
    case Qt::ImTextBeforeCursor: return p ? p->text.left(m_focus.position.offset) : QString();
    case Qt::ImTextAfterCursor: return p ? p->text.mid(m_focus.position.offset) : QString();
    default: return QQuickItem::inputMethodQuery(query);
    }
}

void RichTextArea::mousePressEvent(QMouseEvent *event)
{
    if (!m_document) return QQuickItem::mousePressEvent(event);
    forceActiveFocus(Qt::MouseFocusReason);
    if (!m_preedit.isEmpty()) commitPreedit();
    const Position hit = positionAt(event->position());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool again = now - m_lastPressTime < QGuiApplication::styleHints()->mouseDoubleClickInterval()
        && (event->position() - m_lastPress).manhattanLength() < 6;
    m_clickCount = again ? m_clickCount + 1 : 1;
    m_lastPressTime = now;
    m_lastPress = event->position();
    if ((event->modifiers() & Qt::ControlModifier) && !hrefAt(hit).isEmpty()) {
        emit linkActivated(hrefAt(hit));
        event->accept();
        return;
    }
    if (m_clickCount >= 3) selectParagraphAt(hit);
    else moveCaret(hit, event->modifiers() & Qt::ShiftModifier);
    m_selecting = m_clickCount < 3;
    event->accept();
}

void RichTextArea::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_document) return QQuickItem::mouseDoubleClickEvent(event);
    m_clickCount = 2;
    m_lastPressTime = QDateTime::currentMSecsSinceEpoch();
    selectWordAt(positionAt(event->position()));
    m_selecting = false;
    event->accept();
}

void RichTextArea::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_selecting || !m_document) return QQuickItem::mouseMoveEvent(event);
    m_dragPoint = event->position();
    moveCaret(positionAt(m_dragPoint), true);
    if (m_dragPoint.y() < 24 || m_dragPoint.y() > height() - 24) m_autoScroll.start(30, this);
    event->accept();
}

void RichTextArea::mouseReleaseEvent(QMouseEvent *event)
{
    m_selecting = false;
    m_autoScroll.stop();
    event->accept();
}

void RichTextArea::hoverMoveEvent(QHoverEvent *event)
{
    if (m_document && pageCount() > 0) {
        const bool link = (event->modifiers() & Qt::ControlModifier) && !hrefAt(positionAt(event->position())).isEmpty();
        setCursor(QCursor(link ? Qt::PointingHandCursor : Qt::IBeamCursor));
    }
    QQuickItem::hoverMoveEvent(event);
}

void RichTextArea::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        setZoom(m_zoom * (event->angleDelta().y() > 0 ? 1.1 : 1 / 1.1));
        event->accept();
        return;
    }
    const QPoint pixels = event->pixelDelta();
    const QPointF delta = pixels.isNull() ? QPointF(event->angleDelta()) / 8.0 * 3.0 : QPointF(pixels);
    const qreal oldY = m_contentY;
    const qreal oldX = m_contentX;
    setContentY(m_contentY - delta.y());
    setContentX(m_contentX - delta.x());
    event->setAccepted(!qFuzzyCompare(oldY, m_contentY) || !qFuzzyCompare(oldX, m_contentX));
}

QString RichTextArea::hrefAt(const Position &position) const
{
    const Paragraph *p = m_document ? m_document->paragraph(position.paragraph) : nullptr;
    if (!p || p->text.isEmpty()) return {};
    int start = 0;
    for (const Span &span : p->spans) {
        if (position.offset < start + span.length) return m_document->charFormat(span.format).href;
        start += span.length;
    }
    return {};
}

} // namespace QindaTK
