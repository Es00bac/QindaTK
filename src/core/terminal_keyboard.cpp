// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_keyboard_p.h"

#include <QChar>
#include <QList>
#include <Qt>

namespace QindaTK::TerminalKeyboard {
namespace {
struct Functional {
    int number = 0;
    char suffix = 0;
};

// The key numbers and final bytes of the kitty protocol's functional key table.
Functional functionalKey(int key, bool keypad)
{
    if (keypad) {
        if (key >= Qt::Key_0 && key <= Qt::Key_9) return {57399 + key - Qt::Key_0, 'u'};
        switch (key) {
        case Qt::Key_Period: return {57409, 'u'};
        case Qt::Key_Slash: return {57410, 'u'};
        case Qt::Key_Asterisk: return {57411, 'u'};
        case Qt::Key_Minus: return {57412, 'u'};
        case Qt::Key_Plus: return {57413, 'u'};
        case Qt::Key_Enter: case Qt::Key_Return: return {57414, 'u'};
        case Qt::Key_Equal: return {57415, 'u'};
        case Qt::Key_Comma: return {57416, 'u'};
        case Qt::Key_Left: return {57417, 'u'};
        case Qt::Key_Right: return {57418, 'u'};
        case Qt::Key_Up: return {57419, 'u'};
        case Qt::Key_Down: return {57420, 'u'};
        case Qt::Key_PageUp: return {57421, 'u'};
        case Qt::Key_PageDown: return {57422, 'u'};
        case Qt::Key_Home: return {57423, 'u'};
        case Qt::Key_End: return {57424, 'u'};
        case Qt::Key_Insert: return {57425, 'u'};
        case Qt::Key_Delete: return {57426, 'u'};
        case Qt::Key_Clear: return {57427, 'u'};
        }
    }
    if (key >= Qt::Key_F13 && key <= Qt::Key_F35) return {57376 + key - Qt::Key_F13, 'u'};
    switch (key) {
    case Qt::Key_Escape: return {27, 'u'};
    case Qt::Key_Return: case Qt::Key_Enter: return {13, 'u'};
    case Qt::Key_Tab: case Qt::Key_Backtab: return {9, 'u'};
    case Qt::Key_Backspace: return {127, 'u'};
    case Qt::Key_Insert: return {2, '~'};
    case Qt::Key_Delete: return {3, '~'};
    case Qt::Key_Left: return {1, 'D'};
    case Qt::Key_Right: return {1, 'C'};
    case Qt::Key_Up: return {1, 'A'};
    case Qt::Key_Down: return {1, 'B'};
    case Qt::Key_PageUp: return {5, '~'};
    case Qt::Key_PageDown: return {6, '~'};
    case Qt::Key_Home: return {1, 'H'};
    case Qt::Key_End: return {1, 'F'};
    case Qt::Key_Clear: return {1, 'E'};
    case Qt::Key_F1: return {1, 'P'};
    case Qt::Key_F2: return {1, 'Q'};
    case Qt::Key_F3: return {13, '~'};
    case Qt::Key_F4: return {1, 'S'};
    case Qt::Key_F5: return {15, '~'};
    case Qt::Key_F6: return {17, '~'};
    case Qt::Key_F7: return {18, '~'};
    case Qt::Key_F8: return {19, '~'};
    case Qt::Key_F9: return {20, '~'};
    case Qt::Key_F10: return {21, '~'};
    case Qt::Key_F11: return {23, '~'};
    case Qt::Key_F12: return {24, '~'};
    case Qt::Key_CapsLock: return {57358, 'u'};
    case Qt::Key_ScrollLock: return {57359, 'u'};
    case Qt::Key_NumLock: return {57360, 'u'};
    case Qt::Key_Print: return {57361, 'u'};
    case Qt::Key_Pause: return {57362, 'u'};
    case Qt::Key_Menu: return {57363, 'u'};
    // Qt does not say which side a modifier key is on; report the left one.
    case Qt::Key_Shift: return {57441, 'u'};
    case Qt::Key_Control: return {57442, 'u'};
    case Qt::Key_Alt: return {57443, 'u'};
    case Qt::Key_Meta: case Qt::Key_Super_L: return {57444, 'u'};
    case Qt::Key_Super_R: return {57450, 'u'};
    case Qt::Key_AltGr: return {57453, 'u'};
    default: return {};
    }
}

bool isModifierKey(int number) { return number >= 57441 && number <= 57454; }

bool isTextKey(int key)
{
    // Qt::Key values below Key_Escape are the Unicode code point of the key.
    return key >= 0x20 && key < Qt::Key_Escape && key != 0x7f;
}

int modifierValue(int modifiers)
{
    return 1 + ((modifiers & Qt::ShiftModifier) ? 1 : 0) + ((modifiers & Qt::AltModifier) ? 2 : 0)
        + ((modifiers & Qt::ControlModifier) ? 4 : 0) + ((modifiers & Qt::MetaModifier) ? 8 : 0);
}

char32_t lowered(int key)
{
    return QChar::toLower(char32_t(key));
}

QByteArray utf8(const QString &text) { return text.toUtf8(); }
} // namespace

Encoding encodeKitty(int flags, int key, int modifiers, const QString &text, int eventType)
{
    flags &= AllFlags;
    // Without either of these the program asked for extras on top of the
    // legacy encoding, which has no way to express a release.
    if (!(flags & (Disambiguate | ReportAllKeys)))
        return {eventType == Release, {}};
    const bool reportEvents = flags & ReportEventTypes;
    const bool allKeys = flags & ReportAllKeys;
    if (eventType == Release && !reportEvents)
        return {true, {}};
    // AltGr (and other layout shifts) produce text; Qt reports them as
    // Ctrl+Alt on some platforms, which must not turn into a shortcut.
    if (modifiers & Qt::GroupSwitchModifier)
        modifiers &= ~(Qt::ControlModifier | Qt::AltModifier);
    if (key == Qt::Key_Backtab)
        modifiers |= Qt::ShiftModifier;
    const int mods = modifierValue(modifiers);
    const bool keypad = modifiers & Qt::KeypadModifier;
    QByteArray event;
    if (reportEvents && eventType != Press)
        event = eventType == Repeat ? QByteArrayLiteral(":2") : QByteArrayLiteral(":3");

    Functional functional = functionalKey(key, keypad);
    const bool keypadText = keypad && functional.number >= 57399 && functional.number <= 57416
        && functional.number != 57414;
    if (keypadText && !allKeys)
        functional = {}; // keypad digits and operators type text like any other key

    if (functional.number) {
        if (isModifierKey(functional.number) && !allKeys)
            return {true, {}};
        // F3 is CSI 13 ~: the number alone does not identify Enter.
        const bool legacyByte = functional.suffix == 'u'
            && (functional.number == 13 || functional.number == 9 || functional.number == 127);
        if (legacyByte && !allKeys) {
            if (eventType == Release) return {true, {}};
            if (mods == 1) {
                return {true, functional.number == 13 ? QByteArrayLiteral("\r")
                    : functional.number == 9 ? QByteArrayLiteral("\t") : QByteArrayLiteral("\x7f")};
            }
        }
        QByteArray bytes = QByteArrayLiteral("\x1b[");
        const bool letter = functional.suffix != 'u' && functional.suffix != '~';
        const bool bare = mods == 1 && event.isEmpty();
        if (!(letter && bare && functional.number == 1))
            bytes += QByteArray::number(functional.number);
        if (!bare) {
            bytes += ';';
            bytes += QByteArray::number(mods);
            bytes += event;
        }
        if (allKeys && (flags & ReportAssociatedText) && keypadText && eventType != Release
            && !text.isEmpty()) {
            bytes += bare ? QByteArrayLiteral(";;") : QByteArrayLiteral(";");
            bytes += QByteArray::number(text.toUcs4().value(0));
        }
        bytes += functional.suffix;
        return {true, bytes};
    }

    if (!isTextKey(key))
        return {};
    const bool shortcut = modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (!allKeys && !shortcut) {
        // Plain and shifted keys type their text; a release has nothing to say.
        if (eventType == Release) return {true, {}};
        return text.isEmpty() ? Encoding{} : Encoding{true, utf8(text)};
    }
    const char32_t base = lowered(key);
    QByteArray bytes = QByteArrayLiteral("\x1b[") + QByteArray::number(uint(base));
    if ((flags & ReportAlternateKeys) && (modifiers & Qt::ShiftModifier)) {
        const char32_t shifted = QChar::toUpper(base);
        if (shifted != base) {
            bytes += ':';
            bytes += QByteArray::number(uint(shifted));
        }
    }
    QByteArray associated;
    if (allKeys && (flags & ReportAssociatedText) && eventType != Release && !shortcut) {
        for (const char32_t scalar : text.toUcs4()) {
            if (scalar < 0x20 || scalar == 0x7f) continue;
            if (!associated.isEmpty()) associated += ':';
            associated += QByteArray::number(uint(scalar));
        }
    }
    if (mods != 1 || !event.isEmpty() || !associated.isEmpty()) {
        bytes += ';';
        if (mods != 1 || !event.isEmpty()) {
            bytes += QByteArray::number(mods);
            bytes += event;
        }
    }
    if (!associated.isEmpty()) {
        bytes += ';';
        bytes += associated;
    }
    bytes += 'u';
    return {true, bytes};
}

Encoding encodeModifyOtherKeys(int level, int key, int modifiers, const QString &text)
{
    Q_UNUSED(text);
    if (level < 1 || (modifiers & Qt::GroupSwitchModifier))
        return {};
    const bool shift = modifiers & Qt::ShiftModifier, control = modifiers & Qt::ControlModifier;
    const bool alt = modifiers & Qt::AltModifier;
    const int mods = modifierValue(modifiers & ~Qt::MetaModifier);
    const auto other = [mods](uint code) {
        return Encoding{true, QByteArrayLiteral("\x1b[27;") + QByteArray::number(mods) + ';'
                                  + QByteArray::number(code) + '~'};
    };
    switch (key) {
    case Qt::Key_Return: case Qt::Key_Enter:
        return shift || control ? other(13) : Encoding{};
    case Qt::Key_Tab:
        return control ? other(9) : Encoding{};
    case Qt::Key_Backtab:
        // Shift+Tab keeps CSI Z; only the Ctrl form has no traditional bytes.
        return control ? Encoding{true, QByteArrayLiteral("\x1b[27;")
            + QByteArray::number(modifierValue((modifiers | Qt::ShiftModifier) & ~Qt::MetaModifier))
            + QByteArrayLiteral(";9~")} : Encoding{};
    case Qt::Key_Escape:
        return level >= 2 && mods != 1 ? other(27) : Encoding{};
    case Qt::Key_Space:
        if (control && !shift) return {};
        return (control && shift) || (level >= 2 && mods != 1) ? other(32) : Encoding{};
    default: break;
    }
    if (!isTextKey(key) || (modifiers & Qt::KeypadModifier) || !(control || alt))
        return {};
    const bool letter = key >= Qt::Key_A && key <= Qt::Key_Z;
    const uint code = letter && !shift ? uint(lowered(key)) : uint(key);
    if (level >= 2)
        return other(code);
    // Level 1 leaves every combination that already has well-known bytes.
    if (!control)
        return {};
    static const QByteArray legacyControls = QByteArrayLiteral("@[\\]^_?2345678-/");
    if (letter)
        return shift ? other(code) : Encoding{};
    return legacyControls.contains(char(key)) ? Encoding{} : other(code);
}

} // namespace QindaTK::TerminalKeyboard
