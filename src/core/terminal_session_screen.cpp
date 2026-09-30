// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_session.h"

#include <QVariantMap>
#include <algorithm>
#include <limits>

namespace QindaTK {

QString TerminalSession::cellText(const VTermScreenCell &cell)
{
    // libvterm uses UINT32_MAX in the trailing cell of a wide glyph.
    if (cell.width == 0 || cell.chars[0] == std::numeric_limits<uint32_t>::max())
        return {};
    QString text;
    for (uint32_t ch : cell.chars) {
        if (!ch || ch == std::numeric_limits<uint32_t>::max())
            break;
        const char32_t scalar = static_cast<char32_t>(ch);
        text += QString::fromUcs4(&scalar, 1);
    }
    return text.isEmpty() ? QStringLiteral(" ") : text;
}

QString TerminalSession::colorName(VTermColor color, bool foreground) const
{
    if (foreground ? VTERM_COLOR_IS_DEFAULT_FG(&color) : VTERM_COLOR_IS_DEFAULT_BG(&color))
        return {};
    vterm_screen_convert_color_to_rgb(m_vscreen, &color);
    return QStringLiteral("#%1%2%3")
        .arg(color.rgb.red, 2, 16, QLatin1Char('0'))
        .arg(color.rgb.green, 2, 16, QLatin1Char('0'))
        .arg(color.rgb.blue, 2, 16, QLatin1Char('0'));
}

int TerminalSession::pushScrollback(int columns, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    if (self->m_alternateScreen)
        return 1;
    if (self->m_maxScrollback == 0) {
        ++self->m_scrollbackOffset;
        return 1;
    }
    QList<VTermScreenCell> saved(cells, cells + columns);
    QString line;
    for (const auto &cell : saved)
        line += cellText(cell);
    while (line.endsWith(QLatin1Char(' '))) line.chop(1);
    self->m_history.push_back(std::move(saved));
    self->m_scrollback.append(line);
    while (self->m_scrollback.size() > self->m_maxScrollback) {
        self->m_scrollback.removeFirst();
        self->m_history.pop_front();
        ++self->m_scrollbackOffset;
    }
    return 1;
}

int TerminalSession::popScrollback(int columns, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    if (self->m_alternateScreen || self->m_history.empty())
        return 0;
    const auto saved = std::move(self->m_history.back());
    self->m_history.pop_back();
    self->m_scrollback.removeLast();
    VTermColor fg{}, bg{};
    vterm_state_get_default_colors(vterm_obtain_state(self->m_vterm), &fg, &bg);
    for (int column = 0; column < columns; ++column) {
        if (column < saved.size()) cells[column] = saved.at(column);
        else {
            cells[column] = {};
            cells[column].width = 1;
            cells[column].fg = fg;
            cells[column].bg = bg;
        }
    }
    return 1;
}

void TerminalSession::clearHistory()
{
    m_scrollbackOffset += m_scrollback.size();
    m_scrollback.clear();
    m_history.clear();
    emit screenChanged();
}

int TerminalSession::clearScrollback(void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    self->m_scrollbackOffset += self->m_scrollback.size();
    self->m_scrollback.clear();
    self->m_history.clear();
    return 1;
}

int TerminalSession::cursorMoved(VTermPos pos, VTermPos, int visible, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    self->m_cursorRow = pos.row;
    self->m_cursorColumn = pos.col;
    self->m_cursorVisible = visible;
    return 1;
}

int TerminalSession::termProperty(VTermProp prop, VTermValue *value, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE: self->m_cursorVisible = value->boolean; break;
    case VTERM_PROP_CURSORBLINK: self->m_cursorBlink = value->boolean; break;
    case VTERM_PROP_CURSORSHAPE: self->m_cursorShape = value->number; break;
    case VTERM_PROP_ALTSCREEN: self->m_alternateScreen = value->boolean; break;
    case VTERM_PROP_REVERSE: self->m_reverseScreen = value->boolean; break;
    case VTERM_PROP_MOUSE: self->m_mouseMode = value->number; break;
    case VTERM_PROP_TITLE:
        if (value->string.initial) self->m_titleBytes.clear();
        if (self->m_titleBytes.size() + value->string.len <= 8192)
            self->m_titleBytes.append(value->string.str, value->string.len);
        if (value->string.final) {
            self->m_title = QString::fromUtf8(self->m_titleBytes);
            self->m_title.removeIf([](QChar ch) { return ch.category() == QChar::Other_Control; });
        }
        break;
    default: break;
    }
    return 1;
}

int TerminalSession::ringBell(void *user)
{
    emit static_cast<TerminalSession *>(user)->bell();
    return 1;
}

QList<VTermScreenCell> TerminalSession::rowCells(int row) const
{
    const int history = m_alternateScreen ? 0 : int(m_history.size());
    if (row < 0 || row >= history + m_rows)
        return {};
    if (row < history)
        return m_history.at(size_t(row));
    QList<VTermScreenCell> cells;
    cells.reserve(m_columns);
    for (int column = 0; column < m_columns; ++column) {
        VTermScreenCell cell{};
        vterm_screen_get_cell(m_vscreen, {row - history, column}, &cell);
        cells.append(cell);
    }
    return cells;
}

QVariantMap TerminalSession::formatRow(const QList<VTermScreenCell> &cells, bool includeCells) const
{
    QString plain, spanText;
    QVariantList spans, positioned;
    QVariantMap style;
    int spanColumn = 0, spanWidth = 0;
    auto flushSpan = [&] {
        if (spanText.isEmpty()) return;
        auto span = style;
        span.insert(QStringLiteral("text"), spanText);
        span.insert(QStringLiteral("column"), spanColumn);
        span.insert(QStringLiteral("width"), spanWidth);
        spans.append(span);
        spanText.clear();
        spanWidth = 0;
    };
    for (int column = 0; column < cells.size(); ++column) {
        const auto &cell = cells.at(column);
        const QString glyph = cellText(cell);
        if (glyph.isEmpty()) continue;
        QVariantMap nextStyle{
            {QStringLiteral("foreground"), colorName(cell.fg, true)},
            {QStringLiteral("background"), colorName(cell.bg, false)},
            {QStringLiteral("bold"), bool(cell.attrs.bold)},
            {QStringLiteral("italic"), bool(cell.attrs.italic)},
            {QStringLiteral("underline"), bool(cell.attrs.underline)},
            {QStringLiteral("reverse"), bool(cell.attrs.reverse)},
            {QStringLiteral("conceal"), bool(cell.attrs.conceal)},
            {QStringLiteral("strike"), bool(cell.attrs.strike)},
        };
        if (style != nextStyle) {
            flushSpan();
            style = nextStyle;
            spanColumn = column;
        }
        spanText += glyph;
        spanWidth += qMax(1, int(cell.width));
        plain += glyph;
        if (includeCells) {
            nextStyle.insert(QStringLiteral("text"), glyph);
            nextStyle.insert(QStringLiteral("column"), column);
            nextStyle.insert(QStringLiteral("width"), qMax(1, int(cell.width)));
            positioned.append(nextStyle);
        }
    }
    flushSpan();
    QVariantMap result{{QStringLiteral("text"), plain}, {QStringLiteral("spans"), spans},
                       {QStringLiteral("columns"), cells.size()}};
    if (includeCells) result.insert(QStringLiteral("cells"), positioned);
    return result;
}

int TerminalSession::historyColumns() const
{
    int columns = 0;
    if (!m_alternateScreen)
        for (const auto &row : m_history) columns = qMax(columns, int(row.size()));
    return columns;
}

QVariantMap TerminalSession::rowData(int row) const
{
    return formatRow(rowCells(row), true);
}

void TerminalSession::rebuildScreen()
{
    if (!m_vscreen) return;
    QVariantList rows;
    rows.reserve(m_rows);
    const int history = m_alternateScreen ? 0 : int(m_history.size());
    for (int row = 0; row < m_rows; ++row)
        rows.append(formatRow(rowCells(history + row), false));
    m_screenRows = rows;
    emit screenChanged();
}

QString TerminalSession::copyText(int firstRow, int lastRow) const
{
    return copyTextRange(firstRow, 0, lastRow, -1);
}

QString TerminalSession::copyTextRange(int firstRow, int firstColumn, int lastRow, int lastColumn) const
{
    const int total = (m_alternateScreen ? 0 : int(m_history.size())) + m_rows;
    if (firstRow < 0 || lastRow < firstRow || lastRow >= total || firstColumn < 0)
        return {};
    QStringList selected;
    for (int row = firstRow; row <= lastRow; ++row) {
        const auto cells = rowCells(row);
        const int begin = row == firstRow ? firstColumn : 0;
        const int end = row == lastRow && lastColumn >= 0 ? lastColumn : int(cells.size());
        if (begin > cells.size() || end < 0 || end > cells.size() || end < begin)
            return {};
        QString line;
        for (int column = begin; column < end; ++column) {
            // Starting on a wide continuation includes its leading glyph once.
            if (column == begin && column > 0 && cellText(cells.at(column)).isEmpty())
                line += cellText(cells.at(column - 1));
            line += cellText(cells.at(column));
        }
        if (row != lastRow || lastColumn < 0)
            while (line.endsWith(QLatin1Char(' '))) line.chop(1);
        selected.append(line);
    }
    return selected.join(QLatin1Char('\n'));
}

} // namespace QindaTK
