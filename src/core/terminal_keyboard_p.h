// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

namespace QindaTK::TerminalKeyboard {

// Progressive-enhancement flags of the kitty keyboard protocol.
enum Flag {
    Disambiguate = 1,
    ReportEventTypes = 2,
    ReportAlternateKeys = 4,
    ReportAllKeys = 8,
    ReportAssociatedText = 16,
    AllFlags = 31
};
enum EventType { Press = 1, Repeat = 2, Release = 3 };

struct Encoding {
    // false: this encoder has no opinion and the legacy encoding applies.
    // true with empty bytes: the event is deliberately not reported.
    bool handled = false;
    QByteArray bytes;
};

// AGENT-CONTRACT (kitty keyboard protocol): programs opt in with CSI > flags u
// and the terminal then sends CSI number ; modifiers u forms. `key` and
// `modifiers` are Qt::Key and Qt::KeyboardModifiers values, `text` is what the
// key would type. Enter, Tab and Backspace keep their legacy bytes unless
// ReportAllKeys is set, so a shell stays usable after a program crashes with
// the mode still on.
Encoding encodeKitty(int flags, int key, int modifiers, const QString &text, int eventType);

// xterm modifyOtherKeys (CSI > 4 ; level m), for programs that ask for it
// instead of the kitty protocol. Only combinations without a traditional
// encoding are changed at level 1; level 2 also covers Ctrl and Alt letters.
Encoding encodeModifyOtherKeys(int level, int key, int modifiers, const QString &text);

} // namespace QindaTK::TerminalKeyboard
