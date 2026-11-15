// SPDX-License-Identifier: LGPL-3.0-or-later
// Operating-system commands and capability strings answered by TerminalSession
// itself; see terminal_session_protocol_p.h for how they are lifted out.
#include "terminal_session_protocol_p.h"

#include <QColor>

namespace QindaTK {
namespace {
constexpr qsizetype maxClipboardBytes = 1024 * 1024;
constexpr qsizetype maxNotificationChars = 1024;
constexpr qsizetype maxPendingNotifications = 8;

QByteArray terminator(const TerminalProtocol::Sequence &sequence)
{
    return sequence.bell ? QByteArrayLiteral("\x07") : QByteArrayLiteral("\x1b\\");
}

// Text from the child that is shown outside the grid: no controls, bounded.
QString displayText(QByteArrayView bytes)
{
    QString text = QString::fromUtf8(bytes);
    text.removeIf([](QChar ch) {
        const ushort code = ch.unicode();
        return code < 0x20 || (code >= 0x7f && code <= 0x9f) || (code >= 0x202a && code <= 0x202e)
            || (code >= 0x2066 && code <= 0x2069);
    });
    return text.left(maxNotificationChars);
}

QByteArray rgbSpec(const QColor &color)
{
    const auto channel = [](int value) {
        return QByteArray::number(value * 257, 16).rightJustified(4, '0');
    };
    return QByteArrayLiteral("rgb:") + channel(color.red()) + '/' + channel(color.green()) + '/'
        + channel(color.blue());
}

QColor parseColor(QByteArrayView spec)
{
    if (spec.startsWith("rgb:")) {
        const auto parts = spec.sliced(4).toByteArray().split('/');
        if (parts.size() != 3) return {};
        int channels[3];
        for (int index = 0; index < 3; ++index) {
            bool ok = false;
            const int digits = int(parts.at(index).size());
            const int value = parts.at(index).toInt(&ok, 16);
            if (!ok || digits < 1 || digits > 4) return {};
            channels[index] = value * 255 / ((1 << (4 * digits)) - 1);
        }
        return QColor(channels[0], channels[1], channels[2]);
    }
    return QColor::fromString(QLatin1StringView(spec));
}
} // namespace

bool TerminalSession::Protocol::Handler::wantsOsc(int command) const
{
    switch (command) {
    case 4: case 9: case 10: case 11: case 12: case 52: case 99: case 104: case 110: case 111:
    case 112: case 133: case 777: case 7501:
        return true;
    default:
        return false;
    }
}

void TerminalSession::Protocol::handleOsc(const Sequence &sequence)
{
    switch (sequence.command) {
    case 4: paletteQuery(sequence); break;
    case 10: case 11: case 12: colorQuery(sequence); break;
    case 112:
        if (!cursorColor.isEmpty()) {
            cursorColor.clear();
            m_dirty = true;
        }
        break;
    case 52: clipboard(sequence); break;
    case 9: growlNotification(sequence.payload); break;
    case 777: {
        const auto parts = sequence.payload.toByteArray().split(';');
        if (parts.size() >= 2 && parts.at(0) == "notify")
            notify(displayText(parts.at(1)), displayText(parts.mid(2).join(';')));
        break;
    }
    case 99: kittyNotification(sequence); break;
    case 133:
        // A new shell prompt: whatever was working or blocked has ended.
        if (sequence.payload.startsWith('A') && status.dropTransient(false))
            emit q->programStatusChanged();
        break;
    case 7501: programStatus(sequence); break;
    default: break; // colour resets: the profile owns the palette
    }
}

QByteArray TerminalSession::Protocol::colorSpec(int index) const
{
    VTermState *state = vterm_obtain_state(q->m_vterm);
    VTermColor color {}, other {};
    if (index == -1) vterm_state_get_default_colors(state, &color, &other);
    else if (index == -2) vterm_state_get_default_colors(state, &other, &color);
    else vterm_state_get_palette_color(state, index, &color);
    vterm_screen_convert_color_to_rgb(q->m_vscreen, &color);
    return rgbSpec(QColor(color.rgb.red, color.rgb.green, color.rgb.blue));
}

bool TerminalSession::Protocol::darkBackground() const
{
    if (!q->m_vterm) return true;
    VTermColor foreground {}, background {};
    vterm_state_get_default_colors(vterm_obtain_state(q->m_vterm), &foreground, &background);
    vterm_screen_convert_color_to_rgb(q->m_vscreen, &background);
    return 299 * background.rgb.red + 587 * background.rgb.green + 114 * background.rgb.blue < 128000;
}

void TerminalSession::Protocol::colorsChanged()
{
    const bool dark = darkBackground();
    if (dark == m_lastDark)
        return;
    m_lastDark = dark;
    // Mode 2031: tell the program the scheme flipped; it re-reads OSC 10/11.
    if (m_themeNotifications && q->running()) {
        reply(dark ? QByteArrayLiteral("\x1b[?997;1n") : QByteArrayLiteral("\x1b[?997;2n"));
        q->flushInput();
    }
}

void TerminalSession::Protocol::colorQuery(const Sequence &sequence)
{
    // "OSC 10 ; ? ; ?" asks for 10 and 11: each further value is the next code.
    int code = sequence.command;
    for (const QByteArray &spec : sequence.payload.toByteArray().split(';')) {
        if (code > 12) break;
        if (spec == "?") {
            QByteArray color = code == 10 ? colorSpec(-1) : code == 11 ? colorSpec(-2) : QByteArray();
            if (code == 12) {
                const QColor cursor(cursorColor);
                color = cursor.isValid() ? rgbSpec(cursor) : colorSpec(-1);
            }
            reply(QByteArrayLiteral("\x1b]") + QByteArray::number(code) + ';' + color + terminator(sequence));
        } else if (code == 12) {
            const QColor color = parseColor(spec);
            if (color.isValid() && cursorColor != color.name()) {
                cursorColor = color.name();
                m_dirty = true;
            }
        }
        ++code;
    }
}

void TerminalSession::Protocol::paletteQuery(const Sequence &sequence)
{
    const auto parts = sequence.payload.toByteArray().split(';');
    for (qsizetype index = 0; index + 1 < parts.size() && index < 64; index += 2) {
        bool ok = false;
        const int entry = parts.at(index).toInt(&ok);
        if (!ok || entry < 0 || entry > 255 || parts.at(index + 1) != "?") continue;
        reply(QByteArrayLiteral("\x1b]4;") + QByteArray::number(entry) + ';' + colorSpec(entry)
              + terminator(sequence));
    }
}

// AGENT-CONTRACT (OSC 52): a program may place text on the clipboard; it can
// never read it. A read request is answered with an empty selection so the
// program stops waiting. The host decides whether to honour a write.
void TerminalSession::Protocol::clipboard(const Sequence &sequence)
{
    const qsizetype separator = sequence.payload.indexOf(';');
    if (separator < 0) return;
    const QByteArrayView targets = sequence.payload.first(separator);
    const QByteArrayView data = sequence.payload.sliced(separator + 1);
    if (data == "?") {
        reply(QByteArrayLiteral("\x1b]52;") + targets + ';' + terminator(sequence));
        return;
    }
    const auto decoded = QByteArray::fromBase64Encoding(data.toByteArray(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.isEmpty() || decoded.decoded.size() > maxClipboardBytes) return;
    const QString text = QString::fromUtf8(decoded.decoded);
    const bool primary = targets.contains('p');
    if (targets.isEmpty() || targets.contains('c') || targets.contains('s') || !primary)
        emit q->clipboardWriteRequested(text, false);
    if (primary)
        emit q->clipboardWriteRequested(text, true);
}

void TerminalSession::Protocol::notify(QString title, QString body)
{
    if (title.isEmpty() && body.isEmpty()) return;
    emit q->notificationRequested(title, body);
}

void TerminalSession::Protocol::growlNotification(QByteArrayView payload)
{
    // OSC 9 is a notification, except for ConEmu's numbered subcommands;
    // 9;4 is the progress report several build tools and agents emit.
    const auto parts = payload.toByteArray().split(';');
    bool numbered = false;
    const int subcommand = parts.at(0).toInt(&numbered);
    if (!numbered || parts.size() < 2 || subcommand < 1 || subcommand > 12) {
        notify({}, displayText(payload));
        return;
    }
    // A program that speaks OSC 7501 owns the status; a mapped progress
    // report would erase the kind and message of its record.
    if (subcommand != 4 || m_statusReported) return;
    const int progressState = parts.at(1).toInt();
    ProgramStatus::Report report;
    report.type = progressState == 0 ? ProgramStatus::Report::Clear : ProgramStatus::Report::Status;
    report.state = progressState == 2 ? QByteArrayLiteral("error") : QByteArrayLiteral("working");
    if ((progressState == 1 || progressState == 4) && parts.size() > 2)
        report.progress = qBound(0, parts.at(2).toInt(), 100);
    if (status.apply(report)) emit q->programStatusChanged();
}

void TerminalSession::Protocol::kittyNotification(const Sequence &sequence)
{
    const qsizetype separator = sequence.payload.indexOf(';');
    const QByteArrayView metadata = separator < 0 ? sequence.payload : sequence.payload.first(separator);
    QByteArray payload = separator < 0 ? QByteArray() : sequence.payload.sliced(separator + 1).toByteArray();
    QByteArray id, type = QByteArrayLiteral("title"), occasion = QByteArrayLiteral("always");
    bool done = true, encoded = false;
    for (const QByteArray &pair : metadata.toByteArray().split(':')) {
        const qsizetype equals = pair.indexOf('=');
        if (equals < 1) continue;
        const QByteArray key = pair.first(equals).trimmed(), value = pair.sliced(equals + 1).trimmed();
        if (key == "i") id = value.left(64);
        else if (key == "p") type = value;
        else if (key == "d") done = value != "0";
        else if (key == "e") encoded = value == "1";
        else if (key == "o") occasion = value;
    }
    if (type == "?") {
        reply(QByteArrayLiteral("\x1b]99;i=") + id + QByteArrayLiteral(":p=?;o=always,unfocused,invisible"
              ":p=title,body") + terminator(sequence));
        return;
    }
    if (type != "title" && type != "body") return;
    if (encoded) payload = QByteArray::fromBase64(payload);
    if (!m_notifications.contains(id) && m_notifications.size() >= maxPendingNotifications)
        m_notifications.clear();
    PendingNotification &pending = m_notifications[id];
    QString &target = type == "title" ? pending.title : pending.body;
    target = (target + displayText(payload)).left(maxNotificationChars);
    if (!done) return;
    const PendingNotification complete = m_notifications.take(id);
    // "unfocused" and "invisible" both mean: not while the user is looking.
    if (occasion == "always" || !focused)
        notify(complete.title, complete.body);
}

void TerminalSession::Protocol::programStatus(const Sequence &sequence)
{
    const ProgramStatus::Report report = ProgramStatus::parse(sequence.payload, sequence.raw.size());
    if (report.type == ProgramStatus::Report::Query) {
        reply(QByteArrayLiteral("\x1b]7501;") + ProgramStatus::queryReply() + terminator(sequence));
        return;
    }
    if (report.type == ProgramStatus::Report::Discard) return;
    m_statusReported = true;
    if (status.apply(report)) emit q->programStatusChanged();
}

// XTGETTCAP: programs ask for single terminfo capabilities by hex name.
void TerminalSession::Protocol::handleTermcap(const Sequence &sequence)
{
    for (const QByteArray &hexName : sequence.payload.toByteArray().split(';')) {
        const QByteArray name = QByteArray::fromHex(hexName);
        QByteArray value;
        bool known = true;
        if (name == "TN" || name == "name") value = QByteArrayLiteral("xterm-256color");
        else if (name == "Co" || name == "colors") value = QByteArrayLiteral("256");
        else if (name == "RGB") value = QByteArrayLiteral("8/8/8");
        else if (name == "Ms") value = QByteArrayLiteral("\x1b]52;%p1%s;%p2%s\x07");
        else known = false;
        reply(known ? QByteArrayLiteral("\x1bP1+r") + hexName + '=' + value.toHex() + QByteArrayLiteral("\x1b\\")
                    : QByteArrayLiteral("\x1bP0+r") + hexName + QByteArrayLiteral("\x1b\\"));
    }
}

} // namespace QindaTK
