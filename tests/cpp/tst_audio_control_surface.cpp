// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-062 / AUD-13: a hardware control surface (Mackie Control) and the strips
// edit and follow ONE parameter model. Fader touches are one transaction,
// feedback follows every other writer (automation, a strip), buttons are
// mixer operations with LED feedback, banks wait for a held fader, and the
// ALSA sequencer port carries the same messages to and from a real client.
#include "audio_control_surface.h"
#include "audio_mixer_fixture.h"

#include <QSignalSpy>
#include <QtTest>

#if defined(QINDATK_HAVE_ALSA)
#include <alsa/asoundlib.h>
#endif

using namespace QindaTK;
using namespace QindaTK::MixerFixture;

namespace {

QByteArray midi(std::initializer_list<int> bytes)
{
    QByteArray out;
    for (int byte : bytes) out.append(char(byte));
    return out;
}

QByteArray bend(int strip, int travel) { return midi({0xE0 | strip, travel & 0x7F, travel >> 7}); }

// The last message the surface sent with this status and first data byte.
QByteArray lastSent(const QSignalSpy &spy, int status, int first = -1)
{
    for (qsizetype i = spy.size() - 1; i >= 0; --i) {
        const QByteArray message = spy.at(i).at(0).toByteArray();
        if (quint8(message.at(0)) == status && (first < 0 || quint8(message.at(1)) == first)) return message;
    }
    return {};
}

int travelOf(const QByteArray &message) { return message.isEmpty() ? -1 : int(quint8(message.at(2))) << 7 | quint8(message.at(1)); }

} // namespace

class TestAudioControlSurface : public QObject {
    Q_OBJECT
private slots:
    void surfaceAndStripsShareTheParameterModel()
    {
        Fixture f;
        QStringList tracks;
        for (int i = 0; i < 10; ++i) tracks.push_back(f.session.addTrack(QStringLiteral("T%1").arg(i)).prepend(QStringLiteral("track:")));
        AudioControlSurface surface;
        QSignalSpy sent(&surface, &AudioControlSurface::outgoing);
        surface.setMixer(f.mixer);
        QCOMPARE(surface.channels().first(), tracks.at(0));
        QCOMPARE(surface.channels().last(), tracks.at(7));
        AudioParameterBinding *gain = f.mixer->parameterBinding(tracks.at(0), QStringLiteral("gain"));
        // The motor fader starts where the strip's fader is: unity.
        QCOMPARE(travelOf(lastSent(sent, 0xE0)), int(std::lround(gain->mapper()->positionFor(0.0) * 16383)));

        // Touch, move, release: one transaction on the strip's own binding.
        const int history = f.session.automation()->historyCount();
        QVERIFY(surface.receive(midi({0x90, 0x68, 0x7F})));
        QVERIFY(gain->gestureActive());
        QCOMPARE(f.mixer->heldChannels(), QStringList{tracks.at(0)});
        const int travel = int(std::lround(gain->mapper()->positionFor(-12.0) * 16383));
        surface.receive(bend(0, travel / 2));
        surface.receive(bend(0, travel));
        const double target = gain->mapper()->valueAt(double(travel) / 16383).value(QStringLiteral("value")).toDouble();
        QCOMPARE(gain->value(), target); // previewed live, as a strip drag is
        sent.clear();
        surface.receive(midi({0x90, 0x68, 0x00}));
        QVERIFY(!gain->gestureActive());
        QCOMPARE(f.session.automation()->historyCount(), history + 1);
        QVERIFY(std::abs(gain->value() - target) < 1e-9);

        // Another writer (a strip, automation, undo) moves the motor fader.
        QVERIFY(f.session.automation()->undo());
        QCOMPARE(gain->value(), 0.0);
        QCOMPARE(travelOf(lastSent(sent, 0xE0)), int(std::lround(gain->mapper()->positionFor(0.0) * 16383)));
        AudioParameterBinding strip; // what a Fader delegate binds
        strip.setSession(&f.session);
        strip.setParameterId(tracks.at(0) + QStringLiteral("/gain"));
        QVERIFY(strip.begin(0.0) && strip.commit(-6.0));
        QCOMPARE(gain->value(), -6.0);
        QCOMPARE(travelOf(lastSent(sent, 0xE0)), int(std::lround(gain->mapper()->positionFor(-6.0) * 16383)));
        // ...but never a fader the surface is holding.
        surface.receive(midi({0x90, 0x68, 0x7F}));
        sent.clear();
        QVERIFY(strip.begin(-6.0) && strip.preview(-3.0));
        QVERIFY(lastSent(sent, 0xE0).isEmpty());
        QVERIFY(strip.cancel());
        surface.receive(midi({0x90, 0x68, 0x00}));

        // A surface without touch sensing: a burst of bends is one step after idle.
        const int before = f.session.automation()->historyCount();
        AudioParameterBinding *second = f.mixer->parameterBinding(tracks.at(1), QStringLiteral("gain"));
        surface.receive(bend(1, 4000));
        surface.receive(bend(1, 5000));
        QVERIFY(second->gestureActive());
        QTRY_VERIFY(!second->gestureActive());
        QCOMPARE(f.session.automation()->historyCount(), before + 1);

        // V-Pot: steps of the pan's 0.01; the ring follows (dot 1..11).
        AudioParameterBinding *pan = f.mixer->parameterBinding(tracks.at(2), QStringLiteral("pan"));
        surface.receive(midi({0xB0, 0x12, 0x45})); // strip 3, five steps left
        surface.receive(midi({0xB0, 0x12, 0x45}));
        QCOMPARE(pan->value(), -0.1);
        QTRY_VERIFY(!pan->gestureActive());
        QCOMPARE(f.session.automation()->historyCount(), before + 2);
        QVERIFY(pan->begin(-0.1) && pan->commit(1.0));
        QCOMPARE(lastSent(sent, 0xB0, 0x32), midi({0xB0, 0x32, 11}));

        // Buttons are mixer operations; LEDs follow the model from any writer.
        surface.receive(midi({0x90, 0x10, 0x7F})); // mute strip 1
        QVERIFY(f.value(tracks.at(0), "muted").toBool());
        QCOMPARE(lastSent(sent, 0x90, 0x10), midi({0x90, 0x10, 0x7F}));
        QVERIFY(ok(f.mixer->setMuted(tracks.at(0), false)));
        QCOMPARE(lastSent(sent, 0x90, 0x10), midi({0x90, 0x10, 0x00}));
        surface.receive(midi({0x90, 0x09, 0x7F})); // solo strip 2
        QVERIFY(f.value(tracks.at(1), "soloed").toBool());
        surface.receive(midi({0x90, 0x02, 0x7F})); // arm strip 3
        QVERIFY(f.value(tracks.at(2), "armed").toBool());
        QSignalSpy selected(&surface, &AudioControlSurface::channelSelected);
        surface.receive(midi({0x90, 0x1B, 0x7F})); // select strip 4
        QCOMPARE(selected.size(), 1);
        QCOMPARE(surface.selectedChannelId(), tracks.at(3));
        QCOMPARE(lastSent(sent, 0x90, 0x1B), midi({0x90, 0x1B, 0x7F}));

        // The master fader (pitch bend channel 9) with its touch note.
        AudioParameterBinding *master = f.mixer->parameterBinding(QStringLiteral("master"), QStringLiteral("gain"));
        surface.receive(midi({0x90, 0x70, 0x7F, 0xE8, 0x00, 0x40}));
        surface.receive(midi({0x90, 0x70, 0x00}));
        QVERIFY(!master->gestureActive());
        QCOMPARE(master->value(), master->mapper()->valueAt(8192.0 / 16383).value(QStringLiteral("value")).toDouble());
    }

    void banksWaitForAHeldFader()
    {
        Fixture f;
        QStringList tracks;
        for (int i = 0; i < 12; ++i) tracks.push_back(f.session.addTrack(QStringLiteral("T%1").arg(i)).prepend(QStringLiteral("track:")));
        AudioControlSurface surface;
        surface.setMixer(f.mixer);
        AudioParameterBinding *first = f.mixer->parameterBinding(tracks.at(0), QStringLiteral("gain"));
        surface.receive(midi({0x90, 0x68, 0x7F}));
        surface.receive(midi({0x90, 0x2F, 0x7F})); // bank right while the fader is held
        QCOMPARE(surface.bank(), 0);
        QVERIFY(surface.held());
        surface.receive(bend(0, 2000));
        surface.receive(midi({0x90, 0x68, 0x00})); // the release ends it on the channel it began on
        QVERIFY(!first->gestureActive());
        QVERIFY(first->value() < 0.0);
        QCOMPARE(surface.bank(), 8);             // then the bank applies
        QCOMPARE(surface.channels().first(), tracks.at(8));
        QCOMPARE(surface.channels().at(4), QString()); // the bank runs out
        surface.receive(midi({0x90, 0x30, 0x7F})); // one channel left
        QCOMPARE(surface.bank(), 7);
        // A reorder in the timeline moves what the surface shows.
        QVERIFY(f.moveTrack(tracks.at(11), tracks.at(7)));
        QCOMPARE(surface.channels().first(), tracks.at(11));
    }

    void sequencerPortCarriesTheProtocol()
    {
#if !defined(QINDATK_HAVE_ALSA)
        QSKIP("This build has no ALSA transport");
#else
        snd_seq_t *peer = nullptr;
        if (snd_seq_open(&peer, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0) QSKIP("No ALSA sequencer here");
        const int peerPort = snd_seq_create_simple_port(peer, "test surface",
                                                        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ
                                                            | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                                        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_HARDWARE);
        QVERIFY(peerPort >= 0);
        Fixture f;
        const QString track = QStringLiteral("track:") + f.session.addTrack(QStringLiteral("Voice"));
        AudioControlSurface surface;
        surface.setMixer(f.mixer);
        QVERIFY2(surface.openPort(QStringLiteral("QindaTK test")), qPrintable(surface.lastError()));
        QVERIFY(surface.portOpen());
        QVERIFY2(surface.connectPort(QStringLiteral("%1:%2").arg(snd_seq_client_id(peer)).arg(peerPort)),
                 qPrintable(surface.lastError()));
        const auto sendEvent = [&](const std::function<void(snd_seq_event_t *)> &fill) {
            snd_seq_event_t event;
            snd_seq_ev_clear(&event);
            fill(&event);
            snd_seq_ev_set_source(&event, peerPort);
            snd_seq_ev_set_subs(&event);
            snd_seq_ev_set_direct(&event);
            return snd_seq_event_output_direct(peer, &event) >= 0;
        };
        // The hardware touches fader 1, moves it and lets go.
        AudioParameterBinding *gain = f.mixer->parameterBinding(track, QStringLiteral("gain"));
        const int history = f.session.automation()->historyCount();
        QVERIFY(sendEvent([](snd_seq_event_t *e) { snd_seq_ev_set_noteon(e, 0, 0x68, 127); }));
        QTRY_VERIFY(gain->gestureActive());
        QVERIFY(sendEvent([](snd_seq_event_t *e) { snd_seq_ev_set_pitchbend(e, 0, 2000 - 8192); }));
        QVERIFY(sendEvent([](snd_seq_event_t *e) { snd_seq_ev_set_noteon(e, 0, 0x68, 0); }));
        QTRY_VERIFY(!gain->gestureActive());
        QCOMPARE(f.session.automation()->historyCount(), history + 1);
        QCOMPARE(gain->value(), gain->mapper()->valueAt(2000.0 / 16383).value(QStringLiteral("value")).toDouble());
        // Automation (here an undo) drives the motor fader through the port.
        int received = -1;
        const auto drain = [&] {
            snd_seq_event_t *event = nullptr;
            while (snd_seq_event_input(peer, &event) >= 0 && event) {
                if (event->type == SND_SEQ_EVENT_PITCHBEND && event->data.control.channel == 0)
                    received = event->data.control.value + 8192;
                event = nullptr;
            }
            return received;
        };
        const int moved = int(std::lround(gain->mapper()->positionFor(gain->value(), gain->silent()) * 16383));
        QTRY_COMPARE(drain(), moved); // the committed position, sent after the release
        QVERIFY(f.session.automation()->undo());
        const int unity = int(std::lround(gain->mapper()->positionFor(0.0) * 16383));
        QVERIFY(unity != moved);
        QTRY_COMPARE(drain(), unity);
        surface.closePort();
        QVERIFY(!surface.portOpen());
        snd_seq_close(peer);
#endif
    }
};

QTEST_MAIN(TestAudioControlSurface)
#include "tst_audio_control_surface.moc"
