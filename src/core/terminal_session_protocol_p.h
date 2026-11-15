// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "terminal_program_status_p.h"
#include "terminal_session.h"
#include "terminal_stream_filter_p.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QTimer>

namespace QindaTK {

// AGENT-NOTE (D-363, docs/terminal-session.md): everything TerminalSession
// speaks beyond libvterm 0.3 lives here: synchronized output, the kitty
// keyboard protocol and modifyOtherKeys, faint text, colour and capability
// queries, clipboard writes, notifications and program status. The public
// class keeps one pointer to it so its layout stays stable for hosts built
// against an earlier toolkit.
//
// Child output passes two tokenizers. The first only finds synchronized-update
// brackets (mode 2026) and holds a bracketed frame back whole; the second
// lifts out the sequences answered here and gives libvterm the rest.
class TerminalSession::Protocol {
public:
    explicit Protocol(TerminalSession *session);

    void input(QByteArrayView bytes);
    // A frame is never held past a timeout, a size limit or process exit.
    void endSynchronizedUpdate();
    void schedulePresent();
    void presentNow();
    void processStarted();
    void processEnded();
    void colorsChanged();

    int keyboardFlags() const;
    int modifyOtherKeys() const { return m_modifyOtherKeys; }

    bool bracketedPaste = false;
    bool alternateScroll = true;
    bool focused = false;
    int cellWidth = 0, cellHeight = 0;
    QString cursorColor;
    ProgramStatus::Store status;

    static constexpr int synchronizedTimeoutMs = 1000;
    static constexpr qsizetype synchronizedLimitBytes = 8 * 1024 * 1024;
    // Output is presented after a short quiet gap, or at this age at the latest.
    static constexpr int presentQuietMs = 2, presentMaxDelayMs = 8;

private:
    using Sequence = TerminalProtocol::Sequence;
    class Gate : public TerminalProtocol::StreamFilter::Sink {
    public:
        explicit Gate(Protocol *owner) : m_owner(owner) {}
        void text(QByteArrayView bytes) override { m_owner->gated(bytes); }
        void sequence(const Sequence &sequence) override;
        bool wantsOsc(int) const override { return false; }
    private:
        Protocol *m_owner;
    };
    class Handler : public TerminalProtocol::StreamFilter::Sink {
    public:
        explicit Handler(Protocol *owner) : m_owner(owner) {}
        void text(QByteArrayView bytes) override { m_owner->write(bytes); }
        void sequence(const Sequence &sequence) override { m_owner->handle(sequence); }
        bool wantsOsc(int command) const override;
    private:
        Protocol *m_owner;
    };
    struct KeyboardStack {
        int current = 0;
        QList<int> saved;
    };
    struct PendingNotification {
        QString title, body;
    };

    void gated(QByteArrayView bytes);
    void write(QByteArrayView bytes);
    void reply(const QByteArray &bytes);
    void handle(const Sequence &sequence);
    bool handleCsi(const Sequence &sequence);
    bool rewriteGraphics(const Sequence &sequence);
    bool privateModes(const Sequence &sequence, bool set);
    void keyboardProtocol(const Sequence &sequence);
    bool reportWindow(const Sequence &sequence);
    void resetState();
    KeyboardStack &keyboard();

    // terminal_session_osc.cpp
    void handleOsc(const Sequence &sequence);
    void handleTermcap(const Sequence &sequence);
    void colorQuery(const Sequence &sequence);
    void paletteQuery(const Sequence &sequence);
    void clipboard(const Sequence &sequence);
    void growlNotification(QByteArrayView payload);
    void kittyNotification(const Sequence &sequence);
    void programStatus(const Sequence &sequence);
    void notify(QString title, QString body);
    QByteArray colorSpec(int index) const; // 0..255 palette, -1 foreground, -2 background
    bool darkBackground() const;

    TerminalSession *q;
    Gate m_gate {this};
    Handler m_handler {this};
    TerminalProtocol::StreamFilter m_gateFilter, m_filter;
    QByteArray m_deferred;
    bool m_synchronized = false;
    QTimer m_synchronizedTimer, m_presentTimer;
    QElapsedTimer m_dirtySince;
    bool m_dirty = false;
    KeyboardStack m_mainKeyboard, m_alternateKeyboard;
    int m_modifyOtherKeys = 0;
    bool m_themeNotifications = false;
    bool m_statusReported = false;
    bool m_lastDark = true;
    QHash<QByteArray, PendingNotification> m_notifications;
};

} // namespace QindaTK
