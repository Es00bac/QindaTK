// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-068 in the session (D-194…D-197): AudioPitchSource analyses the samples
// that play on a worker thread and publishes PitchEditor's snapshot; pitch
// edits are one undo step each, render into the offline mix, survive save and
// reopen, hold the analysis through previews, never publish a stale analysis,
// drive the real PitchEditor, and allocate nothing on the device thread.
#include "audio_engine.h"
#include "audio_pitch.h"
#include "audio_pitch_source.h"
#include "audio_session.h"

#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>
#include <cmath>
#include <functional>

using namespace QindaTK;

#include "audio_allocation_probe.h"

namespace {

constexpr int Rate = 48000;
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate;
constexpr double Pi = 3.14159265358979323846;

QString ticks(qint64 samples) { return QString::number(samples * TicksPerSample); }
bool ok(const QVariantMap &result) { return result.value(QStringLiteral("ok")).toBool(); }

// 1/h harmonics under a 1 kHz formant with a phase-continuous f0(t).
AudioBuffer voice(const std::function<double(double)> &f0, double seconds)
{
    AudioBuffer buffer;
    buffer.sampleRate = Rate;
    buffer.channels = 1;
    buffer.frames = qint64(seconds * Rate);
    buffer.samples.resize(size_t(buffer.frames));
    double phase = 0.0;
    for (qint64 i = 0; i < buffer.frames; ++i) {
        const double hz = f0(double(i) / Rate);
        double v = 0.0;
        for (int h = 1; h * hz < 5000.0; ++h)
            v += (0.25 + 1.0 / (1.0 + std::pow((h * hz - 1000.0) / 250.0, 2))) / h * std::sin(h * phase);
        buffer.samples[size_t(i)] = float(0.3 * v);
        phase += 2.0 * Pi * hz / Rate;
    }
    return buffer;
}

// A3 for half a second, then E4 (legato): notes [600, 24600) and [24600, 47880).
QString writeLegato(const QString &path)
{
    return AudioFile::writeWav(path, voice([](double t) { return t < 0.5 ? 220.0 : 330.0; }, 1.0)) ? path : QString();
}

std::vector<float> render(AudioSession &session, const QString &file)
{
    if (!ok(session.renderToFile(file, QStringLiteral("0"), ticks(48000), Rate, 1))) return {};
    const AudioBufferPtr samples = AudioFile::readWav(file);
    return samples ? std::vector<float>(samples->channel(0), samples->channel(0) + samples->frames) : std::vector<float>{};
}

// Largest |cents − expected| of the rendered frames centred in [start, end); +inf if one is unvoiced.
double worstError(const std::vector<float> &x, qint64 start, qint64 end, double expected)
{
    const auto a = AudioPitch::analyse(x.data(), qint64(x.size()), Rate, start, end, {}, {});
    double worst = a ? 0.0 : std::numeric_limits<double>::infinity();
    for (const AudioPitch::Frame &frame : a->frames)
        worst = frame.voiced ? std::max(worst, std::fabs(frame.cents - expected)) : std::numeric_limits<double>::infinity();
    return worst;
}

double pitchAt(const AudioPitchSource &source, qint64 sample)
{
    return source.noteAt(QString::number(sample)).value(QStringLiteral("pitchCents")).toDouble();
}

struct Fixture {
    QTemporaryDir dir;
    AudioSession session;
    AudioPitchSource pitch;
    QString id;
    Fixture()
    {
        const QString track = session.addTrack(QStringLiteral("Vocal"));
        id = session.importMedia(writeLegato(dir.filePath(QStringLiteral("legato.wav"))), track)
                 .value(QStringLiteral("resourceId")).toString();
        pitch.setSession(&session);
        pitch.setResourceId(id);
        pitch.setColumnCount(200);
    }
    QVariantMap editTo(double cents)
    {
        return pitch.editFor(QStringLiteral("segment-600"), QStringLiteral("note-600"), QStringLiteral("pitch"),
                             QStringLiteral("600"), QStringLiteral("600"), cents, 1.0);
    }
};

} // namespace

class TestAudioPitchSession : public QObject {
    Q_OBJECT
private slots:
    void sourcePublishesTheEditorSnapshot()
    {
        Fixture f;
        QVERIFY(!f.id.isEmpty());
        QCOMPARE(f.pitch.state(), QStringLiteral("loading"));
        QTRY_COMPARE(f.pitch.state(), QStringLiteral("ready"));
        QVERIFY(f.pitch.isCurrent());
        QCOMPARE(f.pitch.analysisRuns(), 1);
        QCOMPARE(f.pitch.detector(), QStringLiteral("YIN"));
        const QVariantMap snapshot = f.pitch.snapshot();
        QVERIFY(snapshot.value(QStringLiteral("available")).toBool());
        QVERIFY(!snapshot.value(QStringLiteral("stale")).toBool());
        QVERIFY(std::fabs(snapshot.value(QStringLiteral("confidenceThreshold")).toDouble() - 0.85) < 1e-12);
        QCOMPARE(snapshot.value(QStringLiteral("hopSamples")).toInt(), 240);
        QVERIFY(snapshot.value(QStringLiteral("minimumPitchCents")).toDouble() <= 5550.0);
        QVERIFY(snapshot.value(QStringLiteral("maximumPitchCents")).toDouble() >= 6550.0);
        const QVariantList segments = snapshot.value(QStringLiteral("segments")).toList();
        QCOMPARE(segments.size(), 1);
        const QVariantMap segment = segments.at(0).toMap();
        QCOMPARE(segment.value(QStringLiteral("id")).toString(), QStringLiteral("segment-600"));
        const QVariantList notes = segment.value(QStringLiteral("notes")).toList();
        QCOMPARE(notes.size(), 2);
        const QVariantMap a3 = notes.at(0).toMap();
        const QVariantMap e4 = notes.at(1).toMap();
        QCOMPARE(a3.value(QStringLiteral("id")).toString(), QStringLiteral("note-600"));
        QCOMPARE(a3.value(QStringLiteral("startSample")).toString(), QStringLiteral("600"));
        QCOMPARE(a3.value(QStringLiteral("endSample")).toString(), QStringLiteral("24600"));
        QCOMPARE(a3.value(QStringLiteral("label")).toString(), QStringLiteral("A3"));
        QVERIFY(std::fabs(a3.value(QStringLiteral("pitchCents")).toDouble() - 5700.0) < 0.05);
        QCOMPARE(e4.value(QStringLiteral("label")).toString(), QStringLiteral("E4"));
        QVERIFY(std::fabs(e4.value(QStringLiteral("pitchCents")).toDouble() - AudioPitch::hzToCents(330.0)) < 0.05);
        QVERIFY(std::fabs(a3.value(QStringLiteral("x0")).toDouble() - 600.0 / 48000.0) < 1e-12);
        QVERIFY(!segment.value(QStringLiteral("contour")).toList().isEmpty());
        for (const QVariant &edge : snapshot.value(QStringLiteral("sampleBoundaries")).toList()) {
            const double x = edge.toMap().value(QStringLiteral("x")).toDouble();
            QVERIFY(x >= 0.0 && x <= 1.0);
        }
        const QVariantMap frame = f.pitch.frameAt(QStringLiteral("12000"));
        QVERIFY(frame.value(QStringLiteral("voiced")).toBool());
        QVERIFY(std::fabs(frame.value(QStringLiteral("hz")).toDouble() - 220.0) < 0.01);
        QCOMPARE(f.pitch.notes().size(), 2);

        // Zooming re-publishes from the cached analysis.
        f.pitch.zoom(QStringLiteral("12000"), 4.0);
        QTRY_COMPARE(f.pitch.state(), QStringLiteral("ready"));
        QCOMPARE(f.pitch.analysisRuns(), 1);
        QCOMPARE(f.pitch.snapshot().value(QStringLiteral("startSample")).toString(), QStringLiteral("9000"));
        QCOMPARE(f.pitch.snapshot().value(QStringLiteral("endSample")).toString(), QStringLiteral("21000"));
        // A new voicing threshold is a new analysis.
        f.pitch.setVoicingThreshold(0.2);
        QTRY_COMPARE(f.pitch.state(), QStringLiteral("ready"));
        QCOMPARE(f.pitch.analysisRuns(), 2);
    }

    void editsRenderUndoAndReopen()
    {
        Fixture f;
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        const std::vector<float> before = render(f.session, f.dir.filePath(QStringLiteral("before.wav")));
        QCOMPARE(qint64(before.size()), qint64(48000));
        const QVariantMap made = f.editTo(5800.0);
        QVERIFY2(ok(made), qPrintable(made.value(QStringLiteral("error")).toString()));
        const QVariantMap edit = made.value(QStringLiteral("edit")).toMap();
        QCOMPARE(edit.value(QStringLiteral("operation")).toString(), QStringLiteral("pitch"));
        QCOMPARE(edit.value(QStringLiteral("startSample")).toString(), QStringLiteral("600"));
        QCOMPARE(edit.value(QStringLiteral("endSample")).toString(), QStringLiteral("24600"));
        QCOMPARE(edit.value(QStringLiteral("targetPitchCents")).toDouble(), 5800.0);

        AudioSourceEditModel *edits = f.session.edits();
        const QVariantMap applied = edits->apply(f.id, edit);
        QVERIFY2(ok(applied), qPrintable(applied.value(QStringLiteral("error")).toString()));
        QCOMPARE(edits->undoText(), QStringLiteral("Pitch to A#3"));
        // The old analysis is stale at once and refuses to make edits.
        QVERIFY(!f.pitch.isCurrent());
        QVERIFY(f.pitch.snapshot().value(QStringLiteral("stale")).toBool());
        QVERIFY(!ok(f.editTo(5900.0)));
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        QCOMPARE(f.pitch.analysisRuns(), 2);
        QVERIFY(std::fabs(pitchAt(f.pitch, 12000) - 5800.0) < 0.25);
        QVERIFY(std::fabs(pitchAt(f.pitch, 36000) - AudioPitch::hzToCents(330.0)) < 0.05);

        // The offline render carries the correction and nothing else.
        const std::vector<float> after = render(f.session, f.dir.filePath(QStringLiteral("after.wav")));
        QCOMPARE(after.size(), before.size());
        QVERIFY(worstError(after, 4800, 19200, 5800.0) < 0.25);
        QVERIFY(worstError(after, 28800, 45600, AudioPitch::hzToCents(330.0)) < 0.05);
        for (qint64 i = 0; i < 48000; ++i)
            if (i < 600 || i >= 24600) QCOMPARE(after[size_t(i)], before[size_t(i)]);

        QVERIFY(edits->undo());
        QCOMPARE(render(f.session, f.dir.filePath(QStringLiteral("undone.wav"))), before);
        QVERIFY(edits->redo());
        QCOMPARE(render(f.session, f.dir.filePath(QStringLiteral("redone.wav"))), after);

        // Saved parameters recompute the same samples on open.
        const QString file = f.dir.filePath(QStringLiteral("pitch.qaudio"));
        QVERIFY(f.session.save(file));
        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        const QString reopenedId = reopened.media().value(0).toMap().value(QStringLiteral("resourceId")).toString();
        QCOMPARE(reopenedId, f.id);
        const QVariantList list = reopened.edits()->edits(reopenedId);
        QCOMPARE(list.size(), 1);
        const QVariantMap saved = list.at(0).toMap();
        QCOMPARE(saved.value(QStringLiteral("operation")).toString(), QStringLiteral("pitch"));
        QCOMPARE(saved.value(QStringLiteral("targetPitchCents")).toDouble(), 5800.0);
        QCOMPARE(saved.value(QStringLiteral("correctionAmount")).toDouble(), 0.0);
        QCOMPARE(saved.value(QStringLiteral("formantRatio")).toDouble(), 1.0);
        QCOMPARE(render(reopened, f.dir.filePath(QStringLiteral("reopened.wav"))), after);
    }

    void previewsHoldTheAnalysisAndCommitOneStep()
    {
        Fixture f;
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        AudioSourceEditModel *edits = f.session.edits();
        QSignalSpy ready(edits, &AudioSourceEditModel::previewReady);
        const auto preview = [&](const QString &gesture, double cents) {
            return f.pitch.previewEdit(gesture, QStringLiteral("segment-600"), QStringLiteral("note-600"),
                                       QStringLiteral("pitch"), QStringLiteral("600"), QStringLiteral("600"), cents, 1.0);
        };
        QVERIFY(ok(preview(QStringLiteral("g1"), 5950.0)));
        QCOMPARE(edits->previewState(), QStringLiteral("computing"));
        QVERIFY(ready.wait(10000));
        // The previewed samples play; the analysis of the committed ones holds.
        QVERIFY(f.pitch.holding());
        QVERIFY(f.pitch.isCurrent());
        QTRY_COMPARE(f.pitch.state(), QStringLiteral("ready"));
        QCOMPARE(f.pitch.analysisRuns(), 1);
        QVERIFY(std::fabs(pitchAt(f.pitch, 12000) - 5700.0) < 0.05);
        const AudioBufferPtr previewed = edits->buffer(f.id);
        const auto heard = AudioPitch::analyse(previewed->channel(0), previewed->frames, Rate, 4800, 19200, {}, {});
        for (const AudioPitch::Frame &frame : heard->frames) QVERIFY(std::fabs(frame.cents - 5950.0) < 0.25);

        // The drag moves on: the next preview replaces it, still one analysis.
        QVERIFY(ok(preview(QStringLiteral("g1"), 5900.0)));
        QVERIFY(!f.pitch.holding());
        QVERIFY(ready.wait(10000));
        QCOMPARE(ready.count(), 2);
        QCOMPARE(f.pitch.analysisRuns(), 1);
        const QVariantMap committed = f.pitch.commitEdit(QStringLiteral("g1"), QStringLiteral("segment-600"),
                                                         QStringLiteral("note-600"), QStringLiteral("pitch"),
                                                         QStringLiteral("600"), QStringLiteral("600"), 5900.0, 1.0);
        QVERIFY2(ok(committed), qPrintable(committed.value(QStringLiteral("error")).toString()));
        QCOMPARE(edits->previewState(), QStringLiteral("committed"));
        QCOMPARE(edits->historyCount(), 1);
        QCOMPARE(edits->undoText(), QStringLiteral("Pitch to B3"));
        QVERIFY(!f.pitch.holding());
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        QCOMPARE(f.pitch.analysisRuns(), 2);
        QVERIFY(std::fabs(pitchAt(f.pitch, 12000) - 5900.0) < 0.25);

        // Cancel: back to the committed samples, no step, no new analysis.
        const QVariantMap note = f.pitch.noteAt(QStringLiteral("12000"));
        const AudioBufferPtr committedSamples = edits->buffer(f.id);
        QVERIFY(ok(f.pitch.previewEdit(QStringLiteral("g2"), note.value(QStringLiteral("segmentId")).toString(),
                                       note.value(QStringLiteral("id")).toString(), QStringLiteral("formant"),
                                       note.value(QStringLiteral("startSample")).toString(),
                                       note.value(QStringLiteral("startSample")).toString(), 0.0, 1.2)));
        QVERIFY(ready.wait(10000));
        QVERIFY(edits->buffer(f.id) != committedSamples);
        QVERIFY(f.pitch.cancelEdit(QStringLiteral("g2")));
        QVERIFY(edits->buffer(f.id) == committedSamples);
        QCOMPARE(edits->historyCount(), 1);
        QVERIFY(f.pitch.isCurrent());
        QTRY_COMPARE(f.pitch.state(), QStringLiteral("ready"));
        QCOMPARE(f.pitch.analysisRuns(), 2);
        // A drag back onto the note's own pitch previews nothing.
        QVERIFY(f.pitch.previewEdit(QStringLiteral("g3"), note.value(QStringLiteral("segmentId")).toString(),
                                    note.value(QStringLiteral("id")).toString(), QStringLiteral("pitch"),
                                    note.value(QStringLiteral("startSample")).toString(),
                                    note.value(QStringLiteral("startSample")).toString(),
                                    note.value(QStringLiteral("pitchCents")).toDouble(), 1.0)
                    .value(QStringLiteral("unchanged")).toBool());
    }

    void aStaleAnalysisIsNeverPublished()
    {
        Fixture f;
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        AudioSourceEditModel *edits = f.session.edits();
        const QVariantMap quieter{{QStringLiteral("operation"), QStringLiteral("gain")},
                                  {QStringLiteral("startSample"), QStringLiteral("0")},
                                  {QStringLiteral("endSample"), QStringLiteral("48000")},
                                  {QStringLiteral("gainDb"), -6.0}};
        // Hold every pool thread so the analysis started for the first edit
        // is still queued when the second edit lands.
        QThreadPool *pool = QThreadPool::globalInstance();
        const int threads = pool->maxThreadCount();
        QSemaphore gate;
        QSemaphore started;
        for (int i = 0; i < threads; ++i)
            pool->start([&gate, &started] {
                started.release();
                gate.acquire();
            });
        started.acquire(threads);
        QVERIFY(ok(edits->apply(f.id, quieter)));
        QCOMPARE(f.pitch.state(), QStringLiteral("loading"));
        QVERIFY(!ok(f.editTo(5800.0))); // stale: refused
        QCoreApplication::processEvents(); // the new analysis is queued on the pool
        QVERIFY(ok(edits->apply(f.id, {{QStringLiteral("operation"), QStringLiteral("pitch")},
                                       {QStringLiteral("startSample"), QStringLiteral("600")},
                                       {QStringLiteral("endSample"), QStringLiteral("24600")},
                                       {QStringLiteral("targetPitchCents"), 5800.0}})));
        gate.release(threads);
        QTRY_VERIFY(f.pitch.isCurrent() && f.pitch.state() == QLatin1String("ready"));
        // Only the analysis of the newest samples was published.
        QCOMPARE(f.pitch.analysisRuns(), 2);
        QCOMPARE(f.pitch.snapshot().value(QStringLiteral("revision")).toString(), edits->revision(f.id));
        QVERIFY(std::fabs(pitchAt(f.pitch, 12000) - 5800.0) < 0.25);
        // cancel() stops a request; refresh() runs it again.
        f.pitch.setVoicingThreshold(0.14);
        f.pitch.cancel();
        QCOMPARE(f.pitch.state(), QStringLiteral("canceled"));
        f.pitch.refresh();
        QTRY_COMPARE(f.pitch.state(), QStringLiteral("ready"));
        QCOMPARE(f.pitch.analysisRuns(), 3);
    }

    void testSignalHasTheNotesItNames()
    {
        QTemporaryDir dir;
        AudioPitchSource pitch;
        const QString path = dir.filePath(QStringLiteral("signal.wav"));
        const QVariantMap written = pitch.writeTestSignal(
            QUrl::fromLocalFile(path).toString(),
            {QVariantMap{{QStringLiteral("pitchCents"), 6900.0}, {QStringLiteral("seconds"), 0.5}},
             QVariantMap{{QStringLiteral("seconds"), 0.1}},
             QVariantMap{{QStringLiteral("pitchCents"), 6400.0}, {QStringLiteral("seconds"), 0.5}}});
        QVERIFY2(ok(written), qPrintable(written.value(QStringLiteral("error")).toString()));
        QCOMPARE(written.value(QStringLiteral("frames")).toString(), QStringLiteral("52800"));
        const AudioBufferPtr samples = AudioFile::readWav(path);
        QVERIFY(samples && samples->frames == 52800);
        const auto a = AudioPitch::analyse(samples->channel(0), samples->frames, Rate, 0, samples->frames, {}, {});
        QCOMPARE(a->segments.size(), size_t(2));
        QCOMPARE(a->notes.size(), size_t(2));
        QVERIFY(std::fabs(a->notes[0].cents - 6900.0) < 0.1);
        QVERIFY(std::fabs(a->notes[1].cents - 6400.0) < 0.1);
        QVERIFY(!ok(pitch.writeTestSignal(path, {QVariantMap{{QStringLiteral("seconds"), -1.0}}})));
    }

    void pitchEditorDrivesTheHost()
    {
        QTemporaryDir dir;
        const QString wav = writeLegato(dir.filePath(QStringLiteral("legato.wav")));
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(&engine);
        component.setData(R"qml(
import QtQuick
import QindaTK as Tk
Item {
    id: root
    width: 500; height: 300
    property string wavPath
    property string resourceId
    property alias session: audio
    property alias pitch: analysis
    property alias editor: editor
    Tk.AudioSession { id: audio }
    Tk.AudioPitchSource { id: analysis; session: audio; resourceId: root.resourceId; columnCount: 250; pitchSnapCents: 100 }
    Tk.PitchEditor {
        id: editor
        width: 500; height: 300
        sourceId: root.resourceId
        analysisSnapshot: analysis.snapshot
        editingEnabled: analysis.current
        onEditPreviewRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
            analysis.previewEdit(gesture, segment, note, operation, anchor, target, cents, ratio)
        }
        onEditCommitRequested: function(gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) {
            analysis.commitEdit(gesture, segment, note, operation, anchor, target, cents, ratio)
        }
        onGestureCanceled: function(gesture) { analysis.cancelEdit(gesture) }
    }
    Component.onCompleted: {
        const track = audio.addTrack("Vocal")
        root.resourceId = audio.importMedia(root.wavPath, track, "0").resourceId
    }
}
)qml", QUrl());
        std::unique_ptr<QObject> root(component.createWithInitialProperties({{QStringLiteral("wavPath"), wav}}));
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *pitch = qobject_cast<AudioPitchSource *>(root->property("pitch").value<QObject *>());
        auto *session = qobject_cast<AudioSession *>(root->property("session").value<QObject *>());
        QObject *editor = root->property("editor").value<QObject *>();
        QVERIFY(pitch && session && editor);
        QTRY_VERIFY(pitch->isCurrent() && pitch->state() == QLatin1String("ready"));
        QTRY_VERIFY(editor->property("analysisAvailable").toBool());
        // y for B3 (5900 cents) on the editor's pitch axis; the host snaps to semitones.
        const QVariantMap snapshot = pitch->snapshot();
        const double low = snapshot.value(QStringLiteral("minimumPitchCents")).toDouble();
        const double high = snapshot.value(QStringLiteral("maximumPitchCents")).toDouble();
        const double y = 300.0 * (high - 5900.0) / (high - low);
        QVariant began;
        QVERIFY(QMetaObject::invokeMethod(editor, "beginEdit", Q_RETURN_ARG(QVariant, began),
                                          Q_ARG(QVariant, QStringLiteral("segment-600")),
                                          Q_ARG(QVariant, QStringLiteral("note-600")),
                                          Q_ARG(QVariant, QStringLiteral("pitch")), Q_ARG(QVariant, 100.0), Q_ARG(QVariant, y)));
        QVERIFY(began.toBool());
        QVariant finished;
        QVERIFY(QMetaObject::invokeMethod(editor, "finishEdit", Q_RETURN_ARG(QVariant, finished)));
        QVERIFY(finished.toBool());
        const QString id = root->property("resourceId").toString();
        const QVariantList list = session->edits()->edits(id);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("targetPitchCents")).toDouble(), 5900.0);
        QTRY_VERIFY(pitch->isCurrent() && pitch->state() == QLatin1String("ready"));
        QVERIFY(std::fabs(pitchAt(*pitch, 12000) - 5900.0) < 0.25);
    }

    void playbackAllocatesNothingWhileAPitchEditLands()
    {
        QTemporaryDir dir;
        AudioEngine engine;
        AudioSession session;
        session.setMediaDirectory(dir.filePath(QStringLiteral("takes")));
        session.setEngine(&engine);
        QSignalSpy applied(&engine, &AudioEngine::configurationApplied);
        engine.applyConfiguration(QStringLiteral("test"), {{QStringLiteral("deviceId"), QStringLiteral("builtin:null")},
                                                           {QStringLiteral("sampleRate"), Rate},
                                                           {QStringLiteral("bufferSize"), 256},
                                                           {QStringLiteral("inputLayout"), QStringLiteral("None")},
                                                           {QStringLiteral("outputLayout"), QStringLiteral("Stereo")}});
        QVERIFY(!applied.isEmpty() || applied.wait(5000));
        QCOMPARE(applied.last().at(1).toString(), QStringLiteral("accepted"));
        const QString track = session.addTrack(QStringLiteral("Vocal"));
        const QString id = session.importMedia(writeLegato(dir.filePath(QStringLiteral("legato.wav"))), track)
                               .value(QStringLiteral("resourceId")).toString();
        TransportModel *transport = engine.transport();
        QVERIFY(QTest::qWaitFor([transport] { return transport->canPlay(); }, 5000));
        transport->requestLoop(true, QStringLiteral("0"), QStringLiteral("48000"));
        transport->requestPlay();
        QVERIFY(QTest::qWaitFor([transport] { return transport->playing(); }, 5000));

        // AGENT-GUARD: pitch DSP runs on the GUI thread or the pool; the
        // device thread only adopts the recompiled graph.
        g_probeThread.store(engine.realtimeThreadToken());
        g_deviceAllocations.store(0);
        g_probing.store(true);
        QTest::qWait(100);
        QVERIFY(ok(session.edits()->apply(id, {{QStringLiteral("operation"), QStringLiteral("pitch")},
                                               {QStringLiteral("startSample"), QStringLiteral("600")},
                                               {QStringLiteral("endSample"), QStringLiteral("24600")},
                                               {QStringLiteral("targetPitchCents"), 5800.0}})));
        QTest::qWait(150);
        QVERIFY(session.edits()->undo());
        QTest::qWait(150);
        g_probing.store(false);
#if defined(QINDATK_ALLOCATION_PROBE)
        QCOMPARE(g_deviceAllocations.load(), 0);
#endif
        transport->requestStop();
    }
};

QTEST_MAIN(TestAudioPitchSession)
#include "tst_audio_pitch_session.moc"
