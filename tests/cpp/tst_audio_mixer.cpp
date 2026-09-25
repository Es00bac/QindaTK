// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-062: the mixer model: channel identities, parameters, routing
// constraints and their explanations, undo, session persistence, the strip
// rows MixerView draws, and device ports that never fall back.
#include "audio_mixer_fixture.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::MixerFixture;

namespace {

// A copy of the saved session `file` whose mixer adds a bus that closes a
// loop with `bus`: AudioSession::open must refuse it whole.
QString loopedCopy(Fixture &f, const QString &file, const QString &bus)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return {};
    QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
    QJsonObject mix = root.value(QStringLiteral("mixer")).toObject();
    const QString other = AudioMixer::newId(QStringLiteral("bus"));
    QJsonArray buses = mix.value(QStringLiteral("buses")).toArray();
    buses.push_back(QJsonObject{{QStringLiteral("id"), other}, {QStringLiteral("name"), QStringLiteral("Other")}});
    QJsonArray strips = mix.value(QStringLiteral("strips")).toArray();
    strips.push_back(QJsonObject{{QStringLiteral("channelId"), bus}, {QStringLiteral("output"), other}});
    strips.push_back(QJsonObject{{QStringLiteral("channelId"), other}, {QStringLiteral("output"), bus}});
    mix.insert(QStringLiteral("buses"), buses);
    mix.insert(QStringLiteral("strips"), strips);
    root.insert(QStringLiteral("mixer"), mix);
    const QString bad = f.dir.filePath(QStringLiteral("loop.qtkaudio"));
    QFile out(bad);
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(root).toJson()) <= 0) return {};
    return bad;
}

QJsonObject savedFile(Fixture &f, const QString &name)
{
    const QString file = f.dir.filePath(name);
    if (!f.session.save(file)) return {};
    QFile in(file);
    return in.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(in.readAll()).object() : QJsonObject();
}

QStringList savedParameters(const QJsonObject &root)
{
    QStringList ids;
    for (const QJsonValue &value : root.value(QStringLiteral("automation")).toObject().value(QStringLiteral("parameters")).toArray())
        ids.push_back(value.toObject().value(QStringLiteral("id")).toString());
    ids.sort();
    return ids;
}

} // namespace

class TestAudioMixer : public QObject {
    Q_OBJECT
private slots:
    void identitiesDefaultsAndParameters()
    {
        Fixture f;
        QCOMPARE(f.mixer->channelIds(), QStringList{QStringLiteral("master")});
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        QVERIFY(AudioMixer::isTrack(voice));
        const QString reverb = f.bus(QStringLiteral("Reverb"));
        QVERIFY(AudioMixer::isBus(reverb));
        QCOMPARE(f.mixer->channelIds(), (QStringList{voice, reverb, QStringLiteral("master")}));
        QCOMPARE(f.mixer->strips()->channelIds(), f.mixer->channelIds());
        QCOMPARE(f.value(voice, "output").toString(), QStringLiteral("master"));
        QCOMPARE(f.value(reverb, "output").toString(), QStringLiteral("master"));
        QCOMPARE(f.value(QStringLiteral("master"), "output").toString(), QStringLiteral("main"));
        QCOMPARE(f.value(voice, "layout").toString(), QStringLiteral("Stereo"));
        QCOMPARE(f.value(voice, "channelKind").toString(), QStringLiteral("track"));
        QCOMPARE(f.value(reverb, "channelName").toString(), QStringLiteral("Reverb"));
        QCOMPARE(f.mixer->panLaw(), QStringLiteral("-3dB"));

        // Parameters exist once something asks for them, with declared domains.
        const QString pan = f.mixer->ensureParameter(voice, QStringLiteral("pan"));
        QCOMPARE(pan, voice + QStringLiteral("/pan"));
        const AutomationParameter panParameter = f.session.automation()->lane(pan)->parameter;
        QCOMPARE(panParameter.range.minimum, -1.0);
        QCOMPARE(panParameter.range.maximum, 1.0);
        QCOMPARE(panParameter.defaultValue, 0.0);
        QCOMPARE(panParameter.step, 0.01);
        const QString width = f.mixer->ensureParameter(voice, QStringLiteral("width"));
        QCOMPARE(f.session.automation()->lane(width)->parameter.range.maximum, 2.0);
        QCOMPARE(f.session.automation()->lane(width)->parameter.defaultValue, 1.0);
        // The track gain is the parameter the WP-061 controls already bind.
        const QString gain = f.mixer->ensureParameter(voice, QStringLiteral("gain"));
        QCOMPARE(gain, AudioRenderGraph::gainParameterId(AudioMixer::trackOf(voice).toString(QUuid::WithoutBraces)));
        QCOMPARE(f.session.ensureGainParameter(AudioMixer::trackOf(voice).toString(QUuid::WithoutBraces)), gain);
        QVERIFY(f.mixer->ensureParameter(voice, QStringLiteral("level")).isEmpty()); // a channel has no send level
        QVERIFY(f.mixer->ensureParameter(AudioMixer::newId(QStringLiteral("bus")), QStringLiteral("gain")).isEmpty());
        QVERIFY(!f.mixer->lastError().isEmpty());
        AudioParameterBinding *binding = f.mixer->parameterBinding(voice, QStringLiteral("pan"));
        QVERIFY(binding && binding->valid());
        QCOMPARE(f.mixer->parameterBinding(voice, QStringLiteral("pan")), binding); // one per parameter
        QVERIFY(!f.mixer->meter(voice)); // no engine: no meters

        // The default mix sounds exactly as before the mixer: a mono file on a
        // stereo track plays on both sides at unity; a mono output averages.
        const std::vector<float> stereo = f.frame();
        QCOMPARE(stereo.at(0), 0.5f);
        QCOMPARE(stereo.at(1), 0.5f);
        QCOMPARE(f.frame(0, 1).at(0), 0.5f);
    }

    void refusedRoutesAreExplained()
    {
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        const QString a = f.bus(QStringLiteral("A"));
        const QString b = f.bus(QStringLiteral("B"));
        QVERIFY(ok(f.mixer->setOutput(a, b)));
        const int steps = f.mixer->historyCount();
        const auto refused = [&](const QVariantMap &result, const QString &reason) {
            const QString error = result.value(QStringLiteral("error")).toString();
            if (ok(result) || !error.contains(reason) || f.mixer->lastError() != error) {
                qWarning() << "expected a refusal containing" << reason << "got" << result;
                return false;
            }
            return true;
        };
        QVERIFY(refused(f.mixer->setOutput(b, a), QStringLiteral("would close a feedback loop: B → A → B")));
        QVERIFY(refused(f.mixer->addSend(b, a), QStringLiteral("would close a feedback loop: B → A → B")));
        QVERIFY(refused(f.mixer->addSend(a, a), QStringLiteral("cannot send to itself")));
        QVERIFY(refused(f.mixer->setOutput(a, a), QStringLiteral("cannot feed itself")));
        QVERIFY(refused(f.mixer->setOutput(a, voice), QStringLiteral("Tracks do not accept input")));
        QVERIFY(refused(f.mixer->setOutput(QStringLiteral("master"), a), QStringLiteral("feeds only an output")));
        QVERIFY(refused(f.mixer->setOutput(a, AudioMixer::newId(QStringLiteral("bus"))), QStringLiteral("is not a bus of this mix")));
        QVERIFY(refused(f.mixer->setOutput(a, AudioMixer::devicePortId(QStringLiteral("builtin:null"), 1, 2)),
                        QStringLiteral("no audio device is running")));
        QVERIFY(refused(f.mixer->setLayout(QStringLiteral("master"), QStringLiteral("Mono")), QStringLiteral("master bus is stereo")));
        QVERIFY(refused(f.mixer->setLayout(voice, QStringLiteral("5.1")), QStringLiteral("Mono or Stereo")));
        QVERIFY(refused(f.mixer->setSoloed(QStringLiteral("master"), true), QStringLiteral("cannot be soloed")));
        QCOMPARE(f.mixer->historyCount(), steps); // refusals record nothing
        QCOMPARE(f.value(b, "output").toString(), QStringLiteral("master"));
        QCOMPARE(f.left(), 0.5f);
        // The destination list says in advance what would be refused, and why.
        bool listed = false;
        for (const QVariant &entry : f.mixer->destinations(b)) {
            const QVariantMap destination = entry.toMap();
            if (destination.value(QStringLiteral("id")).toString() != a) continue;
            listed = !destination.value(QStringLiteral("allowed")).toBool()
                && destination.value(QStringLiteral("reason")).toString().contains(QStringLiteral("feedback"));
        }
        QVERIFY(listed);
        QVERIFY(ok(f.mixer->addSend(voice, b)));
        QVERIFY(refused(f.mixer->addSend(voice, b), QStringLiteral("already sends")));
        QCOMPARE(f.mixer->historyCount(), steps + 1);
        QCOMPARE(f.left(), 1.0f); // voice direct, plus its post-fader send through B
    }

    void undoRedoAndReopen()
    {
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        const QString drums = f.track(QStringLiteral("Drums"), 0.25f);
        const QString group = f.bus(QStringLiteral("Group"));
        QVERIFY(ok(f.mixer->setOutput(voice, group)));
        QVERIFY(ok(f.mixer->setLayout(drums, QStringLiteral("Mono"))));
        QVERIFY(ok(f.mixer->setPanLaw(QStringLiteral("-6dB"))));
        QVERIFY(f.set(drums, QStringLiteral("pan"), -0.5));
        QVERIFY(ok(f.mixer->setMuted(group, true)));
        QVERIFY(ok(f.mixer->setHidden(drums, true)));
        const std::vector<float> mixed = f.frame();
        QCOMPARE(mixed.at(0), 0.1875f); // voice muted through the group; drums at -6 dB law, pan -0.5
        QCOMPARE(mixed.at(1), 0.0625f);
        // One undo step per operation; hiding is view state, not a step.
        QCOMPARE(f.mixer->historyCount(), 5);
        QCOMPARE(f.mixer->undoText(), QStringLiteral("Mute Group"));
        QVERIFY(f.mixer->undo());
        QCOMPARE(f.left(), 0.6875f);
        QVERIFY(f.mixer->redo());
        QCOMPARE(f.left(), 0.1875f);

        // Save and reopen: the same mix, the same samples, a fresh history.
        const QString file = f.dir.filePath(QStringLiteral("mix.qtkaudio"));
        QVERIFY2(f.session.save(file), qPrintable(f.session.lastError()));
        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.mixer()->contentMap(), f.mixer->contentMap());
        QCOMPARE(reopened.mixer()->channelIds(), f.mixer->channelIds());
        QCOMPARE(reopened.mixer()->hiddenChannels(), QStringList{drums});
        QCOMPARE(reopened.mixer()->historyCount(), 0);
        std::vector<float> out(2, 0.0f);
        reopened.compileGraph(Rate)->render(0, 1, out.data(), 2, f.scratch.data());
        QCOMPARE(out.at(0), mixed.at(0));
        QCOMPARE(out.at(1), mixed.at(1));

        // A file whose buses form a loop is refused whole; the session keeps its mix.
        const QString bad = loopedCopy(f, file, group);
        QVERIFY(!bad.isEmpty());
        const QVariantMap before = reopened.mixer()->contentMap();
        const QVariantMap timeline = reopened.timeline()->contentMap();
        QVERIFY(!reopened.open(bad));
        QVERIFY2(reopened.lastError().contains(QStringLiteral("Routing loop")), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.mixer()->contentMap(), before);
        QCOMPARE(reopened.timeline()->contentMap(), timeline);
    }

    void openRefusalKeepsHistoriesAndGestures()
    {
        // Audit (WP-062): a refused mixer part must not reload the parts
        // before it, which would clear their undo and cancel a held gesture.
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        const QString group = f.bus(QStringLiteral("Group"));
        QVERIFY(ok(f.mixer->setOutput(voice, group)));
        AudioParameterBinding *gain = f.mixer->parameterBinding(voice, QStringLiteral("gain"));
        QVERIFY(gain->begin(0.0) && gain->commit(-6.0));
        const QString file = f.dir.filePath(QStringLiteral("mix.qtkaudio"));
        QVERIFY2(f.session.save(file), qPrintable(f.session.lastError()));
        const QString bad = loopedCopy(f, file, group);
        QVERIFY(!bad.isEmpty());
        AudioParameterBinding *pan = f.mixer->parameterBinding(voice, QStringLiteral("pan"));
        QVERIFY(pan->begin(0.0) && pan->preview(-0.5)); // a fader or pad is held
        const int automationSteps = f.session.automation()->historyCount();
        const int timelineSteps = f.session.timeline()->historyCount();
        const int mixerSteps = f.mixer->historyCount();
        QVERIFY(automationSteps > 0 && timelineSteps > 0 && mixerSteps > 0);
        const QVariantMap automation = f.session.automation()->contentMap();

        QVERIFY(!f.session.open(bad));
        QVERIFY2(f.session.lastError().contains(QStringLiteral("Routing loop")), qPrintable(f.session.lastError()));
        QCOMPARE(f.session.automation()->historyCount(), automationSteps);
        QCOMPARE(f.session.timeline()->historyCount(), timelineSteps);
        QCOMPARE(f.mixer->historyCount(), mixerSteps);
        QCOMPARE(f.session.automation()->contentMap(), automation);
        QVERIFY(pan->gestureActive());
        QCOMPARE(f.mixer->heldChannels(), QStringList{voice});
        QVERIFY(pan->commit(-0.5)); // the held gesture still ends as one step
        QCOMPARE(f.session.automation()->historyCount(), automationSteps + 1);
        QVERIFY(f.session.automation()->undo());
        QVERIFY(f.session.automation()->undo()); // the gain step survived too
        QCOMPARE(gain->value(), 0.0);
    }

    void sessionHistoryUndoesAcrossOwners()
    {
        // One Undo over the mixer, timeline and automation histories (D-110).
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        const QString bus = f.bus(QStringLiteral("Bus"));
        AudioSessionHistory *history = f.session.history();
        const int base = history->count();
        QVERIFY(ok(f.mixer->setOutput(voice, bus)));   // mixer
        QVERIFY(ok(f.mixer->setMuted(voice, true)));   // timeline (the track's flag)
        AudioParameterBinding *gain = f.mixer->parameterBinding(voice, QStringLiteral("gain"));
        QVERIFY(gain->begin(0.0) && gain->commit(-6.0)); // automation
        QCOMPARE(history->count(), base + 3);
        QCOMPARE(history->undoOwner(), QStringLiteral("automation"));
        QVERIFY(history->undo());
        QCOMPARE(gain->value(), 0.0);
        QCOMPARE(history->undoOwner(), QStringLiteral("timeline"));
        QVERIFY(history->undo());
        QCOMPARE(f.value(voice, "muted").toBool(), false);
        QCOMPARE(history->undoOwner(), QStringLiteral("mixer"));
        QCOMPARE(history->undoText(), f.mixer->undoText());
        QVERIFY(history->undo());
        QCOMPARE(f.value(voice, "output").toString(), QStringLiteral("master"));
        // Redo replays the same order.
        QCOMPARE(history->redoOwner(), QStringLiteral("mixer"));
        QVERIFY(history->redo());
        QVERIFY(history->redo());
        QCOMPARE(f.value(voice, "muted").toBool(), true);
        QCOMPARE(history->redoOwner(), QStringLiteral("automation"));
        QVERIFY(history->redo());
        QCOMPARE(gain->value(), -6.0);
        QVERIFY(!history->canRedo());
        // An undo made on one model directly moves its entry: the session
        // undo then skips it and reverses the newest remaining step.
        QVERIFY(f.session.timeline()->undo()); // the mute
        QCOMPARE(history->redoOwner(), QStringLiteral("timeline"));
        QVERIFY(history->undo());               // the gain, then the route
        QCOMPARE(gain->value(), 0.0);
        QCOMPARE(history->undoOwner(), QStringLiteral("mixer"));
        // A new step ends every redo, as one stack does.
        QVERIFY(ok(f.mixer->setPanLaw(QStringLiteral("-6dB"))));
        QVERIFY(!history->canRedo());
        QCOMPARE(history->undoOwner(), QStringLiteral("mixer"));
        // A model that loads its content drops its entries.
        QVERIFY(ok(f.mixer->loadContent(f.mixer->contentMap())));
        QVERIFY(history->undoOwner() != QStringLiteral("mixer"));
    }

    void savesStayBoundedByTheLiveMix()
    {
        // Audit (WP-062): what users add and remove must not accumulate in
        // saved files or in the mixer's bindings.
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        const QString drums = f.track(QStringLiteral("Drums"), 0.25f);
        QVERIFY(f.set(voice, QStringLiteral("pan"), 0.25));
        const QStringList baseline = savedParameters(savedFile(f, QStringLiteral("base.qtkaudio")));
        QVERIFY(baseline.contains(voice + QStringLiteral("/pan")));
        for (int round = 0; round < 3; ++round) {
            const QString bus = f.bus(QStringLiteral("Bus"));
            const QString send = f.mixer->addSend(voice, bus).value(QStringLiteral("sendId")).toString();
            QPointer<AudioParameterBinding> level = f.mixer->parameterBinding(send, QStringLiteral("level"));
            QPointer<AudioParameterBinding> busGain = f.mixer->parameterBinding(bus, QStringLiteral("gain"));
            QVERIFY(level && busGain && level->begin(0.0) && level->commit(-3.0));
            QVERIFY(ok(f.mixer->removeBus(bus))); // takes the send with it
            // The lanes stay for undo; the bindings of what is gone are released.
            QVERIFY(f.session.automation()->lane(send + QStringLiteral("/level")));
            QTRY_VERIFY(level.isNull() && busGain.isNull());
            if (round == 0) {
                // Undo brings the bus back with its send level.
                QVERIFY(f.mixer->undo());
                AudioParameterBinding *restored = f.mixer->parameterBinding(send, QStringLiteral("level"));
                QVERIFY(restored);
                QCOMPARE(restored->value(), -3.0);
                QVERIFY(f.mixer->redo());
            }
        }
        // A deleted track: its dormant settings stay for timeline undo, but no
        // reopened session could reach them, so the file leaves them out.
        QVERIFY(f.set(drums, QStringLiteral("pan"), -0.5));
        QVERIFY(ok(f.mixer->setLayout(drums, QStringLiteral("Mono"))));
        QVERIFY(ok(f.session.timeline()->applyEdit({{QStringLiteral("kind"), QStringLiteral("removeTrack")},
                                                     {QStringLiteral("trackId"),
                                                      AudioMixer::trackOf(drums).toString(QUuid::WithoutBraces)}})));
        QVERIFY(f.mixer->content().strips.contains(drums)); // dormant
        const QJsonObject saved = savedFile(f, QStringLiteral("after.qtkaudio"));
        QStringList expected = baseline;
        expected.removeIf([&drums](const QString &id) { return id.startsWith(drums); });
        QCOMPARE(savedParameters(saved), expected);
        for (const QJsonValue &strip : saved.value(QStringLiteral("mixer")).toObject().value(QStringLiteral("strips")).toArray())
            QVERIFY(strip.toObject().value(QStringLiteral("channelId")).toString() != drums);
        // The saved file opens to the same live mix.
        AudioSession reopened;
        QVERIFY2(reopened.open(f.dir.filePath(QStringLiteral("after.qtkaudio"))), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.mixer()->channelIds(), f.mixer->channelIds());
        QCOMPARE(reopened.mixer()->contentMap(), f.mixer->savedContentMap());
    }

    void stripRowsMoveAndHideWithoutResets()
    {
        Fixture f;
        const QString one = f.track(QStringLiteral("One"), 0.5f);
        const QString two = f.track(QStringLiteral("Two"), 0.25f);
        const QString three = f.track(QStringLiteral("Three"), 0.125f);
        const QString master = QStringLiteral("master");
        AudioMixerStripModel *strips = f.mixer->strips();
        QCOMPARE(strips->channelIds(), (QStringList{one, two, three, master}));
        QSignalSpy moved(strips, &QAbstractItemModel::rowsMoved);
        QSignalSpy reset(strips, &QAbstractItemModel::modelReset);
        QSignalSpy removed(strips, &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(strips, &QAbstractItemModel::rowsInserted);
        QVERIFY(f.moveTrack(three, one));
        QCOMPARE(strips->channelIds(), (QStringList{three, one, two, master}));
        QVERIFY(!moved.isEmpty());
        QCOMPARE(int(reset.size()), 0);
        QCOMPARE(int(removed.size()), 0);
        QCOMPARE(int(inserted.size()), 0);
        // A hidden channel leaves the strips but stays in the mix.
        QVERIFY(ok(f.mixer->setHidden(two, true)));
        QCOMPARE(int(removed.size()), 1);
        QCOMPARE(strips->indexOf(two), -1);
        QVERIFY(f.mixer->channelIds().contains(two));
        QVERIFY(f.value(two, "hidden").toBool());
        // A held channel keeps its strip until the gesture ends (D-113), and a
        // reorder under the gesture moves its row instead of rebuilding it.
        AudioParameterBinding *gain = f.mixer->parameterBinding(one, QStringLiteral("gain"));
        QVERIFY(gain->begin(0.0));
        QVERIFY(gain->preview(-6.0));
        QCOMPARE(f.mixer->heldChannels(), QStringList{one});
        QVERIFY(ok(f.mixer->setHidden(one, true)));
        QVERIFY(strips->indexOf(one) >= 0);
        QVERIFY(strips->get(strips->indexOf(one)).value(QStringLiteral("held")).toBool());
        const qsizetype moves = moved.size();
        QVERIFY(f.moveTrack(one, three));
        QCOMPARE(strips->channelAt(0), one);
        QVERIFY(moved.size() > moves);
        QCOMPARE(int(reset.size()), 0);
        QVERIFY(gain->gestureActive());
        QVERIFY(near(f.left(), 0.5 * linear(-6.0) + 0.25 + 0.125)); // one held at -6 dB; hidden two still plays
        QVERIFY(gain->commit(-6.0));
        QCOMPARE(gain->lastResult(), QStringLiteral("static"));
        QVERIFY(f.mixer->heldChannels().isEmpty());
        QCOMPARE(strips->channelIds(), (QStringList{three, master}));
        QVERIFY(ok(f.mixer->setHidden(one, false)));
        QCOMPARE(strips->channelIds(), (QStringList{one, three, master}));
    }

    void devicePortsNeverFallBack()
    {
        Fixture f;
        QVERIFY(!f.track(QStringLiteral("Voice"), 0.5f).isEmpty());
        // A mix saved while a device ran: the master feeds that device's outputs 1-2.
        const QString port = AudioMixer::devicePortId(QStringLiteral("builtin:null"), 1, 2);
        QVariantMap mix = f.mixer->contentMap();
        mix.insert(QStringLiteral("strips"),
                   QVariantList{QVariantMap{{QStringLiteral("channelId"), QStringLiteral("master")},
                                            {QStringLiteral("output"), port}}});
        QVERIFY(ok(f.mixer->loadContent(mix)));
        // Without that device the port plays nothing and says why; "main" never stands in.
        const AudioRenderGraphPtr graph = f.session.compileGraph(Rate);
        std::vector<float> out(2, 0.0f);
        graph->render(0, 1, out.data(), 2, f.scratch.data());
        QCOMPARE(out.at(0), 0.0f);
        QCOMPARE(int(graph->problems().size()), 1);
        QVERIFY(!f.value(QStringLiteral("master"), "outputAvailable").toBool());
        bool explained = false;
        for (const QVariant &entry : f.mixer->outputPorts()) {
            const QVariantMap listed = entry.toMap();
            if (listed.value(QStringLiteral("id")).toString() == port)
                explained = !listed.value(QStringLiteral("available")).toBool()
                    && listed.value(QStringLiteral("reason")).toString().contains(QStringLiteral("no audio device is running"));
        }
        QVERIFY(explained);
        // An offline render is the master mix whatever the device routing.
        const QString file = f.dir.filePath(QStringLiteral("bounce.wav"));
        QVERIFY(ok(f.session.renderToFile(file, QStringLiteral("0"), QString::number(16 * TicksPerSample), Rate, 2)));
        const AudioBufferPtr bounce = AudioFile::readWav(file);
        QVERIFY(bounce && bounce->frames == 16);
        QCOMPARE(bounce->channel(0)[0], 0.5f);
        QCOMPARE(bounce->channel(1)[15], 0.5f);
    }
};

QTEST_MAIN(TestAudioMixer)
#include "tst_audio_mixer.moc"
