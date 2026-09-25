// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-066 plugin chains with real CLAP plugins (tests/plugins): reordering
// within a channel and to another channel changes the order the render graph
// processes the inserts in, and keeps the instance (process, parameter IDs,
// automation lanes, state) and survives save and reopen; the rack protocol
// (AudioPluginHost::rackAction) drives every host operation; a plugin
// parameter's automation curve reaches the plugin at every block start.
#include "audio_parameter_binding.h"
#include "audio_plugin_editor.h"
#include "audio_session.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWindow>
#include <QtTest>
#include <cstring>
#include <functional>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate;
const QString Gain = QStringLiteral("org.qindatk.test.gain");
const QString Synth = QStringLiteral("org.qindatk.test.synth");

bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }
float ramp(qint64 frame) { return float(frame + 1) / 65536.0f; } // exact in float, never 0

// The test plugins' state: "QTKS", a value count, then the values.
double stateValue(const QByteArray &state, int index)
{
    if (state.size() < 8 + 8 * (index + 1) || !state.startsWith("QTKS")) return -1.0;
    double value = 0.0;
    std::memcpy(&value, state.constData() + 8 + 8 * index, sizeof value);
    return value;
}

struct Rig {
    QTemporaryDir dir;
    AudioSession session;
    AudioPluginHost *plugins = session.plugins();
    Rig()
    {
        plugins->registry()->setCacheFile(dir.filePath(QStringLiteral("cache.json")));
        plugins->registry()->setSearchPaths({QFileInfo(QStringLiteral(QINDATK_TEST_CLAP)).absolutePath()});
        plugins->registry()->scanNow();
        session.setMediaDirectory(dir.filePath(QStringLiteral("takes")));
    }
    QString track(const QString &name, qint64 frames)
    {
        const QString id = session.addTrack(name);
        AudioBuffer buffer;
        buffer.sampleRate = Rate;
        buffer.channels = 1;
        buffer.frames = frames;
        buffer.samples.resize(std::size_t(frames));
        for (qint64 i = 0; i < frames; ++i) buffer.samples[std::size_t(i)] = ramp(i);
        const QString file = dir.filePath(name + QStringLiteral(".wav"));
        if (id.isEmpty() || !AudioFile::writeWav(file, buffer) || !ok(session.importMedia(file, id))) return {};
        return QStringLiteral("track:") + id;
    }
    // A MIDI track with one full-velocity note on samples [12000, 24000).
    QString noteTrack()
    {
        const QString track = session.addMidiTrack(QStringLiteral("Synth"));
        MidiSequenceModel *midi = session.midi();
        const QString pattern = midi->addPattern(QStringLiteral("P"), QStringLiteral("7680")).value(QStringLiteral("patternId")).toString();
        midi->insertNote(pattern, {{QStringLiteral("tick"), QStringLiteral("480")}, {QStringLiteral("duration"), QStringLiteral("480")},
                                   {QStringLiteral("pitch"), 60}, {QStringLiteral("velocity16"), 65535}});
        if (!ok(session.insertPattern(track, pattern))) return {};
        return AudioMixer::trackChannelId(QUuid::fromString(track));
    }
    QString add(const QString &channel, const QString &pluginId)
    {
        const QVariantMap result = plugins->addPlugin(channel, pluginId);
        if (!ok(result)) qWarning("add %s: %s", qPrintable(pluginId), qPrintable(result.value(QStringLiteral("error")).toString()));
        return result.value(QStringLiteral("instanceId")).toString();
    }
    bool set(const QString &instance, int parameter, double value)
    {
        AudioParameterBinding *binding = plugins->parameterBinding(instance, parameter);
        return binding && binding->begin(value) && binding->commit(value);
    }
    bool point(const QString &parameterId, qint64 sample, double value)
    {
        return ok(session.automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                                                   {QStringLiteral("parameterId"), parameterId},
                                                   {QStringLiteral("ticks"), QString::number(sample * TicksPerSample)},
                                                   {QStringLiteral("value"), value}}));
    }
    AudioBufferPtr render(const QString &name, qint64 frames)
    {
        const QString file = dir.filePath(name);
        const QVariantMap result = session.renderToFile(file, QStringLiteral("0"), QString::number(frames * TicksPerSample));
        if (!ok(result)) qWarning("render: %s", qPrintable(result.value(QStringLiteral("error")).toString()));
        return ok(result) ? AudioFile::readWav(file) : nullptr;
    }
    QVariantMap row(const QString &instance) { return plugins->instance(instance); }
    QStringList parameterIds(const QString &channel, const QString &instance)
    {
        QStringList ids;
        for (const QVariant &value : plugins->instances(channel)) {
            const QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("instanceId")).toString() != instance) continue;
            for (const QVariant &parameter : entry.value(QStringLiteral("parameters")).toList())
                ids.push_back(parameter.toMap().value(QStringLiteral("parameterId")).toString());
        }
        return ids;
    }
};

// Every sample of both lanes equals `expected(frame)`, exactly.
bool matches(const AudioBufferPtr &out, qint64 frames, const std::function<float(qint64)> &expected)
{
    if (!out || out->frames != frames || out->channels != 2) return false;
    for (qint64 i = 0; i < frames; ++i) {
        const float want = expected(i);
        if (out->channel(0)[i] != want || out->channel(1)[i] != want) {
            qWarning("frame %lld: %.9g / %.9g, expected %.9g", i, double(out->channel(0)[i]), double(out->channel(1)[i]),
                     double(want));
            return false;
        }
    }
    return true;
}

bool inNote(qint64 frame) { return frame >= 12000 && frame < 24000; }

} // namespace

class TestAudioPluginChain : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qputenv("QINDATK_PLUGIN_HOST", QINDATK_TEST_PLUGIN_HOST); }

    // The instrument replaces its input; Gain scales it. [Synth, Gain 0.5]
    // plays the note at 0.125, [Gain, Synth] at 0.25, so the rendered samples
    // show the order the graph runs the inserts in.
    void reorderChangesProcessingAndKeepsTheInstance()
    {
        QTemporaryDir saved;
        const QString file = saved.filePath(QStringLiteral("session.json"));
        constexpr qint64 Frames = 36000;
        QString midi, audio, synth, gain;
        QVariantList gainPoints;
        AudioBufferPtr beforeSave;
        {
            Rig rig;
            audio = rig.track(QStringLiteral("A"), Frames);
            midi = rig.noteTrack();
            QVERIFY(!audio.isEmpty() && !midi.isEmpty());
            synth = rig.add(midi, Synth);
            gain = rig.add(midi, Gain);
            QVERIFY(!synth.isEmpty() && !gain.isEmpty());
            QCOMPARE(rig.plugins->chain(midi), QStringList({synth, gain}));
            // Gain 0.5 as an automation lane with breakpoints: it must move with the instance.
            const QString gainParameter = rig.plugins->parameterId(gain, 1);
            QVERIFY(rig.point(gainParameter, 0, 0.5) && rig.point(gainParameter, Frames, 0.5));
            gainPoints = rig.session.automation()->points(gainParameter);
            QCOMPARE(gainPoints.size(), 2);
            const QStringList parameterIds = rig.parameterIds(midi, gain);
            QCOMPARE(parameterIds, QStringList({gain + QStringLiteral("/1"), gain + QStringLiteral("/2")}));
            const qint64 pid = rig.row(gain).value(QStringLiteral("processId")).toLongLong();
            AudioPluginInstance *instance = rig.plugins->instanceObject(gain);
            QVERIFY(pid > 0 && instance);

            QVERIFY(matches(rig.render(QStringLiteral("synth-gain.wav"), Frames), Frames,
                            [](qint64 i) { return (inNote(i) ? 0.125f : 0.0f) + ramp(i); }));

            // Within the channel: Gain first.
            QSignalSpy recompiled(rig.plugins, &AudioPluginHost::contentChanged);
            QVERIFY(ok(rig.plugins->movePlugin(gain, 0)));
            QCOMPARE(recompiled.size(), 1);
            QCOMPARE(rig.plugins->chain(midi), QStringList({gain, synth}));
            QVERIFY(matches(rig.render(QStringLiteral("gain-synth.wav"), Frames), Frames,
                            [](qint64 i) { return (inNote(i) ? 0.25f : 0.0f) + ramp(i); }));

            // To another channel: Gain now halves track A; the synth plays alone.
            QVERIFY(ok(rig.plugins->movePlugin(gain, 0, audio)));
            QCOMPARE(rig.plugins->chain(midi), QStringList({synth}));
            QCOMPARE(rig.plugins->chain(audio), QStringList({gain}));
            QCOMPARE(rig.row(gain).value(QStringLiteral("channelId")).toString(), audio);
            QVERIFY(matches(rig.render(QStringLiteral("moved.wav"), Frames), Frames,
                            [](qint64 i) { return (inNote(i) ? 0.25f : 0.0f) + ramp(i) * 0.5f; }));

            // A refused move changes nothing.
            QVERIFY(!ok(rig.plugins->movePlugin(gain, 0, QStringLiteral("track:not-a-channel"))));
            QVERIFY(!ok(rig.plugins->movePlugin(QStringLiteral("plugin:missing"), 0)));
            QCOMPARE(rig.plugins->chain(audio), QStringList({gain}));

            // The same instance throughout: process, parameter IDs, lanes, state.
            QCOMPARE(rig.plugins->instanceObject(gain), instance);
            QCOMPARE(rig.row(gain).value(QStringLiteral("processId")).toLongLong(), pid);
            QCOMPARE(rig.parameterIds(audio, gain), parameterIds);
            QCOMPARE(rig.session.automation()->points(gainParameter), gainPoints);
            QVERIFY(rig.plugins->refreshStates());
            QCOMPARE(stateValue(instance->savedState(), 0), 0.5); // Gain, as the plugin saves it

            // Back in front of the synth, then saved.
            QVERIFY(ok(rig.plugins->movePlugin(gain, 0, midi)));
            QCOMPARE(rig.plugins->chain(midi), QStringList({gain, synth}));
            QVERIFY(rig.plugins->chain(audio).isEmpty());
            beforeSave = rig.render(QStringLiteral("before-save.wav"), Frames);
            QVERIFY(matches(beforeSave, Frames, [](qint64 i) { return (inNote(i) ? 0.25f : 0.0f) + ramp(i); }));
            QVERIFY(rig.session.save(file));

            // Reopened: the saved order, the same instance and parameter IDs,
            // the lanes, and the same samples. Kept inside this scope because
            // the saved session refers to this rig's media.
            Rig reopened;
            QVERIFY2(reopened.session.open(file), qPrintable(reopened.session.lastError()));
            QCOMPARE(reopened.plugins->chain(midi), QStringList({gain, synth}));
            QCOMPARE(reopened.row(gain).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
            QCOMPARE(reopened.parameterIds(midi, gain), parameterIds);
            QCOMPARE(reopened.session.automation()->points(gainParameter), gainPoints);
            QVERIFY(matches(reopened.render(QStringLiteral("reopened.wav"), Frames), Frames,
                            [&](qint64 i) { return beforeSave->channel(0)[i]; }));
            // And it still reorders after the reopen.
            QVERIFY(ok(reopened.plugins->movePlugin(gain, 1)));
            QCOMPARE(reopened.plugins->chain(midi), QStringList({synth, gain}));
            QVERIFY(matches(reopened.render(QStringLiteral("reopened-moved.wav"), Frames), Frames,
                            [](qint64 i) { return (inNote(i) ? 0.125f : 0.0f) + ramp(i); }));
        }
    }

    // D-191: a plugin parameter is an automation parameter. Its curve reaches
    // the plugin at every block start (every 256 frames): Gain rises from 0
    // to 2 over 8192 frames and falls back to 1 by frame 12288.
    void automationCurveDrivesThePlugin()
    {
        Rig rig;
        constexpr qint64 Frames = 14336;
        const QString channel = rig.track(QStringLiteral("A"), Frames);
        const QString gain = rig.add(channel, Gain);
        QVERIFY(!gain.isEmpty());
        const QString parameter = rig.plugins->parameterId(gain, 1);
        QVERIFY(rig.point(parameter, 0, 0.0) && rig.point(parameter, 8192, 2.0) && rig.point(parameter, 12288, 1.0));
        const auto curve = [](qint64 sample) {
            if (sample <= 8192) return 2.0 * double(sample) / 8192.0;
            if (sample <= 12288) return 2.0 - double(sample - 8192) / 4096.0;
            return 1.0;
        };
        const AudioBufferPtr out = rig.render(QStringLiteral("automated.wav"), Frames);
        QVERIFY(matches(out, Frames, [&](qint64 i) {
            const qint64 blockStart = i - i % AudioRenderGraph::kChunk; // the value sent with this block
            return float(curve(blockStart) * double(ramp(i)));
        }));
        // It follows the curve, not one flat value: the gain changes at 48
        // block starts (32 rising, 16 falling) and holds after the last point.
        int changes = 0;
        float previous = 0.0f;
        for (qint64 i = 0; i < Frames; i += AudioRenderGraph::kChunk) {
            const float value = out->channel(0)[i] / ramp(i);
            if (value != previous) ++changes;
            previous = value;
        }
        QCOMPARE(changes, 48);
    }

    // PluginRack/PluginBrowser requests reach the host through rackAction().
    void rackActionDrivesTheHost()
    {
        Rig rig;
        const QString channel = rig.track(QStringLiteral("A"), 4800);
        const auto accepted = [](const QVariantMap &answer) {
            return answer.value(QStringLiteral("state")).toString() == QLatin1String("accepted");
        };
        const auto rejected = [](const QVariantMap &answer) {
            return answer.value(QStringLiteral("state")).toString() == QLatin1String("rejected")
                   && !answer.value(QStringLiteral("message")).toString().isEmpty();
        };
        // add
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("add"), {}, {{QStringLiteral("channelId"), QStringLiteral("track:none")},
                                                                               {QStringLiteral("pluginId"), Gain}})));
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("add"), {}, {{QStringLiteral("channelId"), channel},
                                                                               {QStringLiteral("pluginId"), QStringLiteral("org.example.none")}})));
        QVariantMap answer = rig.plugins->rackAction(QStringLiteral("add"), {}, {{QStringLiteral("channelId"), channel},
                                                                                  {QStringLiteral("pluginId"), Gain}});
        QVERIFY(accepted(answer));
        const QString first = answer.value(QStringLiteral("result")).toMap().value(QStringLiteral("instanceId")).toString();
        answer = rig.plugins->rackAction(QStringLiteral("add"), {}, {{QStringLiteral("channelId"), channel},
                                                                     {QStringLiteral("pluginId"), Gain},
                                                                     {QStringLiteral("index"), 0}});
        QVERIFY(accepted(answer));
        const QString second = answer.value(QStringLiteral("result")).toMap().value(QStringLiteral("instanceId")).toString();
        QCOMPARE(rig.plugins->chain(channel), QStringList({second, first})); // `index` places it
        // reorder, within the channel and to the master
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("reorder"), second, {{QStringLiteral("targetIndex"), 1}})));
        QCOMPARE(rig.plugins->chain(channel), QStringList({first, second}));
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("reorder"), second, {{QStringLiteral("targetIndex"), 0},
                                                                                     {QStringLiteral("channelId"), QStringLiteral("master")}})));
        QCOMPARE(rig.plugins->chain(channel), QStringList({first}));
        QCOMPARE(rig.plugins->chain(QStringLiteral("master")), QStringList({second}));
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("reorder"), QStringLiteral("plugin:none"), {{QStringLiteral("targetIndex"), 0}})));
        // bypass
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("bypass"), first, {{QStringLiteral("bypassed"), true}})));
        QCOMPARE(rig.row(first).value(QStringLiteral("state")).toString(), QStringLiteral("bypassed"));
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("bypass"), first, {{QStringLiteral("bypassed"), false}})));
        QCOMPARE(rig.row(first).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        // edit-parameter: a number, a typed display value, and what is refused
        AudioParameterBinding *binding = rig.plugins->parameterBinding(first, 1);
        QVERIFY(binding);
        const QString gainId = rig.plugins->parameterId(first, 1);
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("edit-parameter"), first, {{QStringLiteral("parameterId"), gainId},
                                                                                           {QStringLiteral("value"), 0.5}})));
        QCOMPARE(binding->value(), 0.5);
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("edit-parameter"), first, {{QStringLiteral("parameterId"), gainId},
                                                                                           {QStringLiteral("displayValue"), QStringLiteral(" 1.5 ")}})));
        QCOMPARE(binding->value(), 1.5);
        QCOMPARE(rig.session.automation()->lane(gainId)->parameter.defaultValue, 1.5);
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("edit-parameter"), first, {{QStringLiteral("parameterId"), gainId},
                                                                                           {QStringLiteral("displayValue"), QStringLiteral("loud")}})));
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("edit-parameter"), first,
                                                 {{QStringLiteral("parameterId"), rig.plugins->parameterId(second, 1)}, {QStringLiteral("value"), 0.1}})));
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("edit-parameter"), first,
                                                 {{QStringLiteral("parameterId"), first + QStringLiteral("/x")}, {QStringLiteral("value"), 0.1}})));
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("edit-parameter"), first,
                                                 {{QStringLiteral("parameterId"), rig.plugins->parameterId(first, 3)}, {QStringLiteral("value"), 1.0}})));
        QCOMPARE(binding->value(), 1.5); // nothing refused reached the parameter
        // The edited value is what the plugin processes, and what it saves.
        QVERIFY(matches(rig.render(QStringLiteral("edited.wav"), 4800), 4800, [](qint64 i) { return ramp(i) * 1.5f; }));
        QTRY_COMPARE((rig.plugins->refreshStates(), stateValue(rig.plugins->instanceObject(first)->savedState(), 0)), 1.5);
        // open-editor / close-editor with the requesting window as owner
        QWindow owner;
        owner.create();
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("open-editor"), first, {}, &owner)));
        QVERIFY(rig.row(first).value(QStringLiteral("editorOpen")).toBool());
        QCOMPARE(rig.plugins->instanceObject(first)->editorWindow()->transientParent(), &owner);
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("close-editor"), first, {})));
        QTRY_VERIFY(!rig.row(first).value(QStringLiteral("editorOpen")).toBool());
        // reload: a live instance is left alone; a killed one comes back
        const qint64 pid = rig.row(first).value(QStringLiteral("processId")).toLongLong();
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("reload"), first, {})));
        QCOMPARE(rig.row(first).value(QStringLiteral("processId")).toLongLong(), pid);
        rig.plugins->instanceObject(first)->fail(QStringLiteral("test failure"));
        QCOMPARE(rig.row(first).value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("reload"), first, {})));
        QCOMPARE(rig.row(first).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QVERIFY(rig.row(first).value(QStringLiteral("processId")).toLongLong() != pid);
        // remove, and what is not an action
        QVERIFY(accepted(rig.plugins->rackAction(QStringLiteral("remove"), second, {})));
        QVERIFY(rig.plugins->chain(QStringLiteral("master")).isEmpty());
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("remove"), second, {})));
        QVERIFY(rejected(rig.plugins->rackAction(QStringLiteral("explode"), first, {})));
        QCOMPARE(rig.plugins->instanceIds(), QStringList({first}));
    }
};

QTEST_MAIN(TestAudioPluginChain)
#include "tst_audio_plugin_chain.moc"
