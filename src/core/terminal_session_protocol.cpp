// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_session_protocol_p.h"

#include "terminal_keyboard_p.h"

#include <QCoreApplication>

namespace QindaTK {
namespace {
constexpr int maxKeyboardStack = 16;

QList<QByteArrayView> split(QByteArrayView text, char separator)
{
    QList<QByteArrayView> parts;
    for (qsizetype start = 0; start <= text.size();) {
        qsizetype end = text.indexOf(separator, start);
        if (end < 0) end = text.size();
        parts.append(text.sliced(start, end - start));
        start = end + 1;
    }
    return parts;
}

// The leading number of one parameter; -1 when it is omitted or malformed.
int number(QByteArrayView parameter)
{
    const qsizetype colon = parameter.indexOf(':');
    if (colon >= 0) parameter = parameter.first(colon);
    if (parameter.isEmpty() || parameter.size() > 6) return -1;
    bool ok = false;
    const int value = parameter.toInt(&ok);
    return ok ? value : -1;
}

QByteArray identity()
{
    QString name = QCoreApplication::applicationName();
    if (name.isEmpty()) name = QStringLiteral("QindaTK");
    const QString version = QCoreApplication::applicationVersion();
    QByteArray text = (version.isEmpty() ? name : name + QLatin1Char(' ') + version).toLatin1();
    text.removeIf([](char byte) { return byte < 0x20 || byte > 0x7e; });
    return text.left(64);
}
} // namespace

TerminalSession::Protocol::Protocol(TerminalSession *session) : q(session)
{
    m_synchronizedTimer.setSingleShot(true);
    m_synchronizedTimer.setInterval(synchronizedTimeoutMs);
    QObject::connect(&m_synchronizedTimer, &QTimer::timeout, q, [this] {
        endSynchronizedUpdate();
        q->flushInput();
    });
    m_presentTimer.setSingleShot(true);
    QObject::connect(&m_presentTimer, &QTimer::timeout, q, [this] { presentNow(); });
}

void TerminalSession::Protocol::input(QByteArrayView bytes)
{
    m_gateFilter.feed(bytes, m_gate);
}

void TerminalSession::Protocol::Gate::sequence(const Sequence &sequence)
{
    Protocol *p = m_owner;
    const bool modeChange = sequence.kind == Sequence::Csi && sequence.leader == '?'
        && sequence.intermediates.isEmpty() && (sequence.final == 'h' || sequence.final == 'l');
    if (!modeChange || !sequence.parameters.contains("2026")) {
        p->gated(sequence.raw);
        return;
    }
    QByteArray others;
    bool bracket = false;
    for (const QByteArrayView parameter : split(sequence.parameters, ';')) {
        if (number(parameter) == 2026) {
            bracket = true;
            continue;
        }
        if (!others.isEmpty()) others += ';';
        others += parameter;
    }
    if (!bracket) {
        p->gated(sequence.raw);
        return;
    }
    const bool begin = sequence.final == 'h';
    // Modes that share the sequence belong to the side of the bracket they
    // were sent on: before the frame for a begin, inside it for an end.
    const QByteArray rest = others.isEmpty() ? QByteArray()
        : QByteArrayLiteral("\x1b[?") + others + sequence.final;
    if (begin) {
        if (!rest.isEmpty()) p->gated(rest);
        if (!p->m_synchronized) {
            p->m_synchronized = true;
            p->m_synchronizedTimer.start();
        }
    } else {
        if (!rest.isEmpty()) p->gated(rest);
        p->endSynchronizedUpdate();
    }
}

void TerminalSession::Protocol::gated(QByteArrayView bytes)
{
    if (!m_synchronized) {
        m_filter.feed(bytes, m_handler);
        return;
    }
    m_deferred.append(bytes);
    if (m_deferred.size() > synchronizedLimitBytes)
        endSynchronizedUpdate();
}

void TerminalSession::Protocol::endSynchronizedUpdate()
{
    if (!m_synchronized)
        return;
    m_synchronizedTimer.stop();
    // AGENT-GUARD: the frame is applied while the mode still reads as set, so
    // a mode request inside the frame is answered truthfully; gated() must not
    // run in between, or bytes would be appended to a frame already taken.
    const QByteArray frame = std::move(m_deferred);
    m_deferred.clear();
    m_filter.feed(frame, m_handler);
    m_synchronized = false;
    schedulePresent();
}

void TerminalSession::Protocol::write(QByteArrayView bytes)
{
    if (bytes.isEmpty() || !q->m_vterm)
        return;
    const bool alternate = q->m_alternateScreen;
    vterm_input_write(q->m_vterm, bytes.data(), size_t(bytes.size()));
    m_dirty = true;
    if (alternate != q->m_alternateScreen) {
        // Each screen has its own keyboard mode stack; a full-screen program
        // that dies without popping leaves the shell's modes untouched.
        if (q->m_alternateScreen) m_alternateKeyboard = {};
        emit q->modesChanged();
    }
}

void TerminalSession::Protocol::reply(const QByteArray &bytes)
{
    if (q->m_master < 0 || bytes.size() > 1024 * 1024 - q->m_pendingInput.size())
        return;
    q->m_pendingInput.append(bytes);
}

void TerminalSession::Protocol::schedulePresent()
{
    if (!m_dirty)
        return;
    if (!m_dirtySince.isValid())
        m_dirtySince.start();
    const bool overdue = m_dirtySince.elapsed() >= presentMaxDelayMs - presentQuietMs;
    if (overdue && m_presentTimer.isActive())
        return;
    m_presentTimer.start(overdue ? 0 : presentQuietMs);
}

void TerminalSession::Protocol::presentNow()
{
    m_presentTimer.stop();
    m_dirtySince.invalidate();
    if (!m_dirty)
        return;
    m_dirty = false;
    vterm_screen_flush_damage(q->m_vscreen);
    q->rebuildScreen();
}

void TerminalSession::Protocol::resetState()
{
    const bool modes = bracketedPaste || keyboardFlags() || m_modifyOtherKeys || !alternateScroll;
    alternateScroll = true;
    m_mainKeyboard = {};
    m_alternateKeyboard = {};
    m_modifyOtherKeys = 0;
    m_themeNotifications = false;
    m_statusReported = false;
    bracketedPaste = false;
    cursorColor.clear();
    m_notifications.clear();
    if (status.clear()) emit q->programStatusChanged();
    if (modes) emit q->modesChanged();
}

void TerminalSession::Protocol::processStarted()
{
    m_gateFilter.reset();
    m_filter.reset();
    m_deferred.clear();
    m_synchronized = false;
    m_synchronizedTimer.stop();
    m_dirty = false;
    m_presentTimer.stop();
    m_dirtySince.invalidate();
    m_lastDark = darkBackground();
    resetState();
}

void TerminalSession::Protocol::processEnded()
{
    endSynchronizedUpdate();
    presentNow();
    // Working and blocked describe a live program; done and error outlive it.
    if (status.dropTransient(true)) emit q->programStatusChanged();
}

TerminalSession::Protocol::KeyboardStack &TerminalSession::Protocol::keyboard()
{
    return q->m_alternateScreen ? m_alternateKeyboard : m_mainKeyboard;
}

int TerminalSession::Protocol::keyboardFlags() const
{
    return (q->m_alternateScreen ? m_alternateKeyboard : m_mainKeyboard).current;
}

void TerminalSession::Protocol::handle(const Sequence &sequence)
{
    switch (sequence.kind) {
    case Sequence::Reset:
        write(sequence.raw);
        resetState();
        return;
    case Sequence::Osc: handleOsc(sequence); return;
    case Sequence::Dcs: handleTermcap(sequence); return;
    case Sequence::Csi:
        if (!handleCsi(sequence)) write(sequence.raw);
        return;
    }
}

bool TerminalSession::Protocol::handleCsi(const Sequence &sequence)
{
    const bool plain = sequence.intermediates.isEmpty();
    const auto parameters = split(sequence.parameters, ';');
    switch (sequence.final) {
    case 'm':
        if (!plain) return false;
        if (!sequence.leader) return rewriteGraphics(sequence);
        if (sequence.leader == '>') {
            // XTMODKEYS: only resource 4 (modifyOtherKeys) changes key encoding.
            if (sequence.parameters.isEmpty()) m_modifyOtherKeys = 0;
            else if (number(parameters.at(0)) == 4)
                m_modifyOtherKeys = qBound(0, parameters.size() > 1 ? number(parameters.at(1)) : 0, 2);
            emit q->modesChanged();
            return true;
        }
        if (sequence.leader == '?' && number(parameters.at(0)) == 4) {
            reply(QByteArrayLiteral("\x1b[>4;") + QByteArray::number(m_modifyOtherKeys) + 'm');
            return true;
        }
        return false;
    case 'h': case 'l':
        return sequence.leader == '?' && plain && privateModes(sequence, sequence.final == 'h');
    case 'p': {
        if (sequence.leader != '?' || sequence.intermediates != "$") return false;
        // DECRQM for the modes libvterm does not know: 1 set, 2 reset.
        const int mode = number(parameters.at(0));
        if (mode != 2026 && mode != 2031) return false;
        const bool set = mode == 2026 ? m_synchronized : m_themeNotifications;
        reply(QByteArrayLiteral("\x1b[?") + QByteArray::number(mode) + (set ? ";1$y" : ";2$y"));
        return true;
    }
    case 'u':
        // Without a private marker this is "restore cursor", which libvterm owns.
        if (!plain || !sequence.leader) return false;
        keyboardProtocol(sequence);
        return true;
    case 'q':
        if (sequence.leader != '>' || !plain) return false;
        reply(QByteArrayLiteral("\x1bP>|") + identity() + QByteArrayLiteral("\x1b\\"));
        return true;
    case 'n':
        if (sequence.leader != '?' || !plain || number(parameters.at(0)) != 996) return false;
        reply(darkBackground() ? QByteArrayLiteral("\x1b[?997;1n") : QByteArrayLiteral("\x1b[?997;2n"));
        return true;
    case 't':
        return !sequence.leader && plain && reportWindow(sequence);
    default:
        return false;
    }
}

// AGENT-NOTE (D-363): libvterm 0.3 has no faint attribute and reads the
// arguments of an underline colour as separate attributes (58;5;1 would turn
// on blink and bold). SGR 2 is therefore carried in the cell's unused "font"
// attribute, where formatRow() reads it back as `dim`, and what libvterm
// cannot represent is removed before it can be misread.
// AGENT-GUARD: programs must not reach the font attribute themselves; SGR
// 10-19 is dropped here so only this function decides what font 1 means.
bool TerminalSession::Protocol::rewriteGraphics(const Sequence &sequence)
{
    const auto parameters = split(sequence.parameters, ';');
    QByteArray rewritten;
    bool changed = false;
    const auto keep = [&rewritten](QByteArrayView parameter) {
        if (!rewritten.isEmpty()) rewritten += ';';
        rewritten += parameter;
    };
    for (qsizetype index = 0; index < parameters.size(); ++index) {
        const QByteArrayView parameter = parameters.at(index);
        const int code = number(parameter);
        const bool subparameters = parameter.contains(':');
        if (!subparameters && (code == 38 || code == 48 || code == 58)) {
            // Legacy colour form: 38;5;N or 38;2;R;G;B spread over parameters.
            const int model = index + 1 < parameters.size() ? number(parameters.at(index + 1)) : -1;
            const qsizetype count = qMin<qsizetype>(model == 5 ? 3 : model == 2 ? 5 : 1,
                                                    parameters.size() - index);
            for (qsizetype offset = 0; offset < count; ++offset) {
                if (code == 58) changed = true;
                else keep(parameters.at(index + offset));
            }
            index += count - 1;
        } else if (code == 58 || code == 59 || code == 53 || code == 55 || (code >= 10 && code <= 19)) {
            changed = true;
        } else if (code == 2 && !subparameters) {
            keep("11");
            changed = true;
        } else if (code == 22) {
            keep("22;10");
            changed = true;
        } else {
            keep(parameter);
        }
    }
    if (!changed)
        return false;
    // An emptied sequence must not become CSI m, which resets every attribute.
    if (!rewritten.isEmpty())
        write(QByteArrayLiteral("\x1b[") + rewritten + 'm');
    return true;
}

bool TerminalSession::Protocol::privateModes(const Sequence &sequence, bool set)
{
    QByteArray kept;
    bool consumed = false, changed = false;
    for (const QByteArrayView parameter : split(sequence.parameters, ';')) {
        const int mode = number(parameter);
        if (mode == 2031) {
            m_themeNotifications = set;
            consumed = true;
            continue;
        }
        if (mode == 1007) {
            changed = changed || alternateScroll != set;
            alternateScroll = set;
            consumed = true;
            continue;
        }
        if (mode == 2004 && bracketedPaste != set) {
            bracketedPaste = set;
            changed = true;
        }
        if (!kept.isEmpty()) kept += ';';
        kept += parameter;
    }
    if (consumed && !kept.isEmpty())
        write(QByteArrayLiteral("\x1b[?") + kept + sequence.final);
    else if (!consumed)
        write(sequence.raw);
    if (changed) emit q->modesChanged();
    return true;
}

void TerminalSession::Protocol::keyboardProtocol(const Sequence &sequence)
{
    const auto parameters = split(sequence.parameters, ';');
    const int first = number(parameters.at(0));
    KeyboardStack &stack = keyboard();
    const int before = stack.current;
    switch (sequence.leader) {
    case '>':
        if (stack.saved.size() >= maxKeyboardStack) stack.saved.removeFirst();
        stack.saved.append(stack.current);
        stack.current = qMax(0, first) & TerminalKeyboard::AllFlags;
        break;
    case '<':
        for (int count = qMax(1, first); count > 0; --count)
            stack.current = stack.saved.isEmpty() ? 0 : stack.saved.takeLast();
        break;
    case '=': {
        const int flags = qMax(0, first) & TerminalKeyboard::AllFlags;
        const int mode = parameters.size() > 1 ? number(parameters.at(1)) : 1;
        if (mode == 2) stack.current |= flags;
        else if (mode == 3) stack.current &= ~flags;
        else stack.current = flags;
        break;
    }
    case '?':
        reply(QByteArrayLiteral("\x1b[?") + QByteArray::number(stack.current) + 'u');
        break;
    default: break;
    }
    if (before != stack.current) emit q->modesChanged();
}

bool TerminalSession::Protocol::reportWindow(const Sequence &sequence)
{
    const int request = number(split(sequence.parameters, ';').at(0));
    const auto report = [this](int code, int height, int width) {
        reply(QByteArrayLiteral("\x1b[") + QByteArray::number(code) + ';' + QByteArray::number(height)
              + ';' + QByteArray::number(width) + 't');
    };
    if (request == 18) {
        report(8, q->m_rows, q->m_columns);
    } else if ((request == 14 || request == 16) && cellWidth > 0 && cellHeight > 0) {
        if (request == 14) report(4, q->m_rows * cellHeight, q->m_columns * cellWidth);
        else report(6, cellHeight, cellWidth);
    } else {
        return false;
    }
    return true;
}

} // namespace QindaTK
