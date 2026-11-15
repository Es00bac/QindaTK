// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QByteArrayView>

namespace QindaTK::TerminalProtocol {

// One complete control sequence lifted out of the child's output. Every view
// points into the filter's buffer and is valid only during Sink::sequence().
struct Sequence {
    enum Kind { Csi, Osc, Dcs, Reset };
    Kind kind = Csi;
    QByteArrayView raw;           // introducer through terminator, verbatim
    char leader = 0;              // CSI private marker (< = > ?), 0 when absent
    QByteArrayView parameters;    // CSI parameter bytes after the leader
    QByteArrayView intermediates; // CSI intermediate bytes
    char final = 0;               // CSI final byte
    int command = -1;             // OSC number
    QByteArrayView payload;       // OSC text after "N;", or DCS text after "+q"
    bool bell = false;            // the string ended with BEL rather than ST
};

// AGENT-NOTE (D-363, docs/terminal-session.md): libvterm 0.3 silently drops
// the sequences agent CLIs depend on and has no hook for private modes, SGR or
// DECRQM. This tokenizer sits in front of it and lifts out whole CSI
// sequences, the OSC numbers the sink asks for, XTGETTCAP and RIS. Everything
// else reaches libvterm byte for byte through Sink::text().
//
// AGENT-GUARD: text() and sequence() must be delivered in stream order. A
// reply written for a lifted query has to follow the replies libvterm wrote
// for the bytes before it, or programs that end a probe with DA1 misread it.
class StreamFilter {
public:
    class Sink {
    public:
        virtual ~Sink() = default;
        virtual void text(QByteArrayView bytes) = 0;
        // The sink forwards sequence.raw to text() itself to keep a sequence.
        virtual void sequence(const Sequence &sequence) = 0;
        virtual bool wantsOsc(int command) const = 0;
    };

    void feed(QByteArrayView bytes, Sink &sink);
    // Forget a partial sequence, e.g. when a new process starts on the session.
    void reset();

    static constexpr qsizetype maxEscapeBytes = 16;
    static constexpr qsizetype maxCsiBytes = 256;
    static constexpr qsizetype maxOscBytes = 8192;
    // OSC 52 carries a whole clipboard: 1 MiB of text is 1.4 MiB of base64.
    static constexpr qsizetype maxClipboardOscBytes = 1536 * 1024;
    static constexpr qsizetype maxDcsBytes = 1024;

private:
    enum State {
        Ground, Escape, EscapeIntermediate, Csi, OscNumber, OscBody, OscBodyEscape,
        DcsHead, DcsBody, DcsBodyEscape, StringPass, StringPassEscape,
        StringDiscard, StringDiscardEscape
    };
    void emitCsi(Sink &sink);
    void emitString(Sink &sink, Sequence::Kind kind, bool bell);

    State m_state = Ground;
    QByteArray m_sequence;
    qsizetype m_payloadStart = 0;
    int m_oscNumber = -1;
};

} // namespace QindaTK::TerminalProtocol
