// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-062: the mixer model: channel identities, parameters, routing
// constraints and their explanations, undo, session persistence, the strip
// rows MixerView draws, and device ports that never fall back.
#include "audio_mixer_fixture.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::MixerFixture;

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
        QFile in(file);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
        QJsonObject mix = root.value(QStringLiteral("mixer")).toObject();
        const QString other = AudioMixer::newId(QStringLiteral("bus"));
        QJsonArray buses = mix.value(QStringLiteral("buses")).toArray();
        buses.push_back(QJsonObject{{QStringLiteral("id"), other}, {QStringLiteral("name"), QStringLiteral("Other")}});
        QJsonArray strips = mix.value(QStringLiteral("strips")).toArray();
        strips.push_back(QJsonObject{{QStringLiteral("channelId"), group}, {QStringLiteral("output"), other}});
        strips.push_back(QJsonObject{{QStringLiteral("channelId"), other}, {QStringLiteral("output"), group}});
        mix.insert(QStringLiteral("buses"), buses);
        mix.insert(QStringLiteral("strips"), strips);
        root.insert(QStringLiteral("mixer"), mix);
        const QString bad = f.dir.filePath(QStringLiteral("loop.qtkaudio"));
        QFile out2(bad);
        QVERIFY(out2.open(QIODevice::WriteOnly));
        QVERIFY(out2.write(QJsonDocument(root).toJson()) > 0);
        out2.close();
        const QVariantMap before = reopened.mixer()->contentMap();
        const QVariantMap timeline = reopened.timeline()->contentMap();
        QVERIFY(!reopened.open(bad));
        QVERIFY2(reopened.lastError().contains(QStringLiteral("Routing loop")), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.mixer()->contentMap(), before);
        QCOMPARE(reopened.timeline()->contentMap(), timeline);
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
