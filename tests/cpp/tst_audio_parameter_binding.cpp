// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-061: Knob/Fader gestures as engine parameter transactions. Every check
// renders the AudioRenderGraph the engine would play (the device and offline
// render share it, docs/audio-engine.md), so "the engine hears it" is exact.
#include "audio_parameter_binding.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <memory>
#include <vector>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate;

qint64 ticks(qint64 samples) { return samples * TicksPerSample; }

// What the graph plays for the constant 0.5 clip at `db` of track gain.
float gainOf(double db)
{
    return float(double(0.5f) * std::pow(10.0, db / 20.0));
}

struct Fixture {
    QTemporaryDir dir;
    AudioSession session;
    QString track;
    QString gain;
    AudioParameterBinding binding;
    std::vector<double> scratch = std::vector<double>(AudioRenderGraph::kMaxBlock);

    Fixture()
    {
        track = session.addTrack(QStringLiteral("Bus"));
        AudioBuffer constant; // one second of 0.5: the rendered sample is 0.5 x the track gain
        constant.sampleRate = Rate;
        constant.channels = 1;
        constant.frames = Rate;
        constant.samples.assign(std::size_t(Rate), 0.5f);
        const QString file = dir.filePath(QStringLiteral("constant.wav"));
        if (AudioFile::writeWav(file, constant)) session.importMedia(file, track);
        gain = session.ensureGainParameter(track);
        binding.setSession(&session);
        binding.setParameterId(gain);
    }
    AutomationModel *automation() { return session.automation(); }
    // The sample the engine plays at `sample`.
    float rendered(qint64 sample = 0)
    {
        const AudioRenderGraphPtr graph = session.compileGraph(Rate);
        float out = 0.0f;
        graph->render(sample, 1, &out, 1, scratch.data());
        return out;
    }
    bool addPoint(qint64 sample, double value)
    {
        return automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                                        {QStringLiteral("parameterId"), gain},
                                        {QStringLiteral("ticks"), QString::number(ticks(sample))},
                                        {QStringLiteral("value"), value}}).value(QStringLiteral("ok")).toBool();
    }
};

} // namespace

class TestAudioParameterBinding : public QObject {
    Q_OBJECT
private slots:
    void describesTheEngineParameter()
    {
        Fixture f;
        QVERIFY(f.binding.valid());
        QCOMPARE(f.binding.unit(), QStringLiteral("dB"));
        QCOMPARE(f.binding.taper(), QStringLiteral("gainDb"));
        QVERIFY(f.binding.silenceFloor());
        QCOMPARE(f.binding.minimum(), -60.0);
        QCOMPARE(f.binding.maximum(), 12.0);
        QCOMPARE(f.binding.resetValue(), 0.0);
        QCOMPARE(f.binding.value(), 0.0);
        QVERIFY(!f.binding.silent());
        QCOMPARE(f.binding.valueText(), QStringLiteral("0.00 dB"));
        QCOMPARE(f.binding.automationMode(), QStringLiteral("read"));
        // The shared mapper: unity (0 dB) at 0.75 of the travel, silence at 0.
        QCOMPARE(f.binding.mapper()->positionFor(0.0), 0.75);
        QCOMPARE(f.binding.mapper()->positionFor(-60.0, true), 0.0);
        QCOMPARE(f.rendered(), 0.5f);
        // A range edit reaches the descriptor and the mapper.
        QSignalSpy described(&f.binding, &AudioParameterBinding::descriptorChanged);
        QVERIFY(f.automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("editParameter")},
                                           {QStringLiteral("parameterId"), f.gain},
                                           {QStringLiteral("minimum"), -90.0}}).value(QStringLiteral("ok")).toBool());
        QVERIFY(!described.isEmpty());
        QCOMPARE(f.binding.minimum(), -90.0);
        QCOMPARE(f.binding.mapper()->descriptor().value(QStringLiteral("minimum")).toDouble(), -90.0);
        AudioParameterBinding unbound;
        QVERIFY(!unbound.valid());
        QVERIFY(!unbound.begin(0.0));
        QVERIFY(!unbound.lastError().isEmpty());
    }

    void staticGestureIsOneUndoableTransaction()
    {
        Fixture f;
        QSignalSpy finished(&f.binding, &AudioParameterBinding::transactionFinished);
        QVERIFY(f.binding.begin(0.0));
        QVERIFY(f.binding.touched());
        QVERIFY(f.binding.gestureActive());
        QVERIFY(f.binding.preview(-3.0));
        QCOMPARE(f.rendered(), gainOf(-3.0)); // held: the engine plays the live value
        QCOMPARE(f.binding.value(), -3.0);
        QVERIFY(f.binding.preview(-6.0));
        QCOMPARE(f.rendered(), gainOf(-6.0));
        QCOMPARE(f.automation()->historyCount(), 0); // previews store nothing
        QVERIFY(f.binding.commit(-6.0));
        QCOMPARE(f.binding.lastResult(), QStringLiteral("static"));
        QCOMPARE(f.automation()->historyCount(), 1);
        QVERIFY(!f.binding.touched());
        QVERIFY(!f.binding.gestureActive());
        QCOMPARE(f.binding.staticValue(), -6.0);
        QCOMPARE(f.binding.value(), -6.0);
        QCOMPARE(f.rendered(), gainOf(-6.0));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toString(), QStringLiteral("static"));
        // Undo and redo change what the engine plays, not only the control.
        QVERIFY(f.automation()->undo());
        QCOMPARE(f.binding.value(), 0.0);
        QCOMPARE(f.rendered(), 0.5f);
        QVERIFY(f.automation()->redo());
        QCOMPARE(f.rendered(), gainOf(-6.0));
        // A gesture that ends where it began (a plain click) stores nothing.
        QVERIFY(f.binding.begin(-6.0));
        QVERIFY(f.binding.commit(-6.0));
        QCOMPARE(f.binding.lastResult(), QStringLiteral("unchanged"));
        QCOMPARE(f.automation()->historyCount(), 1);
        QVERIFY(f.automation()->canUndo());
        // A second begin continues the open transaction (double-click reset).
        QVERIFY(f.binding.begin(-6.0));
        QVERIFY(f.binding.begin(-1.0));
        QVERIFY(f.binding.commit(0.0));
        QCOMPARE(f.automation()->historyCount(), 2);
        QCOMPARE(f.rendered(), 0.5f);
    }

    void cancelRestoresAndStoresNothing()
    {
        Fixture f;
        QVERIFY(f.binding.begin(0.0));
        QVERIFY(f.binding.preview(-20.0));
        QCOMPARE(f.rendered(), gainOf(-20.0));
        QVERIFY(f.binding.cancel());
        QCOMPARE(f.binding.lastResult(), QStringLiteral("canceled"));
        QCOMPARE(f.binding.value(), 0.0);
        QCOMPARE(f.rendered(), 0.5f);
        QCOMPARE(f.automation()->historyCount(), 0);
        QVERIFY(!f.binding.touched());
        QVERIFY(!f.binding.cancel()); // nothing is open
    }

    void silenceIsExactNotTheFloorValue()
    {
        Fixture f;
        QVERIFY(f.binding.begin(0.0));
        QVERIFY(f.binding.commit(0.0, true));
        QCOMPARE(f.binding.staticValue(), -60.0); // stored as the range minimum (D-101)
        QVERIFY(f.binding.silent());
        QCOMPARE(f.binding.valueText(), QStringLiteral("−∞ dB"));
        QCOMPARE(f.rendered(), 0.0f); // exact silence, not 0.5 x 10^(-60/20)
        // Just above the floor is a finite attenuation.
        QVERIFY(f.binding.begin(-60.0, true));
        QVERIFY(f.binding.commit(-59.5));
        QVERIFY(!f.binding.silent());
        QCOMPARE(f.rendered(), gainOf(-59.5));
        // Save and reopen keep the static value, silence included.
        QVERIFY(f.binding.begin(-59.5));
        QVERIFY(f.binding.commit(-60.0, true));
        const QString file = f.dir.filePath(QStringLiteral("session.json"));
        QVERIFY2(f.session.save(file), qPrintable(f.session.lastError()));
        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        AudioParameterBinding again;
        again.setSession(&reopened);
        again.setParameterId(f.gain);
        QVERIFY(again.valid());
        QCOMPARE(again.staticValue(), -60.0);
        QVERIFY(again.silent());
    }

    void readModeCurveAndOffMode()
    {
        Fixture f;
        QVERIFY(f.addPoint(0, -12.0));
        QVERIFY(f.addPoint(48000, 0.0));
        QCOMPARE(f.automation()->historyCount(), 2);
        QVERIFY(f.binding.automated());
        QCOMPARE(f.binding.value(), -12.0); // the transport is at 0
        QCOMPARE(f.rendered(12000), gainOf(-9.0));
        // Read mode: a held control overrides the curve (D-100), release returns to it.
        QVERIFY(f.binding.begin(-12.0));
        QVERIFY(f.binding.preview(-3.0));
        QVERIFY(!f.binding.automated());
        QCOMPARE(f.rendered(12000), gainOf(-3.0));
        QVERIFY(f.binding.commit(-3.0));
        QCOMPARE(f.binding.lastResult(), QStringLiteral("live"));
        QCOMPARE(f.automation()->historyCount(), 2);
        QCOMPARE(f.rendered(12000), gainOf(-9.0));
        // Off: the static value plays over the curve, and gestures edit it.
        QVERIFY(f.binding.setAutomationMode(QStringLiteral("off")));
        QCOMPARE(f.binding.automationMode(), QStringLiteral("off"));
        QVERIFY(!f.binding.automated());
        QCOMPARE(f.rendered(12000), 0.5f);
        QVERIFY(f.binding.begin(0.0));
        QVERIFY(f.binding.commit(-6.0));
        QCOMPARE(f.binding.lastResult(), QStringLiteral("static"));
        QCOMPARE(f.rendered(12000), gainOf(-6.0));
        QVERIFY(f.binding.setAutomationMode(QStringLiteral("read")));
        QCOMPARE(f.rendered(12000), gainOf(-9.0));
        QVERIFY(!f.binding.setAutomationMode(QStringLiteral("sideways")));
        QVERIFY(!f.binding.lastError().isEmpty());
    }

    void touchAndLatchPassesAreTheTransaction()
    {
        Fixture f;
        AutomationModel *automation = f.automation();
        QVERIFY(f.binding.setAutomationMode(QStringLiteral("touch")));
        // Without a device the automation transport is the clock the binding stamps with.
        automation->transportStarted(0);
        QVERIFY(f.binding.begin(0.0));
        QVERIFY(f.binding.writing());
        QCOMPARE(f.binding.automationState(), QStringLiteral("touch, writing"));
        automation->transportMoved(ticks(4800));
        QVERIFY(f.binding.preview(-12.0));
        QCOMPARE(f.rendered(7200), gainOf(-12.0)); // written values are heard while held
        automation->transportMoved(ticks(9600));
        QVERIFY(f.binding.commit(-12.0));
        QCOMPARE(f.binding.lastResult(), QStringLiteral("recorded"));
        QVERIFY(!f.binding.writing());
        QCOMPARE(automation->historyCount(), 1); // the pass is the one undo step
        automation->transportStopped(ticks(9600));
        QCOMPARE(automation->historyCount(), 1);
        QCOMPARE(f.session.schedule()->acquire()->valueAtSample(f.gain, 7200).value, -12.0);
        QCOMPARE(f.rendered(7200), gainOf(-12.0));
        QVERIFY(automation->undo());
        QCOMPARE(f.rendered(7200), 0.5f);

        // A canceled touch discards the pass it opened.
        automation->transportStarted(ticks(19200));
        QVERIFY(f.binding.begin(0.0));
        QVERIFY(f.binding.writing());
        automation->transportMoved(ticks(24000));
        QVERIFY(f.binding.preview(-30.0));
        QVERIFY(f.binding.cancel());
        QVERIFY(!f.binding.writing());
        automation->transportStopped(ticks(28800));
        QVERIFY(!automation->canUndo());
        QVERIFY(automation->points(f.gain).isEmpty());

        // Latch keeps writing the released value until the transport stops.
        QVERIFY(f.binding.setAutomationMode(QStringLiteral("latch")));
        automation->transportStarted(ticks(48000));
        QVERIFY(f.binding.begin(0.0));
        automation->transportMoved(ticks(52800));
        QVERIFY(f.binding.preview(-6.0));
        QVERIFY(f.binding.commit(-6.0));
        QCOMPARE(f.binding.lastResult(), QStringLiteral("recorded"));
        QVERIFY(f.binding.writing());
        QCOMPARE(f.rendered(0), gainOf(-6.0)); // the latched value plays everywhere while writing
        automation->transportStopped(ticks(57600));
        QVERIFY(!f.binding.writing());
        QVERIFY(automation->canUndo());
    }

    void destroyedBindingReleasesItsGesture()
    {
        Fixture f;
        auto temporary = std::make_unique<AudioParameterBinding>();
        temporary->setSession(&f.session);
        temporary->setParameterId(f.gain);
        QVERIFY(temporary->begin(0.0));
        QVERIFY(temporary->preview(-24.0));
        QCOMPARE(f.rendered(), gainOf(-24.0));
        temporary.reset();
        QVERIFY(!f.automation()->lane(f.gain)->touched);
        QCOMPARE(f.rendered(), 0.5f);
        QCOMPARE(f.automation()->historyCount(), 0);
        // A binding whose session goes away becomes invalid.
        auto session = std::make_unique<AudioSession>();
        AudioParameterBinding orphan;
        orphan.setSession(session.get());
        orphan.setParameterId(session->ensureGainParameter(session->addTrack(QStringLiteral("Gone"))));
        QVERIFY(orphan.valid());
        session.reset();
        QVERIFY(!orphan.valid());
    }
};

QTEST_GUILESS_MAIN(TestAudioParameterBinding)
#include "tst_audio_parameter_binding.moc"
