// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_session.h"

#include <Qt>

namespace QindaTK {
namespace {
VTermModifier terminalModifiers(int modifiers)
{
    int result = VTERM_MOD_NONE;
    if (modifiers & Qt::ShiftModifier) result |= VTERM_MOD_SHIFT;
    if (modifiers & Qt::AltModifier) result |= VTERM_MOD_ALT;
    if (modifiers & Qt::ControlModifier) result |= VTERM_MOD_CTRL;
    return static_cast<VTermModifier>(result);
}

VTermKey terminalKey(int key, int modifiers)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return static_cast<VTermKey>(VTERM_KEY_FUNCTION(key - Qt::Key_F1 + 1));
    if (modifiers & Qt::KeypadModifier) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9)
            return static_cast<VTermKey>(VTERM_KEY_KP_0 + key - Qt::Key_0);
        switch (key) {
        case Qt::Key_Asterisk: return VTERM_KEY_KP_MULT;
        case Qt::Key_Plus: return VTERM_KEY_KP_PLUS;
        case Qt::Key_Comma: return VTERM_KEY_KP_COMMA;
        case Qt::Key_Minus: return VTERM_KEY_KP_MINUS;
        case Qt::Key_Period: return VTERM_KEY_KP_PERIOD;
        case Qt::Key_Slash: return VTERM_KEY_KP_DIVIDE;
        case Qt::Key_Equal: return VTERM_KEY_KP_EQUAL;
        case Qt::Key_Enter: return VTERM_KEY_KP_ENTER;
        }
    }
    switch (key) {
    case Qt::Key_Return: case Qt::Key_Enter: return VTERM_KEY_ENTER;
    case Qt::Key_Tab: case Qt::Key_Backtab: return VTERM_KEY_TAB;
    case Qt::Key_Backspace: return VTERM_KEY_BACKSPACE;
    case Qt::Key_Escape: return VTERM_KEY_ESCAPE;
    case Qt::Key_Up: return VTERM_KEY_UP;
    case Qt::Key_Down: return VTERM_KEY_DOWN;
    case Qt::Key_Left: return VTERM_KEY_LEFT;
    case Qt::Key_Right: return VTERM_KEY_RIGHT;
    case Qt::Key_Insert: return VTERM_KEY_INS;
    case Qt::Key_Delete: return VTERM_KEY_DEL;
    case Qt::Key_Home: return VTERM_KEY_HOME;
    case Qt::Key_End: return VTERM_KEY_END;
    case Qt::Key_PageUp: return VTERM_KEY_PAGEUP;
    case Qt::Key_PageDown: return VTERM_KEY_PAGEDOWN;
    default: return VTERM_KEY_NONE;
    }
}
}

bool TerminalSession::sendKey(int key, int modifiers, const QString &text)
{
    if (!running() || !m_vterm)
        return false;
    if (key == Qt::Key_Backtab)
        modifiers |= Qt::ShiftModifier;
    // Traditional terminal Return remains CR with Shift/Ctrl; many line
    // editors do not understand libvterm's extended CSI-u Return encoding.
    if (key == Qt::Key_Return || key == Qt::Key_Enter)
        modifiers &= ~(Qt::ShiftModifier | Qt::ControlModifier);
    const VTermKey special = terminalKey(key, modifiers);
    if (special != VTERM_KEY_NONE) {
        vterm_keyboard_key(m_vterm, special, terminalModifiers(modifiers));
    } else {
        QString characters = text;
        if ((modifiers & Qt::ControlModifier) && key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde)
            characters = QString(QChar(static_cast<ushort>(key)));
        if ((modifiers & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
            characters = characters.toLower();
            modifiers &= ~Qt::ShiftModifier;
        }
        if (characters.isEmpty())
            return false;
        for (const char32_t scalar : characters.toUcs4())
            vterm_keyboard_unichar(m_vterm, static_cast<uint32_t>(scalar), terminalModifiers(modifiers));
    }
    return flushInput();
}

bool TerminalSession::pasteText(const QString &text)
{
    if (!running() || !m_vterm)
        return false;
    QString safe;
    safe.reserve(text.size());
    // Pasted text must not inject terminal control keys or close its own
    // bracketed-paste frame. Tabs and line endings are ordinary pasted text.
    for (QChar ch : text) {
        if ((ch.unicode() >= 32 && ch.unicode() != 127) || ch == QLatin1Char('\t')
            || ch == QLatin1Char('\r') || ch == QLatin1Char('\n'))
            safe += ch;
    }
    safe.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    safe.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    const QByteArray bytes = safe.toUtf8();
    if (bytes.size() + 16 > 1024 * 1024 - m_pendingInput.size()) {
        setError(QStringLiteral("terminal input buffer is full"));
        return false;
    }
    vterm_keyboard_start_paste(m_vterm);
    const bool sent = queueInput(bytes);
    vterm_keyboard_end_paste(m_vterm);
    return flushInput() && sent;
}

bool TerminalSession::sendMouse(int row, int column, int button, bool pressed, int modifiers)
{
    if (!running() || !m_vterm || m_mouseMode == VTERM_PROP_MOUSE_NONE
        || row < 0 || row >= m_rows || column < 0 || column >= m_columns
        || button < 0 || button > 5)
        return false;
    vterm_mouse_move(m_vterm, row, column, terminalModifiers(modifiers));
    if (button)
        vterm_mouse_button(m_vterm, button, pressed, terminalModifiers(modifiers));
    return flushInput();
}

void TerminalSession::setFocused(bool focused)
{
    if (!m_vterm || !running())
        return;
    if (focused) vterm_state_focus_in(vterm_obtain_state(m_vterm));
    else vterm_state_focus_out(vterm_obtain_state(m_vterm));
    flushInput();
}

void TerminalSession::setColors(const QColor &foreground, const QColor &background,
                                const QVariantList &palette)
{
    if (!m_vterm || !foreground.isValid() || !background.isValid())
        return;
    auto convert = [](const QColor &color) {
        VTermColor value{};
        vterm_color_rgb(&value, color.red(), color.green(), color.blue());
        return value;
    };
    const VTermColor fg = convert(foreground), bg = convert(background);
    vterm_screen_set_default_colors(m_vscreen, &fg, &bg);
    for (int index = 0; index < m_defaultPalette.size(); ++index) {
        const QColor color = palette.value(index).value<QColor>();
        const VTermColor value = color.isValid() ? convert(color) : m_defaultPalette.at(index);
        vterm_state_set_palette_color(vterm_obtain_state(m_vterm), index, &value);
    }
    rebuildScreen();
}

void TerminalSession::outputToPty(const char *bytes, size_t length, void *user)
{
    auto *self = static_cast<TerminalSession *>(user);
    if (self->m_master < 0)
        return;
    if (length > size_t(1024 * 1024 - self->m_pendingInput.size())) {
        self->setError(QStringLiteral("terminal input buffer is full"));
        return;
    }
    self->m_pendingInput.append(bytes, qsizetype(length));
}

} // namespace QindaTK
