// SPDX-License-Identifier: LGPL-3.0-or-later
#include "terminal_stream_filter_p.h"

namespace QindaTK::TerminalProtocol {
namespace {
constexpr char esc = '\x1b';
constexpr char bel = '\x07';
constexpr char can = '\x18';
constexpr char sub = '\x1a';

bool isPrivateMarker(char byte) { return byte >= '<' && byte <= '?'; }
} // namespace

void StreamFilter::reset()
{
    m_state = Ground;
    m_sequence.clear();
    m_payloadStart = 0;
    m_oscNumber = -1;
}

void StreamFilter::emitCsi(Sink &sink)
{
    Sequence sequence;
    sequence.kind = Sequence::Csi;
    sequence.raw = m_sequence;
    const QByteArrayView body = QByteArrayView(m_sequence).sliced(2, m_sequence.size() - 3);
    qsizetype parametersEnd = 0;
    while (parametersEnd < body.size() && body.at(parametersEnd) >= '0' && body.at(parametersEnd) <= '?')
        ++parametersEnd;
    sequence.parameters = body.first(parametersEnd);
    sequence.intermediates = body.sliced(parametersEnd);
    if (!sequence.parameters.isEmpty() && isPrivateMarker(sequence.parameters.front())) {
        sequence.leader = sequence.parameters.front();
        sequence.parameters = sequence.parameters.sliced(1);
    }
    sequence.final = m_sequence.back();
    sink.sequence(sequence);
    m_sequence.clear();
}

void StreamFilter::emitString(Sink &sink, Sequence::Kind kind, bool bell)
{
    Sequence sequence;
    sequence.kind = kind;
    sequence.raw = m_sequence;
    sequence.command = kind == Sequence::Osc ? m_oscNumber : -1;
    sequence.bell = bell;
    const qsizetype terminator = bell ? 1 : 2;
    sequence.payload = QByteArrayView(m_sequence)
        .sliced(m_payloadStart, m_sequence.size() - m_payloadStart - terminator);
    sink.sequence(sequence);
    m_sequence.clear();
}

void StreamFilter::feed(QByteArrayView bytes, Sink &sink)
{
    // Unintercepted bytes are forwarded as slices of the caller's buffer.
    qsizetype passStart = 0;
    const auto flushPass = [&](qsizetype end) {
        if (end > passStart)
            sink.text(bytes.sliced(passStart, end - passStart));
        passStart = end;
    };
    const auto passSequence = [&] {
        if (!m_sequence.isEmpty())
            sink.text(m_sequence);
        m_sequence.clear();
    };

    for (qsizetype index = 0; index < bytes.size(); ++index) {
        const char byte = bytes.at(index);
        // A state that cannot continue hands the byte to another state.
        for (bool again = true; again;) {
            again = false;
            switch (m_state) {
            case Ground:
                if (byte == esc) {
                    flushPass(index);
                    m_sequence.assign(1, esc);
                    m_state = Escape;
                }
                break;
            case Escape:
                passStart = index + 1;
                if (byte == '[') {
                    m_sequence += byte;
                    m_state = Csi;
                } else if (byte == ']') {
                    m_sequence += byte;
                    m_oscNumber = -1;
                    m_state = OscNumber;
                } else if (byte == 'P') {
                    m_sequence += byte;
                    m_state = DcsHead;
                } else if (byte == '_' || byte == '^' || byte == 'X') {
                    m_sequence += byte;
                    passSequence();
                    m_state = StringPass;
                } else if (byte >= 0x20 && byte <= 0x2f) {
                    m_sequence += byte;
                    m_state = EscapeIntermediate;
                } else if (byte == esc) {
                    passSequence();
                    m_sequence.assign(1, esc);
                } else if (byte == 'c') {
                    m_sequence += byte;
                    Sequence sequence;
                    sequence.kind = Sequence::Reset;
                    sequence.raw = m_sequence;
                    sequence.final = byte;
                    sink.sequence(sequence);
                    m_sequence.clear();
                    m_state = Ground;
                } else {
                    // A two-byte escape, or a control libvterm executes itself.
                    m_sequence += byte;
                    passSequence();
                    m_state = Ground;
                }
                break;
            case EscapeIntermediate:
                passStart = index + 1;
                if (byte == esc) {
                    passSequence();
                    m_sequence.assign(1, esc);
                    m_state = Escape;
                } else {
                    m_sequence += byte;
                    if (byte < 0x20 || byte > 0x2f) {
                        passSequence();
                        m_state = Ground;
                    }
                }
                break;
            case Csi:
                passStart = index + 1;
                if (byte >= 0x20 && byte <= 0x3f && m_sequence.size() < maxCsiBytes) {
                    m_sequence += byte;
                } else if (byte >= 0x40 && byte <= 0x7e) {
                    m_sequence += byte;
                    emitCsi(sink);
                    m_state = Ground;
                } else {
                    // An embedded control or an oversized sequence: libvterm
                    // keeps parsing it; this filter only stops looking.
                    passSequence();
                    passStart = index;
                    m_state = Ground;
                    again = true;
                }
                break;
            case OscNumber:
                passStart = index + 1;
                if (byte >= '0' && byte <= '9' && m_sequence.size() < 8) {
                    m_sequence += byte;
                    m_oscNumber = qMax(0, m_oscNumber) * 10 + (byte - '0');
                    break;
                }
                if (sink.wantsOsc(m_oscNumber)) {
                    if (byte == ';')
                        m_sequence += byte;
                    m_payloadStart = m_sequence.size();
                    m_state = OscBody;
                    again = byte != ';';
                } else {
                    passSequence();
                    passStart = index;
                    m_state = StringPass;
                    again = true;
                }
                break;
            case OscBody:
            case DcsBody: {
                passStart = index + 1;
                const bool osc = m_state == OscBody;
                const qsizetype limit = !osc ? maxDcsBytes
                    : m_oscNumber == 52 ? maxClipboardOscBytes : maxOscBytes;
                if (byte == bel) {
                    m_sequence += byte;
                    emitString(sink, osc ? Sequence::Osc : Sequence::Dcs, true);
                    m_state = Ground;
                } else if (byte == esc) {
                    m_state = osc ? OscBodyEscape : DcsBodyEscape;
                } else if (byte == can || byte == sub) {
                    m_sequence.clear();
                    m_state = Ground;
                } else if (m_sequence.size() >= limit) {
                    m_sequence.clear();
                    m_state = StringDiscard;
                } else {
                    m_sequence += byte;
                }
                break;
            }
            case OscBodyEscape:
            case DcsBodyEscape:
                passStart = index + 1;
                if (byte == '\\') {
                    m_sequence += esc;
                    m_sequence += byte;
                    emitString(sink, m_state == OscBodyEscape ? Sequence::Osc : Sequence::Dcs, false);
                    m_state = Ground;
                } else {
                    // A new escape ends an unterminated string; nothing from
                    // the partial request is applied.
                    m_sequence.assign(1, esc);
                    m_state = Escape;
                    again = true;
                }
                break;
            case DcsHead:
                passStart = index + 1;
                // Only XTGETTCAP ("+q") is answered here; DECRQSS stays with libvterm.
                if ((m_sequence.size() == 2 && byte == '+') || (m_sequence.size() == 3 && byte == 'q')) {
                    m_sequence += byte;
                    if (m_sequence.size() == 4) {
                        m_payloadStart = 4;
                        m_state = DcsBody;
                    }
                } else {
                    passSequence();
                    passStart = index;
                    m_state = StringPass;
                    again = true;
                }
                break;
            case StringPass:
                if (byte == bel || byte == can || byte == sub) {
                    m_state = Ground;
                } else if (byte == esc) {
                    flushPass(index);
                    m_sequence.assign(1, esc);
                    m_state = StringPassEscape;
                }
                break;
            case StringPassEscape:
                passStart = index + 1;
                if (byte == '\\') {
                    m_sequence += byte;
                    passSequence();
                    m_state = Ground;
                } else {
                    m_state = Escape;
                    again = true;
                }
                break;
            case StringDiscard:
                passStart = index + 1;
                if (byte == bel || byte == can || byte == sub)
                    m_state = Ground;
                else if (byte == esc)
                    m_state = StringDiscardEscape;
                break;
            case StringDiscardEscape:
                passStart = index + 1;
                if (byte == '\\') {
                    m_state = Ground;
                } else {
                    m_sequence.assign(1, esc);
                    m_state = Escape;
                    again = true;
                }
                break;
            }
        }
    }
    if (m_state == Ground || m_state == StringPass)
        flushPass(bytes.size());
}

} // namespace QindaTK::TerminalProtocol
