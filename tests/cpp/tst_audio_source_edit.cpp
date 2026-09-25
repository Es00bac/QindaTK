// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-063 editing fixtures: exact trim/fade/gain samples, undo/redo, the
// session's rendered output and persistence, asynchronous cancellable
// previews, and analysis sources that follow every edit (D-134…D-137).
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
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate; // 14700, exact

QString ticks(qint64 samples) { return QString::number(samples * TicksPerSample); }
float ramp(qint64 frame) { return float(frame + 1) / 65536.0f; } // exact in float32

std::shared_ptr<AudioBuffer> buffer(qint64 frames, int channels, const std::function<float(int, qint64)> &value)
{
    auto out = std::make_shared<AudioBuffer>();
    out->sampleRate = Rate;
    out->channels = channels;
    out->frames = frames;
    out->samples.resize(size_t(frames) * size_t(channels));
    for (int c = 0; c < channels; ++c)
        for (qint64 i = 0; i < frames; ++i) out->channel(c)[i] = value(c, i);
    return out;
}

QString writeWav(const QString &path, const AudioBuffer &samples)
{
    return AudioFile::writeWav(path, samples) ? path : QString();
}

QString writeRamp(const QString &path, qint64 frames)
{
    return writeWav(path, *buffer(frames, 1, [](int, qint64 i) { return ramp(i); }));
}

QString writeTones(const QString &path)
{
    return writeWav(path, *buffer(48000, 1, [](int, qint64 n) {
        const double t = double(n) / Rate;
        return float(0.5 * std::sin(2.0 * AudioDsp::kPi * 1500.0 * t) + 0.5 * std::sin(2.0 * AudioDsp::kPi * 6000.0 * t));
    }));
}

std::vector<float> readMono(const QString &path)
{
    const AudioBufferPtr samples = AudioFile::readWav(path);
    return samples ? std::vector<float>(samples->channel(0), samples->channel(0) + samples->frames) : std::vector<float>{};
}

AudioSourceEdit edit(const QVariantMap &map)
{
    QString error;
    const auto parsed = AudioSourceEdit::fromMap(map, &error);
    return parsed.value_or(AudioSourceEdit{});
}

AudioBufferPtr applied(const AudioBufferPtr &source, const QVariantMap &map)
{
    AudioSourceEdit e = edit(map);
    QString error;
    if (!AudioSourceEditing::prepare(*source, e, {}, &error)) return nullptr;
    return AudioSourceEditing::apply(*source, e);
}

QVariantMap op(const QString &operation, qint64 start, qint64 end, const QVariantMap &extra = {})
{
    QVariantMap map = extra;
    map.insert(QStringLiteral("operation"), operation);
    map.insert(QStringLiteral("startSample"), QString::number(start));
    map.insert(QStringLiteral("endSample"), QString::number(end));
    return map;
}

void settle()
{
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
}

} // namespace

class TestAudioSourceEdit : public QObject {
    Q_OBJECT
private slots:
    void editsProduceExactSamples()
    {
        const auto source = buffer(16, 2, [](int c, qint64 i) { return c == 0 ? ramp(i) : 1.0f; });
        const auto fadeIn = applied(source, op(QStringLiteral("fadeIn"), 4, 8));
        QVERIFY(fadeIn);
        const float expectedIn[] = {0.0f, 0.25f, 0.5f, 0.75f};
        for (int i = 0; i < 4; ++i) QCOMPARE(fadeIn->channel(1)[4 + i], expectedIn[i]);
        QCOMPARE(fadeIn->channel(1)[3], 1.0f);
        QCOMPARE(fadeIn->channel(1)[8], 1.0f);
        const auto fadeOut = applied(source, op(QStringLiteral("fadeOut"), 8, 12));
        const float expectedOut[] = {0.75f, 0.5f, 0.25f, 0.0f};
        for (int i = 0; i < 4; ++i) QCOMPARE(fadeOut->channel(1)[8 + i], expectedOut[i]);
        const auto power = applied(source, op(QStringLiteral("fadeIn"), 0, 4, {{QStringLiteral("shape"), QStringLiteral("equalPower")}}));
        QVERIFY(std::fabs(power->channel(1)[2] - float(std::sqrt(0.5))) < 1e-7f);
        // Gain on one channel leaves the other untouched.
        const auto gain = applied(source, op(QStringLiteral("gain"), 0, 16, {{QStringLiteral("channel"), 1},
                                                                              {QStringLiteral("gainDb"), -6.0}}));
        QVERIFY(std::fabs(gain->channel(1)[5] - float(std::pow(10.0, -6.0 / 20.0))) < 1e-7f);
        for (qint64 i = 0; i < 16; ++i) QCOMPARE(gain->channel(0)[i], ramp(i));
        const auto silence = applied(source, op(QStringLiteral("silence"), 0, 2));
        QCOMPARE(silence->channel(0)[1], 0.0f);
        QCOMPARE(silence->channel(0)[2], ramp(2));
        // Delete and crop change the length on every channel.
        const auto removed = applied(source, op(QStringLiteral("delete"), 2, 5));
        QCOMPARE(removed->frames, qint64(13));
        QCOMPARE(removed->channel(0)[1], ramp(1));
        QCOMPARE(removed->channel(0)[2], ramp(5));
        QCOMPARE(removed->channel(0)[12], ramp(15));
        const auto cropped = applied(source, op(QStringLiteral("crop"), 4, 8));
        QCOMPARE(cropped->frames, qint64(4));
        QCOMPARE(cropped->channel(0)[0], ramp(4));
        QCOMPARE(cropped->channel(0)[3], ramp(7));
        QCOMPARE(cropped->channels, 2);
    }

    void editsAreValidated()
    {
        const auto source = buffer(4096, 2, [](int, qint64) { return 0.5f; });
        const auto problem = [&](const QVariantMap &map) {
            QString error;
            const auto parsed = AudioSourceEdit::fromMap(map, &error);
            return parsed ? AudioSourceEditing::validate(*source, *parsed) : error;
        };
        QVERIFY(problem(op(QStringLiteral("gain"), 0, 4096)).isEmpty());
        QVERIFY(!problem(op(QStringLiteral("delete"), 0, 10, {{QStringLiteral("channel"), 0}})).isEmpty());
        QVERIFY(!problem(op(QStringLiteral("delete"), 0, 4096)).isEmpty()); // would empty the source
        QVERIFY(!problem(op(QStringLiteral("gain"), 10, 5000)).isEmpty());
        QVERIFY(!problem(op(QStringLiteral("gain"), 0, 10, {{QStringLiteral("channel"), 2}})).isEmpty());
        QVERIFY(!problem(op(QStringLiteral("stretch"), 0, 10)).isEmpty());
        QVERIFY(!problem(op(QStringLiteral("spectralGain"), 100, 300, {{QStringLiteral("highHz"), 1000.0}})).isEmpty());
        QVERIFY(!problem(op(QStringLiteral("fadeIn"), 0, 10, {{QStringLiteral("shape"), QStringLiteral("cubic")}})).isEmpty());
        const QVariantList capabilities = AudioSourceEditing::capabilities(*source);
        QCOMPARE(capabilities.size(), 7);
        QCOMPARE(capabilities.at(4).toMap().value(QStringLiteral("operation")).toString(), QStringLiteral("delete"));
        QCOMPARE(capabilities.at(4).toMap().value(QStringLiteral("perChannel")).toBool(), false);
        QCOMPARE(capabilities.at(0).toMap().value(QStringLiteral("perChannel")).toBool(), true);
    }

    void renderFollowsEditsUndoAndRedo()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString track = session.addTrack(QStringLiteral("Edit"));
        const QVariantMap imported = session.importMedia(writeRamp(dir.filePath(QStringLiteral("ramp.wav")), 24000), track);
        QVERIFY(imported.value(QStringLiteral("ok")).toBool());
        const QString id = imported.value(QStringLiteral("resourceId")).toString();
        AudioSourceEditModel *edits = session.edits();
        const QString before = edits->revision(id);
        const QVariantMap faded = edits->apply(id, op(QStringLiteral("fadeIn"), 0, 1000));
        QVERIFY2(faded.value(QStringLiteral("ok")).toBool(), qPrintable(faded.value(QStringLiteral("error")).toString()));
        QVERIFY(edits->revision(id) != before);
        QCOMPARE(edits->undoText(), QStringLiteral("Fade in"));
        const QString out = dir.filePath(QStringLiteral("out.wav"));
        const auto render = [&] {
            const QVariantMap result = session.renderToFile(out, QStringLiteral("0"), ticks(24000), Rate, 1);
            return result.value(QStringLiteral("ok")).toBool() ? readMono(out) : std::vector<float>{};
        };
        std::vector<float> samples = render();
        QCOMPARE(qint64(samples.size()), qint64(24000));
        for (qint64 i : {0, 1, 500, 999})
            QCOMPARE(samples[size_t(i)], float(double(ramp(i)) * (double(i) / 1000.0)));
        QCOMPARE(samples[1000], ramp(1000));
        QVERIFY(edits->undo());
        samples = render();
        for (qint64 i : {0, 500, 23999}) QCOMPARE(samples[size_t(i)], ramp(i));
        QVERIFY(edits->redo());
        samples = render();
        QCOMPARE(samples[500], float(double(ramp(500)) * 0.5));
        // Deleting samples shifts later material earlier inside the clip; the
        // clip keeps its length and its tail past the new end is silent.
        QVERIFY(edits->apply(id, op(QStringLiteral("delete"), 0, 1000)).value(QStringLiteral("ok")).toBool());
        QCOMPARE(edits->frames(id), QStringLiteral("23000"));
        samples = render();
        QCOMPARE(samples[0], ramp(1000));
        QCOMPARE(samples[22999], ramp(23999));
        QCOMPARE(samples[23000], 0.0f);
        QCOMPARE(edits->historyCount(), 2);
        QVERIFY(edits->undo() && edits->undo());
        QVERIFY(!edits->canUndo());
        QCOMPARE(render()[0], ramp(0));
        // A failed edit changes nothing and reports why.
        QVERIFY(!edits->apply(id, op(QStringLiteral("gain"), 0, 99999)).value(QStringLiteral("ok")).toBool());
        QVERIFY(!edits->lastError().isEmpty());
        QCOMPARE(edits->historyCount(), 2); // the redo steps are still there
    }

    void sessionSavesAndReopensEdits()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("edited.qaudio"));
        std::vector<float> expected;
        {
            AudioSession session;
            const QString track = session.addTrack(QStringLiteral("A"));
            const QString id = session.importMedia(writeTones(dir.filePath(QStringLiteral("tones.wav"))), track)
                                   .value(QStringLiteral("resourceId")).toString();
            QVERIFY(session.edits()->apply(id, op(QStringLiteral("gain"), 0, 48000, {{QStringLiteral("gainDb"), -6.0}}))
                        .value(QStringLiteral("ok")).toBool());
            QVERIFY(session.edits()->apply(id, op(QStringLiteral("spectralGain"), 0, 48000,
                                                  {{QStringLiteral("lowHz"), 5000.0}, {QStringLiteral("highHz"), 7000.0}}))
                        .value(QStringLiteral("ok")).toBool());
            QVERIFY(session.edits()->apply(id, op(QStringLiteral("crop"), 0, 40000)).value(QStringLiteral("ok")).toBool());
            QVERIFY(session.save(file));
            const QString out = dir.filePath(QStringLiteral("first.wav"));
            QVERIFY(session.renderToFile(out, QStringLiteral("0"), ticks(48000), Rate, 1).value(QStringLiteral("ok")).toBool());
            expected = readMono(out);
        }
        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        const QString id = reopened.media().value(0).toMap().value(QStringLiteral("resourceId")).toString();
        const QVariantList list = reopened.edits()->edits(id);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("operation")).toString(), QStringLiteral("spectralGain"));
        QCOMPARE(reopened.edits()->frames(id), QStringLiteral("40000"));
        QVERIFY(!reopened.edits()->canUndo()); // history is per editing run
        const QString out = dir.filePath(QStringLiteral("second.wav"));
        QVERIFY(reopened.renderToFile(out, QStringLiteral("0"), ticks(48000), Rate, 1).value(QStringLiteral("ok")).toBool());
        QCOMPARE(readMono(out), expected); // recomputed deterministically
    }

    void previewIsProvisionalAndCancellable()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString track = session.addTrack(QStringLiteral("A"));
        const QString id = session.importMedia(writeTones(dir.filePath(QStringLiteral("tones.wav"))), track)
                               .value(QStringLiteral("resourceId")).toString();
        AudioSourceEditModel *edits = session.edits();
        const AudioBufferPtr committed = edits->buffer(id);
        const QVariantMap removal = op(QStringLiteral("spectralGain"), 0, 48000,
                                       {{QStringLiteral("lowHz"), 5000.0}, {QStringLiteral("highHz"), 7000.0}});
        QSignalSpy ready(edits, &AudioSourceEditModel::previewReady);
        QSignalSpy canceled(edits, &AudioSourceEditModel::previewCanceled);

        // Cancelled while computing: nothing is ever published.
        QVERIFY(edits->preview(QStringLiteral("p1"), id, removal).value(QStringLiteral("ok")).toBool());
        QCOMPARE(edits->previewState(), QStringLiteral("computing"));
        QVERIFY(!edits->commitPreview(QStringLiteral("p1")).value(QStringLiteral("ok")).toBool()); // still computing
        QVERIFY(edits->cancelPreview(QStringLiteral("p1")));
        settle();
        QCOMPARE(edits->previewState(), QStringLiteral("canceled"));
        QCOMPARE(ready.count(), 0);
        QCOMPARE(canceled.count(), 1);
        QVERIFY(edits->buffer(id) == committed);

        // Ready: the previewed samples play until cancelled.
        QVERIFY(edits->preview(QStringLiteral("p2"), id, removal).value(QStringLiteral("ok")).toBool());
        QVERIFY(ready.wait(10000));
        QCOMPARE(ready.last().at(0).toString(), QStringLiteral("p2"));
        QCOMPARE(edits->previewState(), QStringLiteral("ready"));
        const AudioBufferPtr previewed = edits->buffer(id);
        QVERIFY(previewed != committed);
        // Sample 24001: the 6 kHz tone contributes 0.5·sin(π/4) there.
        QVERIFY(std::fabs(previewed->channel(0)[24001] - committed->channel(0)[24001]) > 0.3f);
        QVERIFY(session.mediaBuffer(id, Rate) == previewed); // what the engine plays
        QVERIFY(edits->cancelPreview(QStringLiteral("p2")));
        QVERIFY(edits->buffer(id) == committed);
        QCOMPARE(edits->historyCount(), 0);

        // Committed: one undo step with the previewed samples.
        QVERIFY(edits->preview(QStringLiteral("p3"), id, removal).value(QStringLiteral("ok")).toBool());
        QVERIFY(ready.wait(10000));
        const AudioBufferPtr result = edits->buffer(id);
        QVERIFY(edits->commitPreview(QStringLiteral("p3")).value(QStringLiteral("ok")).toBool());
        QVERIFY(edits->buffer(id) == result);
        QCOMPARE(edits->undoText(), QStringLiteral("Remove 5000–7000 Hz"));
        QVERIFY(edits->undo());
        const AudioBufferPtr restored = edits->buffer(id);
        QCOMPARE(restored->samples, committed->samples);
    }

    void analysisSourcesFollowEdits()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString track = session.addTrack(QStringLiteral("A"));
        const QString id = session.importMedia(writeRamp(dir.filePath(QStringLiteral("ramp.wav")), 24000), track)
                               .value(QStringLiteral("resourceId")).toString();
        AudioWaveformSource waveform;
        waveform.setSession(&session);
        waveform.setResourceId(id);
        waveform.setChannel(0);
        waveform.setColumnCount(100);
        QCOMPARE(waveform.state(), QStringLiteral("loading"));
        QTRY_COMPARE(waveform.state(), QStringLiteral("ready"));
        QVariantList columns = waveform.columns();
        QCOMPARE(columns.size(), 100);
        QCOMPARE(columns.at(3).toMap().value(QStringLiteral("startSample")).toString(), QStringLiteral("720"));
        QCOMPARE(columns.at(3).toMap().value(QStringLiteral("minimum")).toFloat(), ramp(720));
        QCOMPARE(columns.at(3).toMap().value(QStringLiteral("maximum")).toFloat(), ramp(959));
        const QString revision = waveform.revision();

        // An edit invalidates the pyramid at once and the columns follow it.
        QVERIFY(session.edits()->apply(id, op(QStringLiteral("silence"), 0, 1200)).value(QStringLiteral("ok")).toBool());
        QCOMPARE(waveform.state(), QStringLiteral("loading"));
        QTRY_COMPARE(waveform.state(), QStringLiteral("ready"));
        QVERIFY(waveform.revision() != revision);
        columns = waveform.columns();
        QCOMPARE(columns.at(3).toMap().value(QStringLiteral("maximum")).toFloat(), 0.0f);
        QCOMPARE(columns.at(5).toMap().value(QStringLiteral("minimum")).toFloat(), ramp(1200));

        // Zoom from the whole file to single samples.
        waveform.zoom(QStringLiteral("12000"), 4.0);
        QCOMPARE(waveform.startSample(), QStringLiteral("9000"));
        QCOMPARE(waveform.endSample(), QStringLiteral("15000"));
        waveform.setStartSample(QStringLiteral("5000"));
        waveform.setEndSample(QStringLiteral("5010"));
        QTRY_COMPARE(waveform.state(), QStringLiteral("ready"));
        columns = waveform.columns();
        QCOMPARE(columns.size(), 10);
        for (int i = 0; i < 10; ++i) {
            const QVariantMap column = columns.at(i).toMap();
            QCOMPARE(column.value(QStringLiteral("startSample")).toString(), QString::number(5000 + i));
            QCOMPARE(column.value(QStringLiteral("minimum")).toFloat(), ramp(5000 + i));
            QCOMPARE(column.value(QStringLiteral("maximum")).toFloat(), ramp(5000 + i));
        }
        const QVariantMap extrema = waveform.extrema(QStringLiteral("1100"), QStringLiteral("1300"));
        QCOMPARE(extrema.value(QStringLiteral("minimum")).toFloat(), 0.0f);
        QCOMPARE(extrema.value(QStringLiteral("maximum")).toFloat(), ramp(1299));
        waveform.showAll();
        QCOMPARE(waveform.endSample(), QString());
    }

    void spectrogramSourceAnalysesAndCancels()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString track = session.addTrack(QStringLiteral("A"));
        const QString id = session.importMedia(writeTones(dir.filePath(QStringLiteral("tones.wav"))), track)
                               .value(QStringLiteral("resourceId")).toString();
        AudioSpectrogramSource spectrogram;
        spectrogram.setSession(&session);
        spectrogram.setResourceId(id);
        spectrogram.setChannel(0);
        spectrogram.setColumnCount(8);
        spectrogram.setRows(48);
        spectrogram.setMinimumHz(0.0);
        spectrogram.setMaximumHz(24000.0);
        spectrogram.setFrequencyScale(QStringLiteral("linear"));
        QTRY_COMPARE(spectrogram.state(), QStringLiteral("ready"));
        QCOMPARE(spectrogram.sampleBoundaries().size(), 9);
        QCOMPARE(spectrogram.sampleBoundaries().last(), QStringLiteral("48000"));
        QCOMPARE(spectrogram.rowForFrequency(1500.0), 3);
        QCOMPARE(spectrogram.rowForFrequency(6000.0), 12);
        // Each 0.5-amplitude tone reads -6.02 dBFS in its band.
        QVERIFY(std::fabs(spectrogram.cell(4, 3).value(QStringLiteral("decibels")).toDouble() + 6.0206) < 0.01);
        QVERIFY(std::fabs(spectrogram.cell(4, 12).value(QStringLiteral("decibels")).toDouble() + 6.0206) < 0.01);
        QVERIFY(spectrogram.cell(4, 30).value(QStringLiteral("decibels")).toDouble() < -100.0);
        QVERIFY(!spectrogram.tiles().isEmpty());
        QVERIFY(spectrogram.cachedTiles() > 0);

        // A spectral edit is analysed as soon as it lands.
        QVERIFY(session.edits()->apply(id, op(QStringLiteral("spectralGain"), 0, 48000,
                                              {{QStringLiteral("lowHz"), 5000.0}, {QStringLiteral("highHz"), 7000.0}}))
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(spectrogram.state(), QStringLiteral("loading"));
        QTRY_COMPARE(spectrogram.state(), QStringLiteral("ready"));
        QVERIFY(spectrogram.cell(4, 12).value(QStringLiteral("decibels")).toDouble() < -100.0);
        QVERIFY(std::fabs(spectrogram.cell(4, 3).value(QStringLiteral("decibels")).toDouble() + 6.0206) < 0.01);

        // Cancelled work is never published: the previous result stays.
        spectrogram.setColumnCount(16);
        spectrogram.cancel();
        QCOMPARE(spectrogram.state(), QStringLiteral("canceled"));
        settle();
        settle();
        QCOMPARE(spectrogram.state(), QStringLiteral("canceled"));
        QCOMPARE(spectrogram.sampleBoundaries().size(), 9);
    }

    void waveformEditorGesturesEditTheSession()
    {
        QTemporaryDir dir;
        const QString wav = writeRamp(dir.filePath(QStringLiteral("ramp.wav")), 24000);
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
    Tk.AudioWaveformSource { id: analysis; session: audio; resourceId: root.resourceId; channel: 0; columnCount: 200 }
    Tk.WaveformEditor {
        id: editor
        width: 400; height: 120
        sourceId: root.resourceId
        channelId: "0"
        columns: analysis.columns
        selectionStartSample: "0"
        selectionEndSample: "1200"
        onEditRequested: function(gesture, sourceId, channelId, operation, startSample, endSample, parameters) {
            audio.edits.apply(sourceId, Object.assign({ operation: operation, channel: channelId,
                                                        startSample: startSample, endSample: endSample }, parameters))
        }
        onZoomRequested: function(anchor, factor) { analysis.zoom(anchor, factor) }
    }
    Component.onCompleted: {
        const track = audio.addTrack("A")
        root.resourceId = audio.importMedia(root.wavPath, track, "0").resourceId
    }
}
)qml", QUrl());
        std::unique_ptr<QObject> root(component.createWithInitialProperties({{QStringLiteral("wavPath"), wav}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *source = qobject_cast<AudioWaveformSource *>(root->property("waveformSource").value<QObject *>());
        auto *session = qobject_cast<AudioSession *>(root->property("session").value<QObject *>());
        QObject *editor = root->property("editor").value<QObject *>();
        QVERIFY(source && session && editor);
        QTRY_COMPARE(source->state(), QStringLiteral("ready"));
        QTRY_COMPARE(editor->property("columns").toList().size(), 200);
        const QString id = root->property("resourceId").toString();

        QVariant accepted;
        QVERIFY(QMetaObject::invokeMethod(editor, "requestEdit", Q_RETURN_ARG(QVariant, accepted),
                                          Q_ARG(QVariant, QStringLiteral("fadeIn")), Q_ARG(QVariant, QVariantMap{})));
        QVERIFY(accepted.toBool());
        const QVariantList list = session->edits()->edits(id);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("operation")).toString(), QStringLiteral("fadeIn"));
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("channel")).toInt(), 0);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("endSample")).toString(), QStringLiteral("1200"));
        QTRY_COMPARE(source->state(), QStringLiteral("ready"));
        // Column 0 covers samples 0…119 of the faded source.
        const QVariantMap first = editor->property("columns").toList().at(0).toMap();
        QCOMPARE(first.value(QStringLiteral("minimum")).toFloat(), 0.0f);
        QCOMPARE(first.value(QStringLiteral("maximum")).toFloat(), float(double(ramp(119)) * (119.0 / 1200.0)));

        // The editor's zoom request narrows the analysed range.
        QVERIFY(QMetaObject::invokeMethod(editor, "zoomRequested", Q_ARG(QString, QStringLiteral("12000")),
                                          Q_ARG(double, 2.0)));
        QCOMPARE(source->startSample(), QStringLiteral("6000"));
        QCOMPARE(source->endSample(), QStringLiteral("18000"));
    }
};

QTEST_MAIN(TestAudioSourceEdit)
#include "tst_audio_source_edit.moc"
