// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/render_queue_model.h"
#include "../../src/core/rich_text_document.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class RenderQueueModelTest : public QObject {
    Q_OBJECT
private slots:
    void pcm16WavGainOutput()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QByteArray wav(52, '\0');
        wav.replace(0, 4, "RIFF"); wav.replace(8, 4, "WAVE");
        wav.replace(12, 4, "fmt "); wav.replace(36, 4, "data");
        auto put16 = [&](int at, quint16 value) {
            wav[at] = char(value & 255); wav[at + 1] = char((value >> 8) & 255);
        };
        auto put32 = [&](int at, quint32 value) {
            put16(at, quint16(value & 65535)); put16(at + 2, quint16(value >> 16));
        };
        put32(4, 44); put32(16, 16); put16(20, 1); put16(22, 1);
        put32(24, 48000); put32(28, 96000); put16(32, 2); put16(34, 16);
        put32(40, 8); put16(44, 1000); put16(46, quint16(-1000));
        put16(48, 20000); put16(50, quint16(-20000));
        const QString sourcePath = temp.filePath(QStringLiteral("source.wav"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(wav), wav.size());
        source.close();
        const QString digest = QString::fromLatin1(QCryptographicHash::hash(
            wav, QCryptographicHash::Sha256).toHex());
        RenderQueueModel queue;
        QVERIFY(queue.open(temp.filePath(QStringLiteral("queue.json"))));
        const QString output = temp.filePath(QStringLiteral("wav-output"));
        const QVariantMap snapshot{{QStringLiteral("id"), QStringLiteral("wav-job")},
            {QStringLiteral("label"), QStringLiteral("Audio gain")},
            {QStringLiteral("providerId"), QStringLiteral("qindatk.pcm16-wav-gain")},
            {QStringLiteral("providerVersion"), QStringLiteral("1")},
            {QStringLiteral("format"), QStringLiteral("wav")},
            {QStringLiteral("capabilityId"), QStringLiteral("pcm16-wav-gain")},
            {QStringLiteral("sourceId"), QStringLiteral("audio-a")},
            {QStringLiteral("sourceRevision"), QStringLiteral("sha256:") + digest},
            {QStringLiteral("outputPath"), output},
            {QStringLiteral("resourceSlots"), 1},
            {QStringLiteral("settings"), QVariantMap{{QStringLiteral("sourcePath"), sourcePath},
                {QStringLiteral("sourceSha256"), digest}, {QStringLiteral("gain"), 2.0}}}};
        QCOMPARE(queue.enqueueJob(snapshot), QStringLiteral("wav-job"));
        QTRY_COMPARE_WITH_TIMEOUT(queue.job(QStringLiteral("wav-job")).value(QStringLiteral("state")).toString(),
                                  QStringLiteral("succeeded"), 20000);
        QVERIFY(queue.verifyOutput(QStringLiteral("wav-job")));
        QFile rendered(QDir(output).filePath(QStringLiteral("audio.wav")));
        QVERIFY(rendered.open(QIODevice::ReadOnly));
        const QByteArray result = rendered.readAll();
        QCOMPARE(result.size(), wav.size());
        auto sample = [&](int at) {
            const int value = int(quint8(result.at(at))) | (int(quint8(result.at(at + 1))) << 8);
            return value >= 32768 ? value - 65536 : value;
        };
        QCOMPARE(sample(44), 2000);
        QCOMPARE(sample(46), -2000);
        QCOMPARE(sample(48), 32767);
        QCOMPARE(sample(50), -32768);
    }

    void richDocumentPdfOutput()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        RichTextDocument document;
        QVERIFY(document.insertText(document.startPosition(), QStringLiteral("Queued PDF output"))
                    .has_value());
        const QString json = document.toJson();
        const QString digest = QString::fromLatin1(QCryptographicHash::hash(
            json.toUtf8(), QCryptographicHash::Sha256).toHex());
        RenderQueueModel queue;
        QVERIFY(queue.open(temp.filePath(QStringLiteral("queue.json"))));
        const QString output = temp.filePath(QStringLiteral("pdf-output"));
        const QVariantMap snapshot{{QStringLiteral("id"), QStringLiteral("pdf-job")},
            {QStringLiteral("label"), QStringLiteral("Rich document")},
            {QStringLiteral("providerId"), QStringLiteral("qindatk.rich-document-pdf")},
            {QStringLiteral("providerVersion"), QStringLiteral("1")},
            {QStringLiteral("format"), QStringLiteral("pdf")},
            {QStringLiteral("capabilityId"), QStringLiteral("rich-document-pdf")},
            {QStringLiteral("sourceId"), QStringLiteral("document-a")},
            {QStringLiteral("sourceRevision"), QStringLiteral("sha256:") + digest},
            {QStringLiteral("outputPath"), output},
            {QStringLiteral("resourceSlots"), 1},
            {QStringLiteral("settings"), QVariantMap{{QStringLiteral("documentJson"), json},
                {QStringLiteral("documentSha256"), digest}}}};
        QCOMPARE(queue.enqueueJob(snapshot), QStringLiteral("pdf-job"));
        QTRY_COMPARE_WITH_TIMEOUT(queue.job(QStringLiteral("pdf-job")).value(QStringLiteral("state")).toString(),
                                  QStringLiteral("succeeded"), 20000);
        QVERIFY(queue.verifyOutput(QStringLiteral("pdf-job")));
        QFile pdf(QDir(output).filePath(QStringLiteral("document.pdf")));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QCOMPARE(pdf.read(5), QByteArray("%PDF-"));
    }

    void durableCaptionSequenceAndRetry()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString source = temp.filePath(QStringLiteral("source.png"));
        QImage frame(320, 180, QImage::Format_ARGB32_Premultiplied);
        frame.fill(QColor(28, 65, 82));
        QVERIFY(frame.save(source, "PNG"));
        QFile sourceFile(source);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        const QByteArray original = sourceFile.readAll();
        sourceFile.close();
        const QString digest = QString::fromLatin1(QCryptographicHash::hash(
            original, QCryptographicHash::Sha256).toHex());
        const QString track = QStringLiteral(R"JSON({"version":1,"trackId":"queue","language":"en","cues":[
            {"id":"cue-a","inMs":"100","outMs":"500","text":"Queued caption","language":"en","styleId":"default","lane":0}]})JSON");
        const QString journal = temp.filePath(QStringLiteral("queue.json"));
        const QString output = temp.filePath(QStringLiteral("rendered"));
        QVariantMap snapshot{{QStringLiteral("id"), QStringLiteral("stable-job")},
            {QStringLiteral("label"), QStringLiteral("Caption sequence")},
            {QStringLiteral("providerId"), QStringLiteral("qindatk.caption-png-sequence")},
            {QStringLiteral("providerVersion"), QStringLiteral("1")},
            {QStringLiteral("format"), QStringLiteral("png-sequence")},
            {QStringLiteral("capabilityId"), QStringLiteral("caption-burn-in")},
            {QStringLiteral("sourceId"), QStringLiteral("scene-7")},
            {QStringLiteral("sourceRevision"), QStringLiteral("sha256:track-1")},
            {QStringLiteral("outputPath"), output},
            {QStringLiteral("settings"), QVariantMap{{QStringLiteral("captionTrackJson"), track},
                {QStringLiteral("frames"), QVariantList{QVariantMap{
                    {QStringLiteral("path"), source}, {QStringLiteral("sha256"), digest},
                    {QStringLiteral("timeMs"), QStringLiteral("250")}}}}}}};
        {
            RenderQueueModel queue;
            QVERIFY(queue.open(journal));
            QCOMPARE(queue.enqueueJob(snapshot), QStringLiteral("stable-job"));
            QTRY_COMPARE_WITH_TIMEOUT(queue.job(QStringLiteral("stable-job")).value(QStringLiteral("state")).toString(),
                                      QStringLiteral("succeeded"), 20000);
            QVERIFY(queue.verifyOutput(QStringLiteral("stable-job")));
            QVERIFY(!queue.retryJob(QStringLiteral("stable-job")));
            QImage rendered(QDir(output).filePath(QStringLiteral("frame-000000.png")), "PNG");
            QCOMPARE(rendered.size(), frame.size());
            QVERIFY(rendered != frame);
            QFile manifestFile(QDir(output).filePath(QStringLiteral("render-manifest.json")));
            QVERIFY(manifestFile.open(QIODevice::ReadOnly));
            const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
            QCOMPARE(manifest.value(QStringLiteral("jobId")).toString(), QStringLiteral("stable-job"));
            QCOMPARE(manifest.value(QStringLiteral("files")).toArray().size(), 1);
        }
        // Simulate a process exit after atomic publish but before journal completion.
        QFile journalFile(journal);
        QVERIFY(journalFile.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(journalFile.readAll()).object();
        journalFile.close();
        QJsonArray rows = root.value(QStringLiteral("jobs")).toArray();
        QJsonObject row = rows.at(0).toObject();
        row.insert(QStringLiteral("state"), QStringLiteral("running"));
        rows.replace(0, row); root.insert(QStringLiteral("jobs"), rows);
        QSaveFile save(journal);
        QVERIFY(save.open(QIODevice::WriteOnly));
        QVERIFY(save.write(QJsonDocument(root).toJson()) > 0);
        QVERIFY(save.commit());
        {
            RenderQueueModel recovered;
            QVERIFY(recovered.open(journal));
            QCOMPARE(recovered.job(QStringLiteral("stable-job")).value(QStringLiteral("state")).toString(),
                     QStringLiteral("succeeded"));
            QVERIFY(recovered.verifyOutput(QStringLiteral("stable-job")));
            QVariantMap failed = snapshot;
            failed.insert(QStringLiteral("id"), QStringLiteral("retry-job"));
            failed.insert(QStringLiteral("outputPath"), temp.filePath(QStringLiteral("retry-output")));
            QFile::remove(source);
            QCOMPARE(recovered.enqueueJob(failed), QStringLiteral("retry-job"));
            QTRY_COMPARE_WITH_TIMEOUT(recovered.job(QStringLiteral("retry-job")).value(QStringLiteral("state")).toString(),
                                      QStringLiteral("failed"), 20000);
        }
        // Simulate an unfinished retry with a partial private staging directory.
        QFile journalAgain(journal);
        QVERIFY(journalAgain.open(QIODevice::ReadOnly));
        root = QJsonDocument::fromJson(journalAgain.readAll()).object();
        journalAgain.close();
        rows = root.value(QStringLiteral("jobs")).toArray();
        row = rows.at(1).toObject();
        row.insert(QStringLiteral("state"), QStringLiteral("running"));
        rows.replace(1, row); root.insert(QStringLiteral("jobs"), rows);
        const QString stage = temp.filePath(QStringLiteral(".retry-output.pending-retry-job"));
        QVERIFY(QDir().mkpath(stage));
        QFile partial(QDir(stage).filePath(QStringLiteral("frame-000000.png")));
        QVERIFY(partial.open(QIODevice::WriteOnly));
        QVERIFY(partial.write("partial") > 0);
        partial.close();
        QSaveFile interruptedSave(journal);
        QVERIFY(interruptedSave.open(QIODevice::WriteOnly));
        QVERIFY(interruptedSave.write(QJsonDocument(root).toJson()) > 0);
        QVERIFY(interruptedSave.commit());
        {
            RenderQueueModel recovered;
            QVERIFY(recovered.open(journal));
            QCOMPARE(recovered.job(QStringLiteral("retry-job")).value(QStringLiteral("state")).toString(),
                     QStringLiteral("interrupted"));
            QVERIFY(!QFileInfo::exists(stage));
            QFile restore(source);
            QVERIFY(restore.open(QIODevice::WriteOnly));
            QCOMPARE(restore.write(original), original.size());
            restore.close();
            QVERIFY(recovered.retryJob(QStringLiteral("retry-job")));
            QTRY_COMPARE_WITH_TIMEOUT(recovered.job(QStringLiteral("retry-job")).value(QStringLiteral("state")).toString(),
                                      QStringLiteral("succeeded"), 20000);
            QVERIFY(recovered.verifyOutput(QStringLiteral("retry-job")));
        }
    }
};
QTEST_MAIN(RenderQueueModelTest)
#include "tst_render_queue_model.moc"
