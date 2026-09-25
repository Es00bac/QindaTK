// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-066 plugin hosting with real CLAP binaries (tests/plugins) run by
// qindatk-plugin-host: out-of-process scanning and its cache, a crashing and
// an unloadable binary, the instance lifecycle, parameters as automation,
// state save and restore, latency restarts, request time-outs, a crash that
// is contained and reloaded, the editor lifecycle, session recall of an
// unavailable plugin, and unload at shutdown.
#include "audio_parameter_binding.h"
#include "audio_plugin_editor.h"
#include "audio_session.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWindow>
#include <QtTest>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstring>
#include <limits>

using namespace QindaTK;

namespace {

const QString Gain = QStringLiteral("org.qindatk.test.gain");
const QString Synth = QStringLiteral("org.qindatk.test.synth");
const QString Faulty = QStringLiteral("org.qindatk.test.faulty");
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / 48000;

bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }
QString fixtures() { return QFileInfo(QStringLiteral(QINDATK_TEST_CLAP)).absolutePath(); }
bool alive(qint64 pid) { return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno != ESRCH); }

QString logPath()
{
    return qEnvironmentVariable("QINDATK_TEST_PLUGIN_LOG");
}

QStringList logLines()
{
    QFile file(logPath());
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// The test plugins' state: "QTKS", a value count, then the values.
double stateValue(const QByteArray &state, int index)
{
    if (state.size() < 8 + 8 * (index + 1) || !state.startsWith("QTKS")) return std::numeric_limits<double>::quiet_NaN();
    double value = 0.0;
    std::memcpy(&value, state.constData() + 8 + 8 * index, sizeof value);
    return value;
}

void writeRamp(const QString &file, qint64 frames)
{
    AudioBuffer buffer;
    buffer.sampleRate = 48000;
    buffer.channels = 1;
    buffer.frames = frames;
    buffer.samples.resize(std::size_t(frames));
    for (qint64 i = 0; i < frames; ++i) buffer.samples[std::size_t(i)] = float(i + 1) / 65536.0f;
    QVERIFY(AudioFile::writeWav(file, buffer));
}

struct Rig {
    QTemporaryDir dir;
    AudioSession session;
    AudioPluginHost *plugins = session.plugins();
    explicit Rig(bool installed = true)
    {
        plugins->registry()->setCacheFile(dir.filePath(QStringLiteral("cache.json")));
        plugins->registry()->setSearchPaths({installed ? fixtures() : dir.filePath(QStringLiteral("nothing-here"))});
        plugins->registry()->scanNow();
        QFile::remove(logPath()); // the scan creates every plugin once; tests count their own calls
    }
    QString track(bool media = false)
    {
        const QString id = session.addTrack(QStringLiteral("A"));
        if (media) {
            const QString file = dir.filePath(QStringLiteral("ramp.wav"));
            writeRamp(file, 4800);
            session.importMedia(file, id);
        }
        return QStringLiteral("track:") + id;
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
    QString state(const QString &instance) { return plugins->instance(instance).value(QStringLiteral("state")).toString(); }
    qint64 pid(const QString &instance) { return plugins->instance(instance).value(QStringLiteral("processId")).toLongLong(); }
};

} // namespace

class TestAudioPluginHost : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_logs;

private slots:
    void initTestCase()
    {
        qputenv("QINDATK_PLUGIN_HOST", QINDATK_TEST_PLUGIN_HOST);
        qputenv("QINDATK_TEST_PLUGIN_LOG", m_logs.filePath(QStringLiteral("plugins.log")).toUtf8());
        QVERIFY(QFileInfo(QStringLiteral(QINDATK_TEST_PLUGIN_HOST)).isExecutable());
    }
    void init() { QFile::remove(logPath()); }

    void scanDescribesPluginsAndCachesThem()
    {
        Rig rig;
        AudioPluginRegistry *registry = rig.plugins->registry();
        QCOMPARE(registry->scanState(), QStringLiteral("ready"));
        const AudioPluginDescriptor *gain = registry->descriptor(Gain);
        QVERIFY(gain);
        QCOMPARE(gain->name, QStringLiteral("Test Gain"));
        QCOMPARE(gain->kind(), QStringLiteral("Effect"));
        QCOMPARE(gain->category(), QStringLiteral("Utility"));
        QCOMPARE(gain->audioInputs, 2);
        QCOMPARE(gain->audioOutputs, 2);
        QCOMPARE(gain->parameterCount, 3);
        QVERIFY(gain->state);
        QVERIFY(gain->editors.contains(QStringLiteral("x11")));
        QCOMPARE(gain->path, QFileInfo(QStringLiteral(QINDATK_TEST_CLAP)).canonicalFilePath());
        const AudioPluginDescriptor *synth = registry->descriptor(Synth);
        QVERIFY(synth);
        QCOMPARE(synth->kind(), QStringLiteral("Instrument"));
        QCOMPARE(synth->audioInputs, 0);
        QCOMPARE(synth->noteInputs, 1);
        // A plugin that refuses to initialise stays listed, unavailable, with its reason.
        const AudioPluginDescriptor *broken = registry->descriptor(QStringLiteral("org.qindatk.test.init-fails"));
        QVERIFY(broken && !broken->available);
        QVERIFY(broken->reason.contains(QStringLiteral("initialise")));
        QCOMPARE(registry->binaries().size(), 1);
        QVERIFY(!registry->binaries().first().toMap().value(QStringLiteral("cached")).toBool());

        // An unchanged binary is not scanned again: the cache answers.
        AudioPluginRegistry again;
        again.setCacheFile(rig.dir.filePath(QStringLiteral("cache.json")));
        again.setSearchPaths({fixtures()});
        QVERIFY(again.scanNow());
        QVERIFY(again.binaries().first().toMap().value(QStringLiteral("cached")).toBool());
        QVERIFY(again.descriptor(Gain));
        QVERIFY(again.scanNow(true)); // forced: scanned again
        QVERIFY(!again.binaries().first().toMap().value(QStringLiteral("cached")).toBool());

        // The capability declaration tracks every format on its own.
        const QVariantList formats = rig.plugins->formats();
        QCOMPARE(formats.size(), 3);
        QCOMPARE(formats.at(0).toMap().value(QStringLiteral("format")).toString(), QStringLiteral("CLAP"));
        QVERIFY(formats.at(0).toMap().value(QStringLiteral("available")).toBool());
        QCOMPARE(formats.at(0).toMap().value(QStringLiteral("isolation")).toString(), QStringLiteral("process"));
        QVERIFY(!formats.at(1).toMap().value(QStringLiteral("available")).toBool()); // LV2
        QVERIFY(!formats.at(2).toMap().value(QStringLiteral("available")).toBool()); // VST3
    }

    void scanSurvivesBrokenBinaries()
    {
        QTemporaryDir dir;
        const QString junk = dir.filePath(QStringLiteral("junk.clap"));
        QFile file(junk);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a shared object");
        file.close();
        AudioPluginRegistry registry;
        registry.setCacheFile(dir.filePath(QStringLiteral("cache.json")));
        registry.setSearchPaths({QStringLiteral(QINDATK_TEST_BROKEN_CLAP), junk});
        QVERIFY(registry.scanNow());
        const QVariantList binaries = registry.binaries();
        QCOMPARE(binaries.size(), 2);
        QCOMPARE(binaries.at(0).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("crashed"));
        QCOMPARE(binaries.at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        QVERIFY(binaries.at(1).toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("cannot be loaded")));
        // Both stay visible in the browser rows, unavailable, with the reason.
        const QVariantList rows = registry.plugins();
        QCOMPARE(rows.size(), 2);
        for (const QVariant &row : rows) {
            QVERIFY(!row.toMap().value(QStringLiteral("available")).toBool());
            QVERIFY(!row.toMap().value(QStringLiteral("reason")).toString().isEmpty());
        }
    }

    void lifecycleParametersAndState()
    {
        Rig rig;
        const QString channel = rig.track();
        const QString id = rig.add(channel, Gain);
        QVERIFY(AudioPlugins::isInstanceId(id));
        QCOMPARE(rig.plugins->chain(channel), QStringList{id});
        QVariantMap row = rig.plugins->instance(id);
        QCOMPARE(row.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(row.value(QStringLiteral("latencyFrames")).toInt(), 0);
        QCOMPARE(row.value(QStringLiteral("sampleRate")).toInt(), 48000);
        QVERIFY(row.value(QStringLiteral("editorAvailable")).toBool());
        const qint64 pid = rig.pid(id);
        QVERIFY(pid > 0 && pid != QCoreApplication::applicationPid());
        QVERIFY(alive(pid));

        // Gain and Latency are automation parameters; read-only Blocks is not.
        const QVariantList parameters = rig.plugins->instances(channel).first().toMap().value(QStringLiteral("parameters")).toList();
        QCOMPARE(parameters.size(), 2);
        QCOMPARE(parameters.at(0).toMap().value(QStringLiteral("parameterId")).toString(), id + QStringLiteral("/1"));
        QVERIFY(parameters.at(0).toMap().value(QStringLiteral("binding")).value<QObject *>());
        const AutomationModel::Lane *lane = rig.session.automation()->lane(id + QStringLiteral("/1"));
        QVERIFY(lane);
        QCOMPARE(lane->parameter.range.minimum, 0.0);
        QCOMPARE(lane->parameter.range.maximum, 2.0);
        QCOMPARE(lane->parameter.defaultValue, 1.0);
        QVERIFY(!rig.session.automation()->lane(id + QStringLiteral("/3")));
        QCOMPARE(rig.session.automation()->lane(id + QStringLiteral("/2"))->parameter.step, 1.0);

        // A knob edit reaches the plugin while nothing plays, and its state.
        QVERIFY(rig.set(id, 1, 0.5));
        AudioPluginInstance *instance = rig.plugins->instanceObject(id);
        QTRY_COMPARE((rig.plugins->refreshStates(), stateValue(instance->savedState(), 0)), 0.5);

        // Removing unloads the plugin and ends its process.
        QVERIFY(ok(rig.plugins->removePlugin(id)));
        QVERIFY(rig.plugins->chain(channel).isEmpty());
        QTRY_VERIFY(!alive(pid));
        QVERIFY(logLines().contains(QStringLiteral("destroy org.qindatk.test.gain")));
        QVERIFY(!ok(rig.plugins->addPlugin(QStringLiteral("track:not-a-track"), Gain)));
    }

    void latencyChangeRestartsThePlugin()
    {
        Rig rig;
        const QString channel = rig.track();
        const QString id = rig.add(channel, Gain);
        QVERIFY(!id.isEmpty());
        QVERIFY(rig.set(id, 2, 64));
        // The plugin asks for a restart; the host reactivates it and the
        // graph compensates the new latency.
        QTRY_COMPARE(rig.plugins->instance(id).value(QStringLiteral("latencyFrames")).toInt(), 64);
        QCOMPARE(rig.session.compileGraph(48000)->insertLatency(channel), 64);
        QCOMPARE(logLines().count(QStringLiteral("activate 48000")), 2);
    }

    void failuresToLoadLeaveNothingBehind()
    {
        Rig rig;
        const QString channel = rig.track();
        QVariantMap result = rig.plugins->addPlugin(channel, QStringLiteral("org.qindatk.test.init-fails"));
        QVERIFY(!ok(result));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("initialise")));
        rig.plugins->setRequestTimeoutMs(500);
        QElapsedTimer timer;
        timer.start();
        result = rig.plugins->addPlugin(channel, QStringLiteral("org.qindatk.test.slow")); // activate() takes 30 s
        QVERIFY(!ok(result));
        QVERIFY2(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("did not answer")),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(timer.elapsed() < 10000);
        QVERIFY(rig.plugins->chain(channel).isEmpty());
        QVERIFY(rig.plugins->instanceIds().isEmpty());
    }

    void crashIsContainedAndReloaded()
    {
        Rig rig;
        rig.plugins->setOfflineTimeoutMs(2000);
        const QString channel = rig.track(true);
        const QString id = rig.add(channel, Faulty);
        QVERIFY(!id.isEmpty());
        const qint64 pid = rig.pid(id);
        QSignalSpy failed(rig.plugins, &AudioPluginHost::instanceFailed);
        QVERIFY(rig.set(id, 1, 1.0)); // Crash: the next process() raises SIGSEGV
        const QString out = rig.dir.filePath(QStringLiteral("crash.wav"));
        const QVariantMap render = rig.session.renderToFile(out, QStringLiteral("0"), QString::number(4800 * TicksPerSample));
        QVERIFY2(ok(render), qPrintable(render.value(QStringLiteral("error")).toString()));
        QTRY_COMPARE(rig.state(id), QStringLiteral("failed"));
        QVERIFY(rig.plugins->instance(id).value(QStringLiteral("error")).toString().contains(QStringLiteral("crashed")));
        QCOMPARE(failed.size(), 1);
        QVERIFY(!alive(pid));
        QVERIFY(logLines().contains(QStringLiteral("faulty.crash")));
        // The session rendered on: the failed insert passed the signal through.
        const AudioBufferPtr data = AudioFile::readWav(out);
        QVERIFY(data && data->frames == 4800);
        for (qint64 i = 0; i < data->frames; ++i) QCOMPARE(data->channel(0)[i], float(i + 1) / 65536.0f);

        // Recovery: the same instance starts again with its saved state.
        QVERIFY(rig.set(id, 1, 0.0));
        QVERIFY(ok(rig.plugins->reload(id)));
        QCOMPARE(rig.state(id), QStringLiteral("ready"));
        QVERIFY(alive(rig.pid(id)));
        QVERIFY(ok(rig.session.renderToFile(out, QStringLiteral("0"), QString::number(4800 * TicksPerSample))));
        QCOMPARE(rig.state(id), QStringLiteral("ready"));
    }

    void hangInAnOfflineRenderIsCut()
    {
        Rig rig;
        rig.plugins->setOfflineTimeoutMs(300);
        const QString channel = rig.track(true);
        const QString id = rig.add(channel, Faulty);
        const qint64 pid = rig.pid(id);
        QVERIFY(rig.set(id, 2, 1.0)); // Hang: process() never returns
        QElapsedTimer timer;
        timer.start();
        const QString out = rig.dir.filePath(QStringLiteral("hang.wav"));
        QVERIFY(ok(rig.session.renderToFile(out, QStringLiteral("0"), QString::number(4800 * TicksPerSample))));
        QVERIFY(timer.elapsed() < 5000); // one deadline, then dry audio
        QTRY_COMPARE(rig.state(id), QStringLiteral("failed"));
        QVERIFY(rig.plugins->instance(id).value(QStringLiteral("error")).toString().contains(QStringLiteral("stopped responding")));
        QTRY_VERIFY(!alive(pid));
    }

    void editorLifecycle()
    {
        Rig rig;
        const QString channel = rig.track();
        const QString id = rig.add(channel, Gain);
        QWindow owner;
        owner.create();
        QSignalSpy editor(rig.plugins, &AudioPluginHost::editorChanged);
        QVERIFY(ok(rig.plugins->openEditor(id, &owner)));
        AudioPluginInstance *instance = rig.plugins->instanceObject(id);
        QPointer<AudioPluginEditorWindow> window = instance->editorWindow();
        QVERIFY(window);
        QCOMPARE(window->transientParent(), &owner);
        QVERIFY(rig.plugins->instance(id).value(QStringLiteral("editorOpen")).toBool());
        QVERIFY(!editor.isEmpty());
        QStringList calls = logLines();
        QVERIFY(calls.contains(QStringLiteral("gui.create embedded x11")));
        QVERIFY(calls.contains(QStringLiteral("gui.set_parent %1").arg(qint64(window->winId()))));
        QVERIFY(calls.contains(QStringLiteral("gui.show")));
        // The plugin asked to be 420 x 320; the toolkit window follows.
        QTRY_COMPARE(window->size(), QSize(420, 320));
        // A change made in the plugin's editor is one automation transaction.
        QTRY_COMPARE(rig.session.automation()->lane(id + QStringLiteral("/1"))->parameter.defaultValue, 0.25);

        // Closing the window closes the editor first, then the window goes.
        window->close();
        QTRY_VERIFY(window.isNull());
        QVERIFY(!rig.plugins->instance(id).value(QStringLiteral("editorOpen")).toBool());
        calls = logLines();
        QVERIFY(calls.indexOf(QStringLiteral("gui.hide")) > calls.indexOf(QStringLiteral("gui.show")));
        QVERIFY(calls.indexOf(QStringLiteral("gui.destroy")) > calls.indexOf(QStringLiteral("gui.hide")));

        // A plugin that dies with its editor open takes the editor with it.
        QVERIFY(ok(rig.plugins->openEditor(id, &owner)));
        window = instance->editorWindow();
        QVERIFY(window);
        ::kill(pid_t(rig.pid(id)), SIGKILL);
        QTRY_COMPARE(rig.state(id), QStringLiteral("failed"));
        QTRY_VERIFY(window.isNull());
        QVERIFY(!instance->editorOpen());
        QVERIFY(!ok(rig.plugins->openEditor(id, &owner)));
    }

    void sessionRecallsAnUnavailablePlugin()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("session.json"));
        QString id;
        double blocks = 0.0;
        {
            Rig rig;
            const QString channel = rig.track(true);
            id = rig.add(channel, Gain);
            QVERIFY(rig.set(id, 1, 0.5));
            QVERIFY(ok(rig.plugins->setBypassed(id, true)));
            QVERIFY(ok(rig.plugins->setBypassed(id, false)));
            QVERIFY(ok(rig.session.renderToFile(dir.filePath(QStringLiteral("a.wav")))));
            QVERIFY(rig.session.save(file));
            blocks = stateValue(rig.plugins->instanceObject(id)->savedState(), 2);
            QVERIFY(blocks > 0.0); // the render processed blocks; the state counts them
        }
        const auto pluginsPart = [](const QString &path) {
            QFile in(path);
            return in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object().value(QStringLiteral("plugins")).toObject()
                                                : QJsonObject();
        };
        const QJsonObject saved = pluginsPart(file);
        QCOMPARE(saved.value(QStringLiteral("schema")).toString(), AudioPluginHost::schemaId());
        {
            // Installed: the state comes back, and the automation keeps its value.
            Rig rig;
            QVERIFY2(rig.session.open(file), qPrintable(rig.session.lastError()));
            QCOMPARE(rig.state(id), QStringLiteral("ready"));
            QCOMPARE(rig.plugins->instanceObject(id)->parameter(3)->value, blocks);
            QCOMPARE(rig.session.automation()->lane(id + QStringLiteral("/1"))->parameter.defaultValue, 0.5);
        }
        {
            // Not installed: identity, parameters and state are kept, dry audio plays.
            Rig rig(false);
            QVERIFY2(rig.session.open(file), qPrintable(rig.session.lastError()));
            QCOMPARE(rig.state(id), QStringLiteral("missing"));
            QVERIFY(!rig.plugins->instance(id).value(QStringLiteral("available")).toBool());
            bool listed = false;
            for (const QVariant &row : rig.plugins->browserPlugins())
                if (row.toMap().value(QStringLiteral("pluginId")).toString() == Gain)
                    listed = !row.toMap().value(QStringLiteral("available")).toBool()
                             && row.toMap().value(QStringLiteral("state")).toString() == QLatin1String("missing");
            QVERIFY(listed);
            QVERIFY(ok(rig.session.renderToFile(dir.filePath(QStringLiteral("dry.wav")))));
            const QString again = dir.filePath(QStringLiteral("again.json"));
            QVERIFY(rig.session.save(again));
            QCOMPARE(pluginsPart(again), saved); // saved again unchanged
            // Installed later: the same instance comes back with its state.
            rig.plugins->registry()->setSearchPaths({fixtures()});
            QVERIFY(rig.plugins->registry()->scanNow());
            QVERIFY(ok(rig.plugins->reload(id)));
            QCOMPARE(rig.state(id), QStringLiteral("ready"));
            QCOMPARE(rig.plugins->instanceObject(id)->parameter(3)->value, blocks);
        }
    }

    void shutdownUnloadsEveryProcess()
    {
        auto *rig = new Rig;
        const QString channel = rig->track();
        const QStringList ids{rig->add(channel, Gain), rig->add(QStringLiteral("master"), Synth), rig->add(channel, Faulty)};
        QList<qint64> pids;
        for (const QString &id : ids) {
            QVERIFY(!id.isEmpty());
            pids.push_back(rig->pid(id));
            QVERIFY(alive(pids.last()));
        }
        QElapsedTimer timer;
        timer.start();
        delete rig;
        QVERIFY(timer.elapsed() < 5000);
        for (const qint64 pid : pids) QVERIFY(!alive(pid));
        // Each plugin was destroyed by its own process (asked to quit, not killed).
        QCOMPARE(logLines().filter(QStringLiteral("destroy org.qindatk.test.")).size(), 3);
    }
};

QTEST_MAIN(TestAudioPluginHost)
#include "tst_audio_plugin_host.moc"
