// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-062 / AUD-15: explicit layouts and spatial panning. Quad and 5.1 buses,
// mono and stereo sources placed by pan and depth, surround sources converted
// lane to lane or downmixed (ITU-R BS.775), device ports up to six channels,
// and the layout rules the mixer explains. Every number is what the graph the
// device plays renders (bus lanes through the post-fader channel meters).
#include "audio_channel_meters.h"
#include "audio_mixer_fixture.h"

#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::MixerFixture;

namespace {

const double k = std::sqrt(0.5); // -3 dB

// The lanes a channel carries at sample 0, from the channel meters.
std::vector<double> lanesOf(Fixture &f, const QString &channel)
{
    const AudioRenderGraphPtr graph = f.session.compileGraph(Rate);
    AudioChannelMeterSink sink(Rate);
    std::vector<float> out(std::size_t(2 * sink.blockFrames()), 0.0f);
    graph->render(0, sink.blockFrames(), out.data(), 2, f.scratch.data(), &sink);
    static AudioChannelMeterFrame frame; // large: not on the stack
    if (!sink.pop(frame)) return {};
    const qsizetype slot = graph->meterChannels().indexOf(channel);
    if (slot < 0) return {};
    std::vector<double> lanes;
    for (int lane = 0; lane < graph->meterLanes().at(slot); ++lane) lanes.push_back(frame.peak[slot][lane]);
    return lanes;
}

bool lanesAre(const std::vector<double> &actual, const std::vector<double> &expected)
{
    if (actual.size() != expected.size()) {
        qWarning() << "lanes" << actual.size() << "expected" << expected.size();
        return false;
    }
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (std::abs(actual[i] - expected[i]) > 1e-6) {
            qWarning() << "lane" << i << actual[i] << "expected" << expected[i];
            return false;
        }
    return true;
}

} // namespace

class TestAudioMixerSurround : public QObject {
    Q_OBJECT
private slots:
    void spatialPannerPlacesAMonoSource()
    {
        double g[AudioMixer::kMaxLanes];
        const auto law = AudioPanLaw::ConstantPower;
        // Quad (L R Ls Rs): front centre, the middle of the room, a corner.
        AudioMixer::spatialGains(AudioLayout::Quad, law, 0.0, 1.0, g);
        QVERIFY(near(g[0], k) && near(g[1], k) && g[2] == 0.0 && g[3] == 0.0);
        AudioMixer::spatialGains(AudioLayout::Quad, law, 0.0, 0.0, g);
        for (int lane = 0; lane < 4; ++lane) QVERIFY(near(g[lane], 0.5));
        AudioMixer::spatialGains(AudioLayout::Quad, law, -1.0, -1.0, g);
        QVERIFY(g[0] == 0.0 && g[1] == 0.0 && g[2] == 1.0 && g[3] == 0.0);
        // 5.1 (L R C LFE Ls Rs): a centred front source is the centre alone;
        // halfway left is L and C at -3 dB; the LFE never takes a panned source.
        AudioMixer::spatialGains(AudioLayout::Surround51, law, 0.0, 1.0, g);
        QVERIFY(g[0] == 0.0 && g[1] == 0.0 && g[2] == 1.0 && g[3] == 0.0 && g[4] == 0.0 && g[5] == 0.0);
        AudioMixer::spatialGains(AudioLayout::Surround51, law, -0.5, 1.0, g);
        QVERIFY(near(g[0], k) && near(g[2], k) && g[1] == 0.0);
        AudioMixer::spatialGains(AudioLayout::Surround51, law, 1.0, -1.0, g);
        QVERIFY(g[5] == 1.0 && g[0] == 0.0 && g[2] == 0.0 && g[3] == 0.0);
        // Constant power holds anywhere in the room.
        for (AudioLayout layout : {AudioLayout::Quad, AudioLayout::Surround51}) {
            AudioMixer::spatialGains(layout, law, 0.3, -0.4, g);
            double power = 0.0;
            for (double gain : g) power += gain * gain;
            QVERIFY2(near(power, 1.0), qPrintable(QString::number(power)));
        }
        // A stereo destination is not spatial: nothing here.
        AudioMixer::spatialGains(AudioLayout::Stereo, law, 0.0, 1.0, g);
        QCOMPARE(g[0] + g[1], 0.0);
    }

    void surroundBusesMixAndDownmix()
    {
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        QVERIFY(ok(f.mixer->setLayout(voice, QStringLiteral("Mono"))));
        const QString quad = f.mixer->addBus(QStringLiteral("Quad"), QStringLiteral("Quad")).value(QStringLiteral("channelId")).toString();
        QVERIFY(!quad.isEmpty());
        QVERIFY(ok(f.mixer->setOutput(voice, quad)));
        QCOMPARE(f.value(voice, "outputLayout").toString(), QStringLiteral("Quad"));
        // Default depth is the front: a centred mono source is L and R at -3 dB.
        QVERIFY(lanesAre(lanesOf(f, quad), {0.5 * k, 0.5 * k, 0.0, 0.0}));
        // Rear left corner: only Ls; the bus downmixes to the master at -3 dB.
        QVERIFY(f.set(voice, QStringLiteral("pan"), -1.0));
        QVERIFY(f.set(voice, QStringLiteral("depth"), -1.0));
        QVERIFY(lanesAre(lanesOf(f, quad), {0.0, 0.0, 0.5, 0.0}));
        QVERIFY(near(f.frame().at(0), 0.5 * k));
        QCOMPARE(f.frame().at(1), 0.0f);

        // 5.1: a centred front source is the centre speaker, which the stereo
        // downmix spreads to both sides at -3 dB.
        const QString five = f.mixer->addBus(QStringLiteral("5.1"), QStringLiteral("5.1")).value(QStringLiteral("channelId")).toString();
        QVERIFY(ok(f.mixer->setOutput(voice, five)));
        QVERIFY(f.set(voice, QStringLiteral("pan"), 0.0));
        QVERIFY(f.set(voice, QStringLiteral("depth"), 1.0));
        QVERIFY(lanesAre(lanesOf(f, five), {0.0, 0.0, 0.5, 0.0, 0.0, 0.0}));
        QVERIFY(near(f.frame().at(0), 0.5 * k) && near(f.frame().at(1), 0.5 * k));

        // A stereo source keeps its sides: left on L, right on R (front).
        const QString pair = f.track(QStringLiteral("Pair"), 0.5f, 0.25f);
        QVERIFY(ok(f.mixer->setOutput(pair, quad)));
        QVERIFY(lanesAre(lanesOf(f, quad), {0.5, 0.25, 0.0, 0.0}));

        // Surround to surround: Quad into 5.1 maps L R Ls Rs; 5.1 into Quad
        // folds the centre into L and R at -3 dB.
        QVERIFY(ok(f.mixer->setOutput(quad, five)));
        QVERIFY(lanesAre(lanesOf(f, five), {0.5, 0.25, 0.5, 0.0, 0.0, 0.0}));
        QVERIFY(ok(f.mixer->setOutput(quad, QStringLiteral("master"))));
        QVERIFY(ok(f.mixer->setOutput(five, quad)));
        QVERIFY(lanesAre(lanesOf(f, quad), {0.5 + 0.5 * k, 0.25 + 0.5 * k, 0.0, 0.0}));

        // Depth automation moves a (centred) source from front to rear per
        // sample; the stereo pair is disconnected so only the voice plays.
        QVERIFY(ok(f.mixer->setOutput(pair, QString())));
        QVERIFY(ok(f.mixer->setOutput(five, QStringLiteral("master"))));
        QVERIFY(ok(f.mixer->setOutput(voice, quad)));
        const QString depth = f.mixer->ensureParameter(voice, QStringLiteral("depth"));
        QVERIFY(f.point(depth, 0, 1.0) && f.point(depth, 24000, -1.0));
        QVERIFY(near(f.frame(0).at(0), 0.5 * k));                // front: L and R
        QVERIFY(near(f.frame(12000).at(0), 0.25 + 0.25 * k));   // the middle: L and Ls equally
        QVERIFY(near(f.frame(24000).at(0), 0.25));              // rear: Ls, downmixed at -3 dB
    }

    void surroundPortsCarryEveryLane()
    {
        // A 5.1 bus on a six-channel device port: each lane on its channel.
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        QVERIFY(ok(f.mixer->setLayout(voice, QStringLiteral("Mono"))));
        const QString five = f.mixer->addBus(QStringLiteral("5.1"), QStringLiteral("5.1")).value(QStringLiteral("channelId")).toString();
        QVERIFY(ok(f.mixer->setOutput(voice, five)));
        QVERIFY(f.set(voice, QStringLiteral("pan"), 1.0));
        QVERIFY(f.set(voice, QStringLiteral("depth"), -1.0)); // Rs
        const QString port = AudioMixer::devicePortId(QStringLiteral("test"), 1, 6);
        QCOMPARE(port, QStringLiteral("device:1-6@test"));
        // The mixer refuses it while no such device runs (D-112)...
        QVERIFY(!ok(f.mixer->setOutput(five, port)));
        // ...and the graph plays it on a device that has the channels.
        AudioMixerContent content = f.mixer->content();
        AudioMixerStrip settings = AudioMixer::strip(content, five);
        settings.output = port;
        content.strips.insert(five, settings);
        AudioRenderGraph::Sources sources;
        sources.timeline = f.session.timeline();
        sources.media = [&f](const QString &id, int rate) { return f.session.mediaBuffer(id, rate); };
        sources.schedule = f.session.schedule()->acquire();
        sources.sampleRate = Rate;
        sources.mixer = &content;
        sources.deviceId = QStringLiteral("test");
        sources.deviceChannels = 6;
        std::vector<float> out(6, 0.0f);
        AudioRenderGraph::compile(sources)->render(0, 1, out.data(), 6, f.scratch.data());
        QCOMPARE(out, (std::vector<float>{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f}));
        // A two-channel device cannot take it: nothing plays, and it says why.
        sources.deviceChannels = 2;
        const AudioRenderGraphPtr narrow = AudioRenderGraph::compile(sources);
        std::vector<float> two(2, 0.0f);
        narrow->render(0, 1, two.data(), 2, f.scratch.data());
        QCOMPARE(two, (std::vector<float>{0.0f, 0.0f}));
        QVERIFY(!narrow->problems().isEmpty());
    }

    void layoutsAreExplicit()
    {
        Fixture f;
        const QString voice = f.track(QStringLiteral("Voice"), 0.5f);
        QCOMPARE(f.mixer->layouts(), (QStringList{QStringLiteral("Mono"), QStringLiteral("Stereo"), QStringLiteral("Quad"),
                                                   QStringLiteral("5.1")}));
        QCOMPARE(f.mixer->trackLayouts(), (QStringList{QStringLiteral("Mono"), QStringLiteral("Stereo")}));
        const QVariantMap track = f.mixer->setLayout(voice, QStringLiteral("Quad"));
        QVERIFY(!ok(track));
        QVERIFY(track.value(QStringLiteral("error")).toString().contains(QStringLiteral("surround layouts")));
        QVERIFY(!ok(f.mixer->setLayout(QStringLiteral("master"), QStringLiteral("5.1"))));
        QVERIFY(!ok(f.mixer->addBus(QStringLiteral("Wide"), QStringLiteral("7.1"))));
        const QString five = f.mixer->addBus(QStringLiteral("Room"), QStringLiteral("5.1")).value(QStringLiteral("channelId")).toString();
        QCOMPARE(f.value(five, "layout").toString(), QStringLiteral("5.1"));
        QVERIFY(ok(f.mixer->setLayout(five, QStringLiteral("Quad"))));
        QVERIFY(ok(f.mixer->setLayout(five, QStringLiteral("5.1"))));
        // Depth is a channel parameter like pan: -1 rear .. +1 front, default front.
        const QString depth = f.mixer->ensureParameter(voice, QStringLiteral("depth"));
        const AutomationParameter parameter = f.session.automation()->lane(depth)->parameter;
        QCOMPARE(parameter.range.minimum, -1.0);
        QCOMPARE(parameter.range.maximum, 1.0);
        QCOMPARE(parameter.defaultValue, 1.0);
        // Ports: 1, 2, 4 or 6 channels.
        QCOMPARE(AudioMixer::parsePort(QStringLiteral("device:3-6@x"))->count, 4);
        QVERIFY(!AudioMixer::parsePort(QStringLiteral("device:1-3@x")));
        QCOMPARE(AudioMeterModel::channelNames(QStringLiteral("5.1"), 6),
                 (QStringList{QStringLiteral("L"), QStringLiteral("R"), QStringLiteral("C"), QStringLiteral("LFE"),
                              QStringLiteral("Ls"), QStringLiteral("Rs")}));
        // A surround bus survives save and reopen; a file giving a track a
        // surround layout is refused.
        const QString file = f.dir.filePath(QStringLiteral("room.qtkaudio"));
        QVERIFY(f.session.save(file));
        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.mixer()->channel(five).value(QStringLiteral("layout")).toString(), QStringLiteral("5.1"));
        QVariantMap mix = f.mixer->contentMap();
        QVariantList strips = mix.value(QStringLiteral("strips")).toList();
        strips.push_back(QVariantMap{{QStringLiteral("channelId"), voice}, {QStringLiteral("layout"), QStringLiteral("Quad")}});
        mix.insert(QStringLiteral("strips"), strips);
        QVERIFY(!AudioMixerModel::contentProblem(mix).isEmpty());
    }
};

QTEST_MAIN(TestAudioMixerSurround)
#include "tst_audio_mixer_surround.moc"
