// SPDX-License-Identifier: LGPL-3.0-or-later
// AudioControlSurface's device transport: one duplex ALSA sequencer port
// (Linux, the supported platform; D-088 keeps ALSA optional). Hardware and
// software surfaces connect to it (connectPort, or any sequencer patchbay);
// incoming events are decoded to MIDI bytes for receive(), and feedback is
// encoded back. Everything here runs on the GUI thread through a socket
// notifier: the surface never touches the audio device thread.
#include "audio_control_surface.h"

#if defined(QINDATK_HAVE_ALSA)
#include <QSocketNotifier>
#include <alsa/asoundlib.h>
#include <algorithm>
#include <vector>
#endif

namespace QindaTK {

#if defined(QINDATK_HAVE_ALSA)

struct AudioControlSurface::Port {
    snd_seq_t *seq = nullptr;
    int port = -1;
    snd_midi_event_t *encoder = nullptr;
    snd_midi_event_t *decoder = nullptr;
    std::vector<std::unique_ptr<QSocketNotifier>> notifiers;
    ~Port()
    {
        notifiers.clear();
        if (encoder) snd_midi_event_free(encoder);
        if (decoder) snd_midi_event_free(decoder);
        if (seq) snd_seq_close(seq);
    }
};

bool AudioControlSurface::openPort(const QString &name)
{
    closePort();
    auto port = std::make_unique<Port>();
    const QByteArray label = name.toUtf8();
    if (const int error = snd_seq_open(&port->seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK); error < 0) {
        port->seq = nullptr;
        setError(tr("No MIDI sequencer: %1").arg(QString::fromLocal8Bit(snd_strerror(error))));
        return false;
    }
    snd_seq_set_client_name(port->seq, label.constData());
    port->port = snd_seq_create_simple_port(port->seq, label.constData(),
                                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_WRITE
                                                | SND_SEQ_PORT_CAP_SUBS_WRITE | SND_SEQ_PORT_CAP_DUPLEX,
                                            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port->port < 0 || snd_midi_event_new(64, &port->encoder) < 0 || snd_midi_event_new(64, &port->decoder) < 0) {
        setError(tr("The MIDI sequencer port could not be created"));
        return false;
    }
    snd_midi_event_no_status(port->decoder, 1); // every decoded message carries its status byte
    const int count = snd_seq_poll_descriptors_count(port->seq, POLLIN);
    std::vector<pollfd> fds(std::size_t(std::max(0, count)));
    snd_seq_poll_descriptors(port->seq, fds.data(), unsigned(fds.size()), POLLIN);
    for (const pollfd &fd : fds) {
        auto notifier = std::make_unique<QSocketNotifier>(fd.fd, QSocketNotifier::Read);
        connect(notifier.get(), &QSocketNotifier::activated, this, &AudioControlSurface::portRead);
        port->notifiers.push_back(std::move(notifier));
    }
    m_port = std::move(port);
    emit portChanged();
    refresh();
    return true;
}

void AudioControlSurface::closePort()
{
    if (!m_port) return;
    m_port.reset();
    emit portChanged();
}

bool AudioControlSurface::portOpen() const
{
    return bool(m_port);
}

QString AudioControlSurface::portAddress() const
{
    return m_port ? QStringLiteral("%1:%2").arg(snd_seq_client_id(m_port->seq)).arg(m_port->port) : QString();
}

bool AudioControlSurface::connectPort(const QString &address)
{
    if (!m_port) {
        setError(tr("Open the control surface port first"));
        return false;
    }
    snd_seq_addr_t peer;
    if (snd_seq_parse_address(m_port->seq, &peer, address.toUtf8().constData()) < 0) {
        setError(tr("%1 is not a MIDI port").arg(address));
        return false;
    }
    if (snd_seq_connect_from(m_port->seq, m_port->port, peer.client, peer.port) < 0
        || snd_seq_connect_to(m_port->seq, m_port->port, peer.client, peer.port) < 0) {
        setError(tr("%1 could not be connected").arg(address));
        return false;
    }
    refresh(); // a surface that just connected shows the current bank
    return true;
}

void AudioControlSurface::portRead()
{
    if (!m_port) return;
    snd_seq_event_t *event = nullptr;
    // Bounded: what is queued now; the notifier fires again for more.
    for (int n = 0; n < 1024 && m_port && snd_seq_event_input(m_port->seq, &event) >= 0 && event; ++n) {
        unsigned char bytes[16];
        snd_midi_event_reset_decode(m_port->decoder);
        const long size = snd_midi_event_decode(m_port->decoder, bytes, sizeof bytes, event);
        if (size > 0) receive(QByteArray(reinterpret_cast<const char *>(bytes), int(size)));
        event = nullptr;
    }
}

void AudioControlSurface::portWrite(const QByteArray &message)
{
    if (!m_port) return;
    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    snd_midi_event_reset_encode(m_port->encoder);
    const long used = snd_midi_event_encode(m_port->encoder, reinterpret_cast<const unsigned char *>(message.constData()),
                                            message.size(), &event);
    if (used <= 0 || event.type == SND_SEQ_EVENT_NONE) return;
    snd_seq_ev_set_source(&event, m_port->port);
    snd_seq_ev_set_subs(&event);
    snd_seq_ev_set_direct(&event);
    snd_seq_event_output_direct(m_port->seq, &event);
}

#else

struct AudioControlSurface::Port {};

bool AudioControlSurface::openPort(const QString &)
{
    setError(tr("This build has no MIDI transport (ALSA is off); feed receive() from the host instead"));
    return false;
}

void AudioControlSurface::closePort() {}
bool AudioControlSurface::portOpen() const { return false; }
QString AudioControlSurface::portAddress() const { return {}; }

bool AudioControlSurface::connectPort(const QString &)
{
    setError(tr("This build has no MIDI transport (ALSA is off)"));
    return false;
}

void AudioControlSurface::portRead() {}
void AudioControlSurface::portWrite(const QByteArray &) {}

#endif

// Both need the complete Port (the unique_ptr member).
AudioControlSurface::AudioControlSurface(QObject *parent)
    : QObject(parent)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMs);
    connect(&m_idle, &QTimer::timeout, this, &AudioControlSurface::idle);
}

AudioControlSurface::~AudioControlSurface()
{
    releaseAll();
}

} // namespace QindaTK
