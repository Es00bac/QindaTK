// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-062: the mixed output. Every check renders the AudioRenderGraph the
// device plays (no engine: the master feeds "main" in stereo), so the numbers
// are what the outputs receive: pan laws, width and balance, buses, sends and
// faders, mute/solo/groups, automation, and the two-parameter pad gesture.
#include "audio_mixer_fixture.h"

#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::MixerFixture;

class TestAudioMixerDsp : public QObject {
    Q_OBJECT
private slots:
    void panLawsPlaceAMonoSource()
    {
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        QVERIFY(ok(f.mixer->setLayout(voice, QStringLiteral("Mono"))));
        struct Case { const char *law; double pan; double left; double right; };
        const Case cases[] = {
            {"-3dB", 0.0, std::cos(Quarter), std::cos(Quarter)},
            {"-3dB", -1.0, 1.0, 0.0},
            {"-3dB", 1.0, 0.0, 1.0},
            {"-3dB", 0.5, std::cos(1.5 * Quarter), std::cos(0.5 * Quarter)},
            {"-4.5dB", 0.0, std::sqrt(std::cos(Quarter) * 0.5), std::sqrt(std::cos(Quarter) * 0.5)},
            {"-6dB", 0.0, 0.5, 0.5},
            {"-6dB", -0.5, 0.75, 0.25},
            {"0dB", 0.0, 1.0, 1.0},
            {"0dB", 0.5, 0.5, 1.0},
        };
        for (const Case &entry : cases) {
            QVERIFY(ok(f.mixer->setPanLaw(QLatin1String(entry.law))));
            QVERIFY(f.set(voice, QStringLiteral("pan"), entry.pan));
            const std::vector<float> out = f.frame();
            QCOMPARE(out.at(0), float(0.5 * entry.left));
            QCOMPARE(out.at(1), float(0.5 * entry.right));
        }
        // Constant power: the centre of the -3 dB law carries the same power as a hard pan.
        QVERIFY(near(std::cos(Quarter) * std::cos(Quarter) * 2.0, 1.0));
        QVERIFY(!ok(f.mixer->setPanLaw(QStringLiteral("-2dB"))));
    }

    void stereoWidthAndBalance()
    {
        Fixture f;
        const QString pair = f.track(QStringLiteral("Pair"), 0.5f, 0.25f);
        const auto expect = [&](float left, float right) {
            const std::vector<float> out = f.frame();
            return out.at(0) == left && out.at(1) == right;
        };
        QVERIFY(expect(0.5f, 0.25f)); // width 1, centre: the file as recorded
        QVERIFY(f.set(pair, QStringLiteral("width"), 0.0));
        QVERIFY(expect(0.375f, 0.375f)); // mid only
        QVERIFY(f.set(pair, QStringLiteral("width"), 2.0));
        QVERIFY(expect(0.625f, 0.125f)); // side doubled
        QVERIFY(f.set(pair, QStringLiteral("width"), 1.0));
        QVERIFY(f.set(pair, QStringLiteral("pan"), 0.5));
        QVERIFY(expect(0.25f, 0.25f)); // balance: the left side halves, the right stays
        QVERIFY(f.set(pair, QStringLiteral("pan"), -1.0));
        QVERIFY(expect(0.5f, 0.0f));
        QVERIFY(f.set(pair, QStringLiteral("pan"), 0.0));
        QCOMPARE(f.frame(0, 1).at(0), 0.375f); // a mono output averages the stereo master
        // A mono channel takes the average of the file's two channels.
        QVERIFY(ok(f.mixer->setLayout(pair, QStringLiteral("Mono"))));
        QCOMPARE(f.left(), float(0.375 * std::cos(Quarter)));
    }

    void busesSendsAndFadersSum()
    {
        Fixture f;
        const QString a = f.track(QStringLiteral("A"), 0.5f);
        const QString b = f.track(QStringLiteral("B"), 0.25f);
        const QString c = f.track(QStringLiteral("C"), 0.125f);
        const QString drums = f.bus(QStringLiteral("Drums"));
        const QString verb = f.bus(QStringLiteral("Verb"));
        const QString cue = f.bus(QStringLiteral("Cue"));
        QVERIFY(ok(f.mixer->setOutput(a, drums)));
        QVERIFY(ok(f.mixer->setOutput(b, drums)));
        QVERIFY(f.set(drums, QStringLiteral("gain"), -6.0));
        QVERIFY(f.set(c, QStringLiteral("gain"), -12.0));
        const QVariantMap post = f.mixer->addSend(c, verb, false);
        QVERIFY(ok(post));
        const QString postSend = post.value(QStringLiteral("sendId")).toString();
        QCOMPARE(post.value(QStringLiteral("parameterId")).toString(), postSend + QStringLiteral("/level"));
        QVERIFY(f.set(postSend, QStringLiteral("level"), -6.0));
        const QVariantMap pre = f.mixer->addSend(c, cue, true);
        QVERIFY(ok(pre));
        const QString preSend = pre.value(QStringLiteral("sendId")).toString();
        QCOMPARE(int(f.value(c, "sends").toList().size()), 2);
        const double drumsPart = 0.75 * linear(-6.0);
        const std::vector<float> out = f.frame();
        const double expected = drumsPart + 0.125 * linear(-12.0) * (1.0 + linear(-6.0)) + 0.125;
        QVERIFY2(near(out.at(0), expected) && near(out.at(1), expected), qPrintable(QString::number(out.at(0))));
        // C's fader at its floor is exact silence (D-101): its post-fader routes
        // stop, its pre-fader send does not.
        QVERIFY(f.set(c, QStringLiteral("gain"), -60.0));
        QVERIFY(near(f.left(), drumsPart + 0.125));
        QVERIFY(ok(f.mixer->setSendPreFader(preSend, false)));
        QVERIFY(near(f.left(), drumsPart));
        // Removing a bus disconnects what fed it and reports it; nothing is rerouted.
        const QVariantMap removed = f.mixer->removeBus(drums);
        QVERIFY(ok(removed));
        QStringList disconnected = removed.value(QStringLiteral("disconnected")).toStringList();
        disconnected.sort();
        QStringList sources{a, b};
        sources.sort();
        QCOMPARE(disconnected, sources);
        QCOMPARE(f.value(a, "output").toString(), QString());
        QCOMPARE(f.left(), 0.0f);
        QVERIFY(ok(f.mixer->removeSend(postSend)));
        QCOMPARE(int(f.value(c, "sends").toList().size()), 1);
    }

    void muteSoloGroupsAndSoloSafe()
    {
        Fixture f;
        const QString t1 = f.track(QStringLiteral("T1"), 0.5f);
        const QString t2 = f.track(QStringLiteral("T2"), 0.25f);
        const QString t3 = f.track(QStringLiteral("T3"), 0.0625f);
        const QString x = f.bus(QStringLiteral("X"));
        const QString r = f.bus(QStringLiteral("Return"));
        QVERIFY(ok(f.mixer->setOutput(t1, x)));
        QVERIFY(ok(f.mixer->addSend(t3, r)));
        QCOMPARE(f.left(), 0.875f);
        // Solo in place: a soloed track keeps the bus it feeds; the track flag is the timeline's.
        QVERIFY(ok(f.mixer->setSoloed(t1, true)));
        QVERIFY(f.session.timeline()->trackRecord(AudioMixer::trackOf(t1))->solo);
        QCOMPARE(f.left(), 0.5f);
        QVERIFY(f.value(t2, "implicitlyMuted").toBool());
        QVERIFY(f.value(r, "implicitlyMuted").toBool());
        QVERIFY(!f.value(x, "implicitlyMuted").toBool());
        // A solo-safe return stays open, but only soloed sources reach it.
        QVERIFY(ok(f.mixer->setSoloSafe(r, true)));
        QVERIFY(!f.value(r, "implicitlyMuted").toBool());
        QCOMPARE(f.left(), 0.5f);
        QVERIFY(ok(f.mixer->setSoloed(t1, false)));
        // A soloed bus keeps its sources; a soloed send source keeps its return.
        QVERIFY(ok(f.mixer->setSoloed(x, true)));
        QCOMPARE(f.left(), 0.5f);
        QVERIFY(ok(f.mixer->setSoloed(x, false)));
        QVERIFY(ok(f.mixer->setSoloed(t3, true)));
        QCOMPARE(f.left(), 0.125f);
        QVERIFY(ok(f.mixer->setSoloed(t3, false)));
        QCOMPARE(f.left(), 0.875f);
        // A muted bus silences what reaches the output through it.
        QVERIFY(ok(f.mixer->setMuted(x, true)));
        QCOMPARE(f.left(), 0.375f);
        QVERIFY(ok(f.mixer->setMuted(x, false)));
        // A group's mute or solo applies to every member; members keep their own flags.
        const QVariantMap group = f.mixer->addGroup(QStringLiteral("Keys"), {t2, t3});
        QVERIFY(ok(group));
        const QString keys = group.value(QStringLiteral("groupId")).toString();
        QCOMPARE(f.value(t2, "groupNames").toStringList(), QStringList{QStringLiteral("Keys")});
        QVERIFY(ok(f.mixer->setGroupMuted(keys, true)));
        QCOMPARE(f.left(), 0.5f);
        QVERIFY(f.value(t3, "effectiveMuted").toBool());
        QVERIFY(!f.value(t3, "muted").toBool());
        QVERIFY(ok(f.mixer->setGroupMuted(keys, false)));
        QVERIFY(ok(f.mixer->setGroupSoloed(keys, true)));
        QCOMPARE(f.left(), 0.375f);
        QVERIFY(ok(f.mixer->setGroupSoloed(keys, false)));
        QVERIFY(ok(f.mixer->setMuted(QStringLiteral("master"), true)));
        QCOMPARE(f.left(), 0.0f);
    }

    void automationDrivesPanAndSendLevels()
    {
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        QVERIFY(ok(f.mixer->setLayout(voice, QStringLiteral("Mono"))));
        const QString pan = f.mixer->ensureParameter(voice, QStringLiteral("pan"));
        QVERIFY(f.point(pan, 0, -1.0));
        QVERIFY(f.point(pan, 48000, 1.0));
        std::vector<float> out = f.frame(0);
        QCOMPARE(out.at(0), 0.5f);
        QCOMPARE(out.at(1), 0.0f);
        out = f.frame(24000); // pan 0, sample-accurate from the curve
        QCOMPARE(out.at(0), float(0.5 * std::cos(Quarter)));
        QCOMPARE(out.at(1), float(0.5 * std::cos(Quarter)));
        // A send level curve: silent (its floor) at the start, unity from 24000.
        const QString echo = f.bus(QStringLiteral("Echo"));
        QVERIFY(ok(f.mixer->setOutput(voice, QString()))); // only the send reaches the output
        const QString level = f.mixer->addSend(voice, echo).value(QStringLiteral("parameterId")).toString();
        QCOMPARE(f.mixer->ensureParameter(level.section(QLatin1Char('/'), 0, 0), QStringLiteral("level")), level);
        QVERIFY(f.point(level, 0, -60.0));
        QVERIFY(f.point(level, 24000, 0.0));
        out = f.frame(0);
        QCOMPARE(out.at(0), 0.0f);
        out = f.frame(24000);
        QCOMPARE(out.at(0), float(0.5 * std::cos(Quarter)));
        QCOMPARE(out.at(1), float(0.5 * std::cos(Quarter)));
    }

    void padGestureIsOneUndoStep()
    {
        Fixture f;
        const QString pair = f.track(QStringLiteral("Pair"), 0.5f, 0.25f);
        AudioParameterBinding *pan = f.mixer->parameterBinding(pair, QStringLiteral("pan"));
        AudioParameterBinding *width = f.mixer->parameterBinding(pair, QStringLiteral("width"));
        const int steps = f.session.automation()->historyCount();
        QVERIFY(pan->begin(0.0));
        QVERIFY(width->begin(1.0));
        QVERIFY(pan->preview(-0.5));
        QVERIFY(width->preview(0.0));
        std::vector<float> out = f.frame(); // held values are heard: mid 0.375, balance -0.5
        QCOMPARE(out.at(0), 0.375f);
        QCOMPARE(out.at(1), 0.1875f);
        QVERIFY(pan->commitWith(-0.5, width, 0.0));
        QCOMPARE(f.session.automation()->historyCount(), steps + 1);
        QCOMPARE(pan->lastResult(), QStringLiteral("static"));
        QCOMPARE(width->lastResult(), QStringLiteral("static"));
        QVERIFY(!pan->gestureActive() && !width->gestureActive());
        out = f.frame();
        QCOMPARE(out.at(0), 0.375f);
        QVERIFY(f.session.automation()->undo()); // one step restores both
        out = f.frame();
        QCOMPARE(out.at(0), 0.5f);
        QCOMPARE(out.at(1), 0.25f);
    }
};

QTEST_MAIN(TestAudioMixerDsp)
#include "tst_audio_mixer_dsp.moc"
