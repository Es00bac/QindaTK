// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-075 result integrity: long tracks reopen, undo keeps unaccepted
// analysis results, replaced frames invalidate finished results, a cancelled
// job's late samples never reach a new job, and generated mask keys stay
// marked through edits, undo and reopen.
#include "../../src/core/tracking_roto_model.h"

#include <QColorSpace>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using namespace QindaTK;

class TrackingRotoResultsTest final : public QObject {
    Q_OBJECT
private:
    static QImage patterned(int dx, int dy, bool occluded = false)
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        image.fill(QColor(16, 16, 16));
        if (!occluded) {
            for (int y = -5; y <= 5; ++y)
                for (int x = -5; x <= 5; ++x) {
                    const int value = 55 + ((x + 5) * 17 + (y + 5) * 29) % 175;
                    image.setPixelColor(20 + dx + x, 20 + dy + y, QColor(value, value, value));
                }
        }
        return image;
    }
    // 2 px right and 1 px down per frame; frames 3 and 4 occluded.
    static void loadOccludedRun(TrackingRotoModel &model)
    {
        for (int k = 0; k <= 6; ++k)
            QVERIFY(model.setFrame(k, patterned(2 * k, k, k == 3 || k == 4)));
    }
    static QVariantMap track(const TrackingRotoModel &model, int index = 0)
    {
        return model.tracks().at(index).toMap();
    }
    static int count(const TrackingRotoModel &model, const char *key, int index = 0)
    {
        return int(track(model, index).value(QLatin1String(key)).toList().size());
    }
    static QString state(const TrackingRotoModel &model, int index = 0)
    {
        return track(model, index).value(QStringLiteral("state")).toString();
    }
    static QVariantList square(double left, double top, double size = 10)
    {
        QVariantList points;
        const double xs[] = {left, left + size, left + size, left};
        const double ys[] = {top, top, top + size, top + size};
        for (int i = 0; i < 4; ++i)
            points.append(QVariantMap{{QStringLiteral("pointId"), QStringLiteral("p%1").arg(i)},
                                      {QStringLiteral("x"), xs[i] / 64.0}, {QStringLiteral("y"), ys[i] / 64.0}});
        return points;
    }
    static QVariantMap keyAt(const TrackingRotoModel &model, int frame)
    {
        for (const QVariant &key : model.masks().first().toMap().value(QStringLiteral("keyframes")).toList())
            if (key.toMap().value(QStringLiteral("frame")).toInt() == frame) return key.toMap();
        return {};
    }

private slots:
    void longTrackSavesReopensAndIsCapped()
    {
        TrackingRotoModel model;
        const QImage still = patterned(0, 0);
        for (int k = 0; k <= 300; ++k) QVERIFY(model.setFrame(k, still));
        const QString id = model.addTrack(QStringLiteral("long"), 0, 20, 20, 5, 3);
        // One allowed 300-frame job plus the seed gives 301 accepted points.
        QVERIFY(model.startAnalysis(id, 1, 300));
        QTRY_VERIFY_WITH_TIMEOUT(!model.analysisRunning(), 20000);
        QVERIFY(model.acceptAnalysis(id));
        QCOMPARE(count(model, "points"), 301);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl url = QUrl::fromLocalFile(directory.filePath(QStringLiteral("long.json")));
        QVERIFY2(model.saveToFile(url), qPrintable(model.lastError()));
        TrackingRotoModel reopened;
        QVERIFY2(reopened.loadFromFile(url), qPrintable(reopened.lastError()));
        QCOMPARE(count(reopened, "points"), 301);

        // The per-track cap is the loader's limit: editing refuses to pass it,
        // an existing frame stays editable, and a capped track still reopens.
        // (Frames 301.. are new; counting once keeps the loop linear.)
        for (int frame = 301; frame < TrackingRotoModel::kMaxTrackPoints; ++frame)
            QVERIFY(model.correctPoint(id, frame, 20, 20));
        QCOMPARE(count(model, "points"), TrackingRotoModel::kMaxTrackPoints);
        QVERIFY(!model.correctPoint(id, 9000, 20, 20));
        QVERIFY(model.lastError().contains(QStringLiteral("at most")));
        QVERIFY(model.correctPoint(id, 5, 21, 20));
        QVERIFY(model.setFrame(9000, still));
        QVERIFY(model.startAnalysis(id, 9000, 9000));
        QTRY_VERIFY(!model.analysisRunning());
        QVERIFY(!model.acceptAnalysis(id));
        QCOMPARE(count(model, "points"), TrackingRotoModel::kMaxTrackPoints);
        QVERIFY2(model.saveToFile(url), qPrintable(model.lastError()));
        QVERIFY(reopened.loadFromFile(url));
        QCOMPARE(count(reopened, "points"), TrackingRotoModel::kMaxTrackPoints);
    }

    void undoKeepsUnacceptedResultsOfARunningJob()
    {
        TrackingRotoModel model;
        loadOccludedRun(model);
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 3);
        QVERIFY(model.startAnalysis(id, 1, 2));
        // This undo snapshot is taken while the track is "running".
        QVERIFY(!model.addMask(QStringLiteral("drawn during analysis")).isEmpty());
        QTRY_VERIFY(!model.analysisRunning());
        QCOMPARE(state(model), QStringLiteral("ready"));
        QCOMPARE(count(model, "previewPoints"), 2);
        QVERIFY(model.undo()); // undoes only the mask
        QVERIFY(model.masks().isEmpty());
        QCOMPARE(state(model), QStringLiteral("ready"));
        QCOMPARE(count(model, "previewPoints"), 2);
        QVERIFY(model.redo());
        QCOMPARE(model.masks().size(), 1);
        QCOMPARE(count(model, "previewPoints"), 2);
        QVERIFY(model.acceptAnalysis(id));
        QCOMPARE(count(model, "points"), 3);
        // Undoing the accept returns its results to preview.
        QVERIFY(model.undo());
        QCOMPARE(count(model, "points"), 1);
        QCOMPARE(count(model, "previewPoints"), 2);
        QVERIFY(model.acceptAnalysis(id));
    }

    void replacedFramesInvalidateFinishedResults()
    {
        TrackingRotoModel model;
        loadOccludedRun(model);
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 3);
        QVERIFY(model.startAnalysis(id, 1, 2));
        QTRY_VERIFY(!model.analysisRunning());
        QCOMPARE(count(model, "previewPoints"), 2);
        QVERIFY(model.setFrame(5, patterned(10, 5))); // not measured: results stay
        QCOMPARE(count(model, "previewPoints"), 2);
        QVERIFY(model.setFrame(2, patterned(4, 2))); // measured: results go
        QCOMPARE(count(model, "previewPoints"), 0);
        QCOMPARE(state(model), QStringLiteral("cancelled"));
        QVERIFY(model.lastError().contains(QStringLiteral("Frame 2")));
        QVERIFY(!model.acceptAnalysis(id));
        QCOMPARE(count(model, "points"), 1);

        // Neither can undo bring back a preview measured on replaced pixels,
        // including one measured against a replaced reference frame.
        QVERIFY(model.startAnalysis(id, 1, 2));
        QTRY_VERIFY(!model.analysisRunning());
        QVERIFY(model.acceptAnalysis(id));
        QVERIFY(model.setFrame(0, patterned(0, 0)));
        QVERIFY(model.undo());
        QCOMPARE(count(model, "points"), 1);
        QCOMPARE(count(model, "previewPoints"), 0);
        QVERIFY(!model.acceptAnalysis(id));
    }

    void cancelThenRestartDropsLateSamples()
    {
        TrackingRotoModel model;
        loadOccludedRun(model);
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 16);
        QVERIFY(model.startAnalysis(id, 1, 6, 2));
        // Let the worker finish and queue all six samples without this thread
        // reading them; they arrive only after the next job has started.
        QThread::msleep(300);
        model.cancelAnalysis();
        QVERIFY(model.startAnalysis(id, 6, 6));
        QTRY_VERIFY(!model.analysisRunning());
        QTest::qWait(50);
        const QVariantList preview = track(model).value(QStringLiteral("previewPoints")).toList();
        QCOMPARE(preview.size(), 1);
        QCOMPARE(preview.first().toMap().value(QStringLiteral("frame")).toInt(), 6);
        QCOMPARE(preview.first().toMap().value(QStringLiteral("x")).toDouble(), 32.0);
        QCOMPARE(state(model), QStringLiteral("ready"));
    }

    void generatedKeysStayMarkedThroughEditsUndoAndReopen()
    {
        TrackingRotoModel model;
        loadOccludedRun(model);
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 20, 5, 3);
        QVERIFY(model.startAnalysis(id, 1, 6, 2));
        QTRY_VERIFY(!model.analysisRunning());
        QVERIFY(model.acceptAnalysis(id));
        const QString maskId = model.addMask(QStringLiteral("follow"));
        QVERIFY(model.setMaskKeyframe(maskId, 0, square(15, 15)));
        QVERIFY(model.setMaskKeyframe(maskId, 5, square(30, 28, 12))); // drawn by the user

        // The range preview changes nothing.
        const QVariantMap preview = model.previewPropagation(maskId, 0, {id}, 0, 6);
        QVERIFY2(preview.value(QStringLiteral("ok")).toBool(), qPrintable(preview.value(QStringLiteral("error")).toString()));
        QCOMPARE(preview.value(QStringLiteral("state")).toString(), QStringLiteral("preview"));
        QCOMPARE(preview.value(QStringLiteral("keyedFrames")).toList(), (QVariantList{1, 2, 6}));
        QCOMPARE(preview.value(QStringLiteral("keptFrames")).toList(), (QVariantList{5}));
        QCOMPARE(preview.value(QStringLiteral("skippedFrames")).toList(), (QVariantList{3, 4}));
        QCOMPARE(preview.value(QStringLiteral("keys")).toList().size(), 3);
        QCOMPARE(model.masks().first().toMap().value(QStringLiteral("keyframes")).toList().size(), 2);

        const QVariantMap applied = model.applyPropagation(maskId, 0, {id}, 0, 6);
        QVERIFY(applied.value(QStringLiteral("ok")).toBool());
        QCOMPARE(keyAt(model, 0).value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QCOMPARE(keyAt(model, 1).value(QStringLiteral("source")).toString(), QStringLiteral("generated"));
        QCOMPARE(keyAt(model, 1).value(QStringLiteral("trackIds")).toStringList(), QStringList{id});
        QCOMPARE(keyAt(model, 5).value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QCOMPARE(keyAt(model, 5).value(QStringLiteral("points")).toList().first().toMap()
                     .value(QStringLiteral("x")).toDouble(), 30.0 / 64.0); // user key intact
        QCOMPARE(model.evaluateMask(maskId, 1).value(QStringLiteral("source")).toString(), QStringLiteral("generated"));
        QCOMPARE(model.evaluateMask(maskId, 0).value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        // Editing a generated key accepts it as the user's.
        QVERIFY(model.setMaskKeyframe(maskId, 2, square(18, 17)));
        QCOMPARE(keyAt(model, 2).value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QVERIFY(model.undo());
        QCOMPARE(keyAt(model, 2).value(QStringLiteral("source")).toString(), QStringLiteral("generated"));
        QVERIFY(model.redo());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QUrl url = QUrl::fromLocalFile(directory.filePath(QStringLiteral("roto.json")));
        QVERIFY(model.saveToFile(url));
        TrackingRotoModel reopened;
        QVERIFY(reopened.loadFromFile(url));
        QCOMPARE(keyAt(reopened, 1).value(QStringLiteral("source")).toString(), QStringLiteral("generated"));
        QCOMPARE(keyAt(reopened, 2).value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QCOMPARE(keyAt(reopened, 6).value(QStringLiteral("trackIds")).toStringList(), QStringList{id});
        // Rerunning propagation refreshes generated keys and keeps user keys.
        const QVariantMap again = model.applyPropagation(maskId, 0, {id}, 0, 6);
        QCOMPARE(again.value(QStringLiteral("keyedFrames")).toList(), (QVariantList{1, 6}));
        QCOMPARE(again.value(QStringLiteral("keptFrames")).toList(), (QVariantList{2, 5}));
        QVERIFY(!model.applyPropagation(maskId, 0, {id, id}, 0, 6).value(QStringLiteral("ok")).toBool());
    }
};

QTEST_GUILESS_MAIN(TrackingRotoResultsTest)
#include "tst_tracking_roto_results.moc"
