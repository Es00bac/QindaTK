// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-063 audit fixtures: analysis workers cancelled or superseded after they
// ran, the memory bound on committed edits (D-294), masked spectral edits
// through the session (D-293), and the WaveformEditor's gain/fade handles and
// channel layout driving real edits.
#include "audio_analysis_source.h"
#include "audio_dsp.h"
#include "audio_session.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;

float ramp(qint64 frame) { return float(frame + 1) / 65536.0f; } // exact in float32

QString writeBuffer(const QString &path, qint64 frames, int channels, const std::function<float(int, qint64)> &value)
{
    AudioBuffer out;
    out.sampleRate = Rate;
    out.channels = channels;
    out.frames = frames;
    out.samples.resize(size_t(frames) * size_t(channels));
    for (int c = 0; c < channels; ++c)
        for (qint64 i = 0; i < frames; ++i) out.channel(c)[i] = value(c, i);
    return AudioFile::writeWav(path, out) ? path : QString();
}

QString writeTones(const QString &path)
{
    return writeBuffer(path, 48000, 1, [](int, qint64 n) {
        const double t = double(n) / Rate;
        return float(0.5 * std::sin(2.0 * AudioDsp::kPi * 1500.0 * t) + 0.5 * std::sin(2.0 * AudioDsp::kPi * 6000.0 * t));
    });
}

QVariantMap op(const QString &operation, qint64 start, qint64 end, const QVariantMap &extra = {})
{
    QVariantMap map = extra;
    map.insert(QStringLiteral("operation"), operation);
    map.insert(QStringLiteral("startSample"), QString::number(start));
    map.insert(QStringLiteral("endSample"), QString::number(end));
    return map;
}

QVariantList polygon(std::initializer_list<std::pair<qint64, double>> points)
{
    QVariantList list;
    for (const auto &[sample, hz] : points)
        list.push_back(QVariantMap{{QStringLiteral("sample"), QString::number(sample)}, {QStringLiteral("hz"), hz}});
    return list;
}

void settle()
{
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
}

// Starts the source's queued run (so its worker is launched) and waits until
// that worker has finished, without delivering its result yet.
void runWorkerToCompletion(QObject *source)
{
    QCoreApplication::sendPostedEvents(source, QEvent::MetaCall);
    QThreadPool::globalInstance()->waitForDone();
}

QString importInto(AudioSession &session, const QString &file)
{
    const QString track = session.addTrack(QStringLiteral("A"));
    return session.importMedia(file, track).value(QStringLiteral("resourceId")).toString();
}

} // namespace

class TestAudioEditBounds : public QObject {
    Q_OBJECT
private slots:
    void waveformWorkerCancelledAfterItRanIsNeverPublished()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString id = importInto(session, writeBuffer(dir.filePath(QStringLiteral("r.wav")), 24000, 1,
                                                           [](int, qint64 i) { return ramp(i); }));
        AudioWaveformSource waveform;
        waveform.setSession(&session);
        waveform.setResourceId(id);
        waveform.setChannel(0);
        waveform.setColumnCount(100);
        runWorkerToCompletion(&waveform);
        QCOMPARE(waveform.state(), QStringLiteral("loading")); // a finished pyramid waits in the queue
        waveform.cancel();
        settle();
        settle();
        QCOMPARE(waveform.state(), QStringLiteral("canceled"));
        QVERIFY(waveform.columns().isEmpty());

        // Superseded by an edit after it ran: only the edited samples are ever shown.
        waveform.refresh();
        runWorkerToCompletion(&waveform);
        QVERIFY(session.edits()->apply(id, op(QStringLiteral("silence"), 0, 1200)).value(QStringLiteral("ok")).toBool());
        QList<float> published;
        connect(&waveform, &AudioWaveformSource::resultChanged, this, [&] {
            if (waveform.columns().size() > 3) published.push_back(waveform.columns().at(3).toMap().value(QStringLiteral("maximum")).toFloat());
        });
        QTRY_COMPARE(waveform.state(), QStringLiteral("ready"));
        QVERIFY(!published.isEmpty());
        for (float value : published) QCOMPARE(value, 0.0f); // never ramp(959) from the old revision
    }

    void spectrogramWorkerCancelledOrSupersededAfterItRanIsNeverPublished()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString id = importInto(session, writeTones(dir.filePath(QStringLiteral("t.wav"))));
        AudioSpectrogramSource spectrogram;
        spectrogram.setSession(&session);
        spectrogram.setResourceId(id);
        spectrogram.setChannel(0);
        spectrogram.setColumnCount(8);
        spectrogram.setRows(48);
        QTRY_COMPARE(spectrogram.state(), QStringLiteral("ready"));
        QCOMPARE(spectrogram.sampleBoundaries().size(), 9);

        // Cancelled after its worker computed 16 columns: the 8-column result stays.
        spectrogram.setColumnCount(16);
        runWorkerToCompletion(&spectrogram);
        spectrogram.cancel();
        settle();
        settle();
        QCOMPARE(spectrogram.state(), QStringLiteral("canceled"));
        QCOMPARE(spectrogram.sampleBoundaries().size(), 9);

        // Superseded after it ran: the 12-column answer is never published.
        spectrogram.setColumnCount(12);
        runWorkerToCompletion(&spectrogram);
        QList<int> published;
        connect(&spectrogram, &AudioSpectrogramSource::resultChanged, this,
                [&] { published.push_back(int(spectrogram.sampleBoundaries().size())); });
        spectrogram.setColumnCount(4);
        QTRY_COMPARE(spectrogram.state(), QStringLiteral("ready"));
        QCOMPARE(spectrogram.sampleBoundaries().size(), 5);
        QVERIFY(!published.contains(13));
    }

    void committedEditsKeepBoundedMemory()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString id = importInto(session, writeTones(dir.filePath(QStringLiteral("t.wav"))));
        AudioSourceEditModel *edits = session.edits();
        // Gain, fades and silence hold no copy of their range (D-294).
        for (const QVariantMap &edit : {op(QStringLiteral("gain"), 0, 48000, {{QStringLiteral("gainDb"), -6.0}}),
                                        op(QStringLiteral("fadeIn"), 0, 24000),
                                        op(QStringLiteral("fadeOut"), 24000, 48000, {{QStringLiteral("shape"), QStringLiteral("equalPower")}}),
                                        op(QStringLiteral("silence"), 100, 200),
                                        op(QStringLiteral("gain"), 0, 48000, {{QStringLiteral("gainDb"), 3.0}})})
            QVERIFY(edits->apply(id, edit).value(QStringLiteral("ok")).toBool());
        QCOMPARE(edits->patchBytes(), QStringLiteral("0"));

        // Spectral patches are bounded by the budget; the newest always stays.
        // The largest of the four patches: frames centred 24064…35840 at hop 512 → [23040, 36864).
        const qint64 onePatch = qint64(sizeof(float)) * (36864 - 23040);
        edits->setPatchBudgetBytes(onePatch + 1);
        std::vector<std::vector<float>> after;
        for (qint64 start : {0, 12000, 24000, 36000}) {
            QVERIFY(edits->apply(id, op(QStringLiteral("spectralGain"), start, start + 12000,
                                        {{QStringLiteral("lowHz"), 5000.0}, {QStringLiteral("highHz"), 7000.0}}))
                        .value(QStringLiteral("ok")).toBool());
            QVERIFY(edits->heldPatchBytes() <= onePatch + 1);
            QVERIFY(edits->heldPatchBytes() > 0);
            after.push_back(edits->buffer(id)->samples);
        }
        // Undo replays from the original, recomputing dropped patches bit for bit.
        for (int step = 2; step >= 0; --step) {
            QVERIFY(edits->undo());
            QVERIFY(edits->buffer(id)->samples == after[size_t(step)]);
            QVERIFY(edits->heldPatchBytes() <= onePatch + 1); // a replay does not grow past the budget
        }
        for (int step = 1; step <= 3; ++step) {
            QVERIFY(edits->redo());
            QVERIFY(edits->buffer(id)->samples == after[size_t(step)]);
            QVERIFY(edits->heldPatchBytes() <= onePatch + 1);
        }
    }

    void maskedSpectralEditReachesTheSession()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString id = importInto(session, writeTones(dir.filePath(QStringLiteral("t.wav"))));
        AudioSourceEditModel *edits = session.edits();
        const AudioBufferPtr before = edits->buffer(id);
        // A lasso around the 6 kHz tone from sample 24000 to 36000, and a
        // brush dab elsewhere, removed in one step.
        const QVariantList mask{polygon({{24000, 5000.0}, {36000, 5000.0}, {36000, 7000.0}, {24000, 7000.0}}),
                                polygon({{4000, 1000.0}, {5000, 1000.0}, {5000, 2000.0}, {4000, 2000.0}})};
        const QVariantMap result = edits->apply(id, op(QStringLiteral("spectralGain"), 4000, 36000,
                                                       {{QStringLiteral("mask"), mask}}));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(edits->undoText(), QStringLiteral("Remove masked region"));
        const QVariantMap saved = edits->edits(id).at(0).toMap();
        QCOMPARE(saved.value(QStringLiteral("mask")).toList().size(), 2);
        QCOMPARE(saved.value(QStringLiteral("lowHz")).toDouble(), 1000.0);
        const AudioBufferPtr after = edits->buffer(id);
        // Between the two shapes no frame is processed: the samples are untouched.
        for (qint64 i = 5000 + 1024; i < 24000 - 1024; ++i) QCOMPARE(after->channel(0)[i], before->channel(0)[i]);
        AudioDsp::FrameAnalyzer analyzer(2048);
        std::vector<float> m(size_t(analyzer.bins()));
        analyzer.magnitudes(after->channel(0), after->frames, 30720, m.data());
        QVERIFY(m[256] < 1e-5f);                       // 6 kHz removed inside the lasso
        QVERIFY(std::fabs(m[64] - 0.5f) < 1e-5f);      // 1.5 kHz kept
        analyzer.magnitudes(after->channel(0), after->frames, 16384, m.data());
        QVERIFY(std::fabs(m[256] - 0.5f) < 1e-5f);     // 6 kHz kept before the lasso
        // Masks are validated: too few points, points outside the range.
        QVERIFY(!edits->apply(id, op(QStringLiteral("spectralGain"), 0, 20000,
                                     {{QStringLiteral("mask"), QVariantList{polygon({{0, 100.0}, {10, 100.0}})}}}))
                     .value(QStringLiteral("ok")).toBool());
        QVERIFY(!edits->apply(id, op(QStringLiteral("spectralGain"), 0, 20000,
                                     {{QStringLiteral("mask"), QVariantList{polygon({{0, 100.0}, {30000, 100.0}, {0, 900.0}})}}}))
                     .value(QStringLiteral("ok")).toBool());
    }

    void waveformHandlesAndChannelLayoutEditTheSession()
    {
        QTemporaryDir dir;
        const QString wav = writeBuffer(dir.filePath(QStringLiteral("stereo.wav")), 24000, 2,
                                        [](int c, qint64 i) { return c == 0 ? ramp(i) : -ramp(i); });
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QindaTK as Tk
Item {
    id: root
    width: 400; height: 200
    property string wavPath
    property string resourceId
    property alias session: audio
    property alias waveformSource: analysis
    property alias editor: editor
    Tk.AudioSession { id: audio }
    Tk.AudioWaveformSource { id: analysis; session: audio; resourceId: root.resourceId; columnCount: 200
                             splitChannels: true }
    Tk.WaveformEditor {
        id: editor
        width: 400; height: 160
        sourceId: root.resourceId
        columns: analysis.columns
        channelLayout: analysis.channelLayout
        channelColumns: analysis.channelColumns
        selectionStartSample: "0"
        selectionEndSample: "1200"
        onEditRequested: function(gesture, sourceId, channelId, operation, startSample, endSample, parameters) {
            audio.edits.apply(sourceId, Object.assign({ operation: operation, channel: channelId,
                                                        startSample: startSample, endSample: endSample }, parameters))
        }
    }
    Component.onCompleted: root.resourceId = audio.importMedia(root.wavPath, audio.addTrack("A"), "0").resourceId
}
)qml", QUrl());
        std::unique_ptr<QObject> root(component.createWithInitialProperties({{QStringLiteral("wavPath"), wav}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *source = qobject_cast<AudioWaveformSource *>(root->property("waveformSource").value<QObject *>());
        auto *session = qobject_cast<AudioSession *>(root->property("session").value<QObject *>());
        QObject *editor = root->property("editor").value<QObject *>();
        QVERIFY(source && session && editor);
        QTRY_COMPARE(source->state(), QStringLiteral("ready"));
        const QString id = root->property("resourceId").toString();
        // One band per channel, each with that channel's exact extrema.
        QCOMPARE(editor->property("channelLayout").toList().size(), 2);
        QCOMPARE(source->channelLayout().at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("R"));
        const QVariantList bands = editor->property("channelColumns").toList();
        QCOMPARE(bands.size(), 2);
        QCOMPARE(bands.at(0).toList().at(0).toMap().value(QStringLiteral("maximum")).toFloat(), ramp(119));
        QCOMPARE(bands.at(1).toList().at(0).toMap().value(QStringLiteral("minimum")).toFloat(), -ramp(119));

        // The right channel's selection, the gain the handle set: −6 dB on channel 1 only.
        editor->setProperty("selectionChannelId", QStringLiteral("1"));
        QVariant ok;
        QVERIFY(QMetaObject::invokeMethod(editor, "beginHandleDrag", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, QStringLiteral("gain"))));
        QVERIFY(ok.toBool());
        QVERIFY(QMetaObject::invokeMethod(editor, "updateHandleDrag", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, 0.0), Q_ARG(QVariant, 24.0)));
        QVERIFY(QMetaObject::invokeMethod(editor, "finishHandleDrag", Q_RETURN_ARG(QVariant, ok)));
        QVERIFY(ok.toBool());
        QVariantList list = session->edits()->edits(id);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("gainDb")).toDouble(), -6.0);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("channel")).toInt(), 1);
        AudioBufferPtr samples = session->edits()->buffer(id);
        const double g = std::pow(10.0, -6.0 / 20.0);
        QCOMPARE(samples->channel(1)[500], float(-double(ramp(500)) * g));
        QCOMPARE(samples->channel(0)[500], ramp(500));
        QCOMPARE(samples->channel(1)[1200], -ramp(1200));

        // The fade-out handle dragged to the middle: an equal-power fade over [600, 1200) on every channel.
        editor->setProperty("selectionChannelId", QString());
        editor->setProperty("fadeShape", QStringLiteral("equalPower"));
        QVERIFY(QMetaObject::invokeMethod(editor, "beginHandleDrag", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, QStringLiteral("fadeOut"))));
        const double fraction = 600.0 / 24000.0; // 200 columns of 120 samples: boundary 5 is sample 600
        QVERIFY(QMetaObject::invokeMethod(editor, "updateHandleDrag", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, fraction), Q_ARG(QVariant, 0.0)));
        QVERIFY(QMetaObject::invokeMethod(editor, "finishHandleDrag", Q_RETURN_ARG(QVariant, ok)));
        QVERIFY(ok.toBool());
        list = session->edits()->edits(id);
        QCOMPARE(list.size(), 2);
        const QVariantMap fade = list.at(1).toMap();
        QCOMPARE(fade.value(QStringLiteral("operation")).toString(), QStringLiteral("fadeOut"));
        QCOMPARE(fade.value(QStringLiteral("shape")).toString(), QStringLiteral("equalPower"));
        QCOMPARE(fade.value(QStringLiteral("channel")).toInt(), -1);
        QCOMPARE(fade.value(QStringLiteral("startSample")).toString(), QStringLiteral("600"));
        QCOMPARE(fade.value(QStringLiteral("endSample")).toString(), QStringLiteral("1200"));
        samples = session->edits()->buffer(id);
        QCOMPARE(samples->channel(0)[1199], 0.0f);
        QCOMPARE(samples->channel(0)[600], float(double(ramp(600)) * std::sin(AudioDsp::kPi / 2.0 * 599.0 / 600.0)));
    }
};

QTEST_MAIN(TestAudioEditBounds)
#include "tst_audio_edit_bounds.moc"
