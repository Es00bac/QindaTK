// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-065 on the silent built-in loopback device: a loop recording becomes one
// take per pass at the exact wrap frames, the passes line up with what was
// heard, a punched second recording joins the comp, and neither recording
// nor comp playback with crossfades allocates on the device thread.
#include "audio_session.h"
#include "audio_take_comp_fixture.h"

#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::CompFixture;

#include "audio_allocation_probe.h"

namespace {

struct Rig {
    QTemporaryDir dir;
    AudioEngine engine;
    AudioSession session;
    TransportModel *transport = engine.transport();
    Rig()
    {
        session.setMediaDirectory(dir.filePath(QStringLiteral("takes")));
        session.setEngine(&engine);
    }
    QString apply(const QString &device, int buffer)
    {
        QSignalSpy spy(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), device},
                                                           {QStringLiteral("sampleRate"), Rate},
                                                           {QStringLiteral("bufferSize"), buffer},
                                                           {QStringLiteral("inputLayout"), QStringLiteral("Mono")},
                                                           {QStringLiteral("outputLayout"), QStringLiteral("Mono")}});
        if (spy.isEmpty() && !spy.wait(5000)) return QStringLiteral("timeout");
        return spy.last().at(1).toString();
    }
    // Records from 1000 over the loop [2000, 10000) for about `ms`; the
    // finished take's map lands in `take`.
    void record(int ms, QVariantMap &take)
    {
        QSignalSpy recorded(&session, &AudioSession::takeRecorded);
        transport->requestSeek(QStringLiteral("1000"));
        QTRY_COMPARE_WITH_TIMEOUT(transport->pendingCount(), 0, 5000);
        transport->requestRecord(true);
        QTRY_VERIFY_WITH_TIMEOUT(transport->recording(), 5000);
        QTest::qWait(ms);
        transport->requestStop();
        QTRY_VERIFY_WITH_TIMEOUT(!transport->playing(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(recorded.size(), 1, 5000);
        take = recorded.first().at(2).toMap();
    }
};

} // namespace

class TestAudioTakeCompEngine : public QObject {
    Q_OBJECT
private slots:
    void loopRecordingMakesAlignedPassTakes()
    {
        Rig rig;
        QCOMPARE(rig.apply(QStringLiteral("builtin:loopback"), 256), QStringLiteral("accepted"));
        const QString source = rig.session.addTrack(QStringLiteral("Source"));
        QVERIFY(rig.session.importMedia(writeMono(rig.dir.filePath(QStringLiteral("ramp.wav")), 48000, ramp), source)
                    .value(QStringLiteral("ok")).toBool());
        const QString takes = rig.session.addTrack(QStringLiteral("Takes"));
        QVERIFY(rig.session.setTrackArmed(takes, true));
        QTRY_VERIFY(rig.transport->canRecord());
        rig.transport->requestLoop(true, QStringLiteral("2000"), QStringLiteral("10000"));
        QTRY_COMPARE(rig.transport->pendingCount(), 0);

        // Recording (loop wraps push take jumps) must not allocate on the device thread.
        g_probeThread.store(rig.engine.realtimeThreadToken());
        g_deviceAllocations.store(0);
        g_probing.store(true);
        QVariantMap take;
        rig.record(420, take);
        g_probing.store(false);
        if (QTest::currentTestFailed()) return;
        const int recordingAllocations = g_deviceAllocations.load();
        const qint64 frames = take.value(QStringLiteral("frames")).toString().toLongLong();
        QVERIFY2(frames > 17000 + 256, qPrintable(QString::number(frames))); // at least three passes
        // Wraps at transport 10000: after 9000 frames, then every 8000.
        const QVariantList jumps = take.value(QStringLiteral("jumps")).toList();
        QVERIFY(jumps.size() >= 2);
        for (qsizetype k = 0; k < jumps.size(); ++k) {
            QCOMPARE(jumps.at(k).toMap().value(QStringLiteral("frame")).toString(), text(9000 + 8000 * k));
            QCOMPARE(jumps.at(k).toMap().value(QStringLiteral("position")).toString(), QStringLiteral("2000"));
        }
        QCOMPARE(take.value(QStringLiteral("jumpsDropped")).toString(), QStringLiteral("0"));

        // One comp clip at the first heard sample (1000 - 256); one take per pass.
        const QString clipId = rig.session.timeline()->clipAt(takes, text(ticks(800)));
        const QString compId = rig.session.compForClip(clipId);
        QVERIFY(!compId.isEmpty());
        QCOMPARE(rig.session.timeline()->clipRecord(QUuid::fromString(clipId))->start, ticks(744));
        AudioTakeCompModel *comps = rig.session.comps();
        const QVariantList list = comps->compData(compId).value(QStringLiteral("takes")).toList();
        qsizetype passes = 1; // a wrap in the last 256 frames starts no audible pass
        for (const QVariant &jump : jumps) passes += jump.toMap().value(QStringLiteral("frame")).toString().toLongLong() + 256 < frames;
        QCOMPARE(list.size(), passes);
        for (qsizetype k = 1; k < list.size(); ++k) {
            const QVariantMap pass = list.at(k).toMap();
            QCOMPARE(pass.value(QStringLiteral("sourceStart")).toString(), text(9000 + 8000 * (k - 1) + 256));
            // Heard from the loop start: comp sample 2000 - 744 is its first frame.
            QCOMPARE(pass.value(QStringLiteral("sourceOffset")).toString(), text(9000 + 8000 * (k - 1) + 256 - 1256));
        }

        // Every pass recorded what was heard, so without crossfades the comp
        // plays the source ramp exactly wherever it has audio.
        TimelineEditRequest mute;
        mute.kind = TimelineEditKind::TrackProperty;
        mute.trackId = QUuid::fromString(source);
        mute.property = QStringLiteral("muted");
        mute.value = true;
        QVERIFY(!rig.session.timeline()->apply(mute, {}));
        QVERIFY(comps->setFade(compId, QStringLiteral("0"), QStringLiteral("linear")).value(QStringLiteral("ok")).toBool());
        const auto check = [&](const char *name) {
            const std::vector<float> data = render(rig.session, rig.dir.filePath(QString::fromLatin1(name)), 10000);
            int wrong = data.size() == 10000 ? 0 : 1;
            for (qint64 t = 0; t < qint64(data.size()); ++t) {
                const float want = t >= 1000 ? ramp(t) : 0.0f; // one period of loopback silence before 1000
                if (data[size_t(t)] != want && ++wrong < 4) qWarning("%s[%lld] = %.9g, want %.9g", name, t, data[size_t(t)], want);
            }
            return wrong;
        };
        QCOMPARE(check("passes.wav"), 0);

        // A punched second recording joins the comp as one comp step.
        QSet<QString> oldTakes;
        for (const QVariant &value : list) oldTakes.insert(value.toMap().value(QStringLiteral("takeId")).toString());
        rig.transport->requestPunch(true, QStringLiteral("3000"), QStringLiteral("5000"));
        QTRY_VERIFY(rig.transport->punchEnabled());
        QCOMPARE(rig.transport->punchStartSample(), QStringLiteral("3000"));
        QVariantMap punched;
        rig.record(250, punched);
        if (QTest::currentTestFailed()) return;
        QVERIFY(punched.value(QStringLiteral("punchEnabled")).toBool());
        QCOMPARE(rig.session.compForClip(clipId), compId);
        QVERIFY(comps->compData(compId).value(QStringLiteral("takes")).toList().size() > oldTakes.size());
        QCOMPARE(comps->historyCount(), 2); // the fade change and the punched recording
        // Exactly the punch range [3000, 5000) (comp [2256, 4256)) plays new takes.
        qint64 punchedIn = 0;
        for (const QVariant &value : comps->compData(compId).value(QStringLiteral("segments")).toList()) {
            const QVariantMap segment = value.toMap();
            const qint64 start = segment.value(QStringLiteral("start")).toString().toLongLong();
            const qint64 end = segment.value(QStringLiteral("end")).toString().toLongLong();
            const bool fresh = !oldTakes.contains(segment.value(QStringLiteral("takeId")).toString());
            if (fresh) {
                QVERIFY2(start >= 2256 && end <= 4256, qPrintable(QStringLiteral("%1-%2").arg(start).arg(end)));
                punchedIn += end - start;
            }
        }
        QCOMPARE(punchedIn, qint64(2000));
        QCOMPARE(check("punched.wav"), 0);

        // Comp playback with equal-power crossfades on the device.
        QVERIFY(comps->setFade(compId, QStringLiteral("240"), QStringLiteral("equalPower")).value(QStringLiteral("ok")).toBool());
        rig.transport->requestPunch(false, QStringLiteral("3000"), QStringLiteral("5000"));
        rig.transport->requestSeek(QStringLiteral("744"));
        rig.transport->requestPlay();
        QTRY_VERIFY(rig.transport->playing());
        QTest::qWait(150);
        g_deviceAllocations.store(0);
        g_probing.store(true);
        QTest::qWait(250);
        g_probing.store(false);
        rig.engine.stop();
        QTRY_COMPARE(rig.engine.stateName(), QStringLiteral("stopped"));
#if defined(QINDATK_ALLOCATION_PROBE)
        QCOMPARE(recordingAllocations, 0);
        QCOMPARE(g_deviceAllocations.load(), 0);
#else
        Q_UNUSED(recordingAllocations);
#endif
    }
};

QTEST_MAIN(TestAudioTakeCompEngine)
#include "tst_audio_take_comp_engine.moc"
