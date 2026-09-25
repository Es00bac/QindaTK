// SPDX-License-Identifier: LGPL-3.0-or-later
#include "midi_input_port.h"

#include "audio_engine.h"
#include "midi_types.h"

#if defined(QINDATK_HAVE_ALSA)
#include <QSocketNotifier>
#include <alsa/asoundlib.h>
#include <vector>
#endif

namespace QindaTK {

#if defined(QINDATK_HAVE_ALSA)

struct MidiInputPort::Port {
    snd_seq_t *seq = nullptr;
    int port = -1;
    std::vector<std::unique_ptr<QSocketNotifier>> notifiers;
    ~Port()
    {
        notifiers.clear();
        if (seq) snd_seq_close(seq);
    }
};

bool MidiInputPort::available()
{
    return true;
}

bool MidiInputPort::openPort(const QString &name)
{
    closePort();
    auto port = std::make_unique<Port>();
    if (const int error = snd_seq_open(&port->seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK); error < 0) {
        port->seq = nullptr;
        setError(tr("No MIDI sequencer: %1").arg(QString::fromLocal8Bit(snd_strerror(error))));
        return false;
    }
    const QByteArray label = name.toUtf8();
    snd_seq_set_client_name(port->seq, label.constData());
    port->port = snd_seq_create_simple_port(port->seq, label.constData(), SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port->port < 0) {
        setError(tr("The MIDI input port could not be created"));
        return false;
    }
    const int count = snd_seq_poll_descriptors_count(port->seq, POLLIN);
    std::vector<pollfd> fds(std::size_t(std::max(0, count)));
    snd_seq_poll_descriptors(port->seq, fds.data(), unsigned(fds.size()), POLLIN);
    for (const pollfd &fd : fds) {
        auto notifier = std::make_unique<QSocketNotifier>(fd.fd, QSocketNotifier::Read);
        connect(notifier.get(), &QSocketNotifier::activated, this, &MidiInputPort::read);
        port->notifiers.push_back(std::move(notifier));
    }
    m_port = std::move(port);
    emit portChanged();
    return true;
}

QString MidiInputPort::portAddress() const
{
    return m_port ? QStringLiteral("%1:%2").arg(snd_seq_client_id(m_port->seq)).arg(m_port->port) : QString();
}

bool MidiInputPort::connectPort(const QString &address)
{
    if (!m_port) {
        setError(tr("Open the MIDI input port first"));
        return false;
    }
    snd_seq_addr_t peer;
    if (snd_seq_parse_address(m_port->seq, &peer, address.toUtf8().constData()) < 0
        || snd_seq_connect_from(m_port->seq, m_port->port, peer.client, peer.port) < 0) {
        setError(tr("%1 could not be connected").arg(address));
        return false;
    }
    return true;
}

void MidiInputPort::read()
{
    snd_seq_event_t *event = nullptr;
    // Bounded: what is queued now; the notifier fires again for more.
    for (int n = 0; n < 1024 && m_port && snd_seq_event_input(m_port->seq, &event) >= 0 && event; ++n) {
        if (event->type == SND_SEQ_EVENT_NOTEON) receiveNote(event->data.note.note, event->data.note.velocity);
        else if (event->type == SND_SEQ_EVENT_NOTEOFF) receiveNote(event->data.note.note, 0);
        event = nullptr;
    }
}

#else

struct MidiInputPort::Port {};

bool MidiInputPort::available()
{
    return false;
}

bool MidiInputPort::openPort(const QString &)
{
    setError(tr("This build has no MIDI input (ALSA is off); feed receiveNote() from the host instead"));
    return false;
}

QString MidiInputPort::portAddress() const
{
    return {};
}

bool MidiInputPort::connectPort(const QString &)
{
    setError(tr("This build has no MIDI input (ALSA is off)"));
    return false;
}

void MidiInputPort::read() {}

#endif

MidiInputPort::MidiInputPort(QObject *parent)
    : QObject(parent)
{
}

MidiInputPort::~MidiInputPort()
{
    releaseAll();
}

bool MidiInputPort::portOpen() const
{
    return bool(m_port);
}

void MidiInputPort::closePort()
{
    releaseAll();
    if (!m_port) return;
    m_port.reset();
    emit portChanged();
}

void MidiInputPort::setEngine(AudioEngine *engine)
{
    if (m_engine == engine) return;
    releaseAll(); // the old engine keeps no note from this port
    m_engine = engine;
}

void MidiInputPort::receiveNote(int pitch, int velocity7)
{
    if (pitch < 0 || pitch > 127) return;
    ++m_received;
    if (velocity7 > 0) {
        m_held.set(std::size_t(pitch));
        if (m_engine) m_engine->auditionNoteOn16(pitch, Midi::velocityFrom7(std::min(velocity7, 127)));
    } else if (m_held.test(std::size_t(pitch))) {
        m_held.reset(std::size_t(pitch));
        if (m_engine) m_engine->auditionNoteOff(pitch);
    }
    emit notesChanged();
}

void MidiInputPort::releaseAll()
{
    if (m_held.none()) return;
    for (int pitch = 0; pitch < 128; ++pitch)
        if (m_held.test(std::size_t(pitch)) && m_engine) m_engine->auditionNoteOff(pitch);
    m_held.reset();
    emit notesChanged();
}

void MidiInputPort::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

} // namespace QindaTK
