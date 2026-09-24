// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-060 session and offline-render fixtures: WAV codec, resampling, exact
// samples after timeline edits and automation, and save/reopen/relink.
#include "audio_session.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cmath>
#include <numbers>

using namespace QindaTK;

namespace {

constexpr int Rate = 48000;
constexpr qint64 TicksPerSample = TimelineTime::kDefaultTicksPerSecond / Rate; // 14700, exact

qint64 ticks(qint64 samples) { return samples * TicksPerSample; }
QString text(qint64 value) { return QString::number(value); }
float ramp(qint64 frame) { return float(frame + 1) / 65536.0f; } // exact in float32

QString writeRamp(const QString &path, qint64 frames)
{
    AudioBuffer buffer;
    buffer.sampleRate = Rate;
    buffer.channels = 1;
    buffer.frames = frames;
    buffer.samples.resize(size_t(frames));
    for (qint64 i = 0; i < frames; ++i) buffer.samples[size_t(i)] = ramp(i);
    return AudioFile::writeWav(path, buffer) ? path : QString();
}

std::vector<float> readMono(const QString &path)
{
    const AudioBufferPtr buffer = AudioFile::readWav(path);
    return buffer ? std::vector<float>(buffer->channel(0), buffer->channel(0) + buffer->frames) : std::vector<float>{};
}

// Hand-built integer PCM WAV (the reference writer only emits float32).
QByteArray pcmWav(int bits, const QList<qint32> &values)
{
    const int width = bits / 8;
    QByteArray data;
    for (qint32 v : values)
        for (int b = 0; b < width; ++b) data.append(char((v >> (8 * b)) & 0xff));
    QByteArray out("RIFF\0\0\0\0WAVEfmt ", 16);
    char field[4];
    const auto u32 = [&](quint32 x) { qToLittleEndian(x, field); out.append(field, 4); };
    const auto u16 = [&](quint16 x) { qToLittleEndian(x, field); out.append(field, 2); };
    u32(16); u16(1); u16(1); u32(Rate); u32(Rate * width); u16(quint16(width)); u16(quint16(bits));
    out.append("data", 4);
    u32(quint32(data.size()));
    out.append(data);
    qToLittleEndian<quint32>(quint32(out.size() - 8), out.data() + 4);
    return out;
}

} // namespace

class TestAudioSession : public QObject {
    Q_OBJECT
private slots:
    void wavCodecAndResampler()
    {
        QTemporaryDir dir;
        const QString pcm16 = dir.filePath(QStringLiteral("pcm16.wav"));
        const QString pcm24 = dir.filePath(QStringLiteral("pcm24.wav"));
        QFile f16(pcm16), f24(pcm24);
        QVERIFY(f16.open(QIODevice::WriteOnly) && f16.write(pcmWav(16, {0, 16384, -32768, 32767})) > 0);
        QVERIFY(f24.open(QIODevice::WriteOnly) && f24.write(pcmWav(24, {-8388608, 4194304})) > 0);
        f16.close();
        f24.close();
        QCOMPARE(readMono(pcm16), (std::vector<float>{0.0f, 0.5f, -1.0f, 32767.0f / 32768.0f}));
        QCOMPARE(readMono(pcm24), (std::vector<float>{-1.0f, 0.5f}));
        QString error;
        QVERIFY(!AudioFile::readWav(dir.filePath(QStringLiteral("missing.wav")), &error));
        QVERIFY(!error.isEmpty());

        // 1 kHz at 48 kHz converted to 44.1 kHz keeps duration and pitch.
        AudioBuffer tone;
        tone.sampleRate = Rate;
        tone.channels = 1;
        tone.frames = 4800;
        tone.samples.resize(4800);
        for (int i = 0; i < 4800; ++i) tone.samples[size_t(i)] = float(std::sin(2 * std::numbers::pi * 1000.0 * i / Rate));
        const AudioBufferPtr converted = AudioFile::resample(tone, 44100);
        QCOMPARE(converted->frames, qint64(4410));
        double worst = 0.0;
        for (int n = 200; n < 4210; ++n)
            worst = std::max(worst, std::abs(converted->channel(0)[n] - std::sin(2 * std::numbers::pi * 1000.0 * n / 44100.0)));
        qInfo("resampler 48k->44.1k 1 kHz max interior error %.3g", worst);
        QVERIFY2(worst < 2e-3, qPrintable(QString::number(worst)));
    }

    void offlineRenderFollowsEditsAndAutomation()
    {
        QTemporaryDir dir;
        AudioSession session;
        const QString track = session.addTrack(QStringLiteral("Ramp"));
        QVERIFY(!track.isEmpty());
        const QVariantMap imported = session.importMedia(writeRamp(dir.filePath(QStringLiteral("ramp.wav")), 24000), track);
        QVERIFY2(imported.value(QStringLiteral("ok")).toBool(), qPrintable(session.lastError()));
        const QUuid clip = QUuid::fromString(imported.value(QStringLiteral("clipId")).toString());
        TimelineModel *timeline = session.timeline();

        // Split at 6000, move the right part to 12000, trim the left part to 3000.
        TimelineEditRequest split;
        split.kind = TimelineEditKind::Split;
        split.clipIds = {clip};
        TimelineEditInput at;
        at.snap = false;
        at.pointerTicks = ticks(6000);
        QVERIFY(!timeline->apply(split, at));
        const QUuid right = QUuid::fromString(timeline->clipAt(track, text(ticks(6000))));
        QVERIFY(!right.isNull() && right != clip);
        TimelineEditRequest move;
        move.kind = TimelineEditKind::Move;
        move.clipIds = {right};
        move.grabTicks = ticks(6000);
        at.pointerTicks = ticks(12000);
        QVERIFY(!timeline->apply(move, at));
        TimelineEditRequest trim;
        trim.kind = TimelineEditKind::SetClipTiming;
        trim.clipIds = {clip};
        trim.duration = ticks(3000);
        QVERIFY(!timeline->apply(trim, {}));

        // Track gain automation in dB: -12 dB at 12000 rising linearly to 0 dB at 18000.
        const QString gain = session.ensureGainParameter(track);
        for (const auto &[sample, value] : {std::pair{12000, -12.0}, std::pair{18000, 0.0}})
            QVERIFY(session.automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                                                     {QStringLiteral("parameterId"), gain},
                                                     {QStringLiteral("ticks"), text(ticks(sample))},
                                                     {QStringLiteral("value"), value}}).value(QStringLiteral("ok")).toBool());
        const auto expected = [&](qint64 sample, qint64 source) {
            const double db = session.automation()->valueAt(gain, text(ticks(sample)));
            return float(double(ramp(source)) * std::pow(10.0, db / 20.0));
        };

        const QString out = dir.filePath(QStringLiteral("render.wav"));
        const QVariantMap rendered = session.renderToFile(out, QStringLiteral("0"), text(ticks(30000)), Rate, 1);
        QVERIFY2(rendered.value(QStringLiteral("ok")).toBool(), qPrintable(session.lastError()));
        const std::vector<float> samples = readMono(out);
        QCOMPARE(qint64(samples.size()), qint64(30000));
        int mismatches = 0;
        for (qint64 i = 0; i < 30000; ++i) {
            const float want = i < 3000 ? expected(i, i) : i < 12000 ? 0.0f : expected(i, 6000 + (i - 12000));
            if (samples[size_t(i)] != want && ++mismatches < 4)
                qWarning("sample %lld: %.9g != %.9g", i, samples[size_t(i)], want);
        }
        QCOMPARE(mismatches, 0);

        // Undoing the trim brings frames 3000..5999 back, exactly.
        QVERIFY(timeline->undo());
        QVERIFY(session.renderToFile(out, QStringLiteral("0"), text(ticks(6000)), Rate, 1).value(QStringLiteral("ok")).toBool());
        const std::vector<float> undone = readMono(out);
        QCOMPARE(qint64(undone.size()), qint64(6000));
        for (qint64 i = 3000; i < 6000; i += 997) QCOMPARE(undone[size_t(i)], expected(i, i));
    }

    void saveReopenRelinkAndRender()
    {
        QTemporaryDir dir;
        const QString media = writeRamp(dir.filePath(QStringLiteral("ramp.wav")), 9600);
        const QString file = dir.filePath(QStringLiteral("session.qtkaudio"));
        const QString first = dir.filePath(QStringLiteral("first.wav"));
        QString resource;
        {
            AudioSession session;
            const QString track = session.addTrack(QStringLiteral("Voice"));
            const QVariantMap imported = session.importMedia(media, track, text(ticks(1000)));
            QVERIFY(imported.value(QStringLiteral("ok")).toBool());
            resource = imported.value(QStringLiteral("resourceId")).toString();
            const QString gain = session.ensureGainParameter(track);
            QVERIFY(session.automation()->applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")},
                                                     {QStringLiteral("parameterId"), gain},
                                                     {QStringLiteral("ticks"), QStringLiteral("0")},
                                                     {QStringLiteral("value"), -6.0}}).value(QStringLiteral("ok")).toBool());
            QVERIFY2(session.save(file), qPrintable(session.lastError()));
            QVERIFY(session.renderToFile(first, QStringLiteral("0"), {}, Rate, 1).value(QStringLiteral("ok")).toBool());
        }
        QFile saved(file);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(saved.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("schema")).toString(), AudioSession::schemaId());
        QCOMPARE(root.value(QStringLiteral("media")).toArray().at(0).toObject().value(QStringLiteral("uri")).toString(),
                 QStringLiteral("ramp.wav")); // relative to the session

        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("moved")));
        const QString moved = dir.filePath(QStringLiteral("moved/ramp.wav"));
        QVERIFY(QFile::rename(media, moved));

        AudioSession reopened;
        QVERIFY2(reopened.open(file), qPrintable(reopened.lastError()));
        QCOMPARE(reopened.timeline()->clipCount(), 1);
        QCOMPARE(reopened.missingMedia(), QStringList{resource});
        const QString silent = dir.filePath(QStringLiteral("silent.wav"));
        const QVariantMap missingRender = reopened.renderToFile(silent, QStringLiteral("0"), text(ticks(10600)), Rate, 1);
        QCOMPARE(missingRender.value(QStringLiteral("missingMedia")).toStringList(), QStringList{resource});
        for (float v : readMono(silent)) QCOMPARE(v, 0.0f);

        QVERIFY(!reopened.relinkMedia(resource, dir.filePath(QStringLiteral("nowhere.wav"))));
        QVERIFY2(reopened.relinkMedia(resource, moved), qPrintable(reopened.lastError()));
        QVERIFY(reopened.missingMedia().isEmpty());
        const QString second = dir.filePath(QStringLiteral("second.wav"));
        QVERIFY(reopened.renderToFile(second, QStringLiteral("0"), {}, Rate, 1).value(QStringLiteral("ok")).toBool());
        const std::vector<float> a = readMono(first), b = readMono(second);
        QCOMPARE(qint64(a.size()), qint64(10600));
        QVERIFY(a == b);
        QCOMPARE(a[999], 0.0f);
        QCOMPARE(a[1000], float(double(ramp(0)) * std::pow(10.0, -6.0 / 20.0)));
    }
};

QTEST_MAIN(TestAudioSession)
#include "tst_audio_session.moc"
