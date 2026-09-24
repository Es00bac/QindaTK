// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_fixture.h"
#include "timeline_media_provider.h"
#include "timeline_transfer.h"
#include "timeline_viewport.h"

#include <QElapsedTimer>
#include <QMimeData>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
using namespace TimelineFixture;

namespace {
// Test-controlled backend: requests complete only when the test says so, in
// any order, so cancellation and stale completion are deterministic.
class ManualBackend final : public TimelineMediaBackend {
public:
    struct Pending {
        TimelineMediaRequest request;
        CancellationToken cancel;
        Completion completion;
    };
    QString backendId() const override { return QStringLiteral("test.manual"); }
    void start(const TimelineMediaRequest &request, CancellationToken cancel, Completion done) override
    {
        pending.push_back({request, cancel, std::move(done)});
    }
    void complete(int index, float value = 0.5f)
    {
        TimelineMediaResult result;
        result.peaks = QVector<float>(pending.at(index).request.columns, value);
        pending[index].completion(result);
    }
    QVector<Pending> pending;
};

class MemoryClipboard final : public ClipboardAdapter {
public:
    const QMimeData *mimeData() const override { return data.get(); }
    bool setMimeData(QMimeData *next) override
    {
        data.reset(next);
        emit changed();
        return true;
    }
    std::unique_ptr<QMimeData> data;
};

void configure(TimelineViewport &view, TimelineModel &model)
{
    view.setModel(&model);
    view.setViewportWidth(1200);
    view.setViewportHeight(700);
    view.setDefaultTrackHeight(56);
    view.setOverscanPixels(64);
    view.setPixelsPerSecond(100);
}
} // namespace

class TimelineViewTest : public QObject {
    Q_OBJECT
private slots:
    void largeSessionCreatesViewportRowsOnly();
    void scrollingRetainsRowsThatStayVisible();
    void fullZoomOutStaysBounded();
    void hitTestingFindsBodiesAndHandles();
    void zoomKeepsTheAnchorTick();
    void previewRowsAndGhostsFollowTheSession();
    void farOriginGeometryStaysExact();
    void mediaRequestsSupersedeAndRejectStaleResults();
    void mediaRequestsAreBoundedAndCached();
    void mediaBackendSwapFailsPendingAndBoundsUnowned();
    void syntheticBackendIsDeterministicAndHonest();
    void clipsTransferThroughTheWp016Codec();
};

void TimelineViewTest::largeSessionCreatesViewportRowsOnly()
{
    TimelineModel model;
    QVERIFY(loadLargeSession(model));
    TimelineViewport view;
    QElapsedTimer timer;
    timer.start();
    configure(view, model);
    const qint64 refreshMs = timer.elapsed();
    // 700 px / 56 px = 12.5 -> 13 track rows; window [-0.64 s, 12.64 s) holds
    // the clips starting at 0, 2, ..., 12 s -> 7 per track.
    QCOMPARE(view.tracks()->count(), 13);
    QCOMPARE(view.clips()->count(), 13 * 7);
    QCOMPARE(view.firstVisibleTrack(), 0);
    QCOMPARE(view.lastVisibleTrack(), 12);
    QCOMPARE(view.contentHeight(), 5600.0);
    QVERIFY(!view.truncated());
    view.setScrollY(view.maximumScrollY());
    QCOMPARE(view.lastVisibleTrack(), 99);
    QCOMPARE(view.tracks()->count(), 13);
    const QVariantMap last = view.clips()->get(view.clips()->count() - 1);
    QCOMPARE(last.value(QStringLiteral("trackIndex")).toInt(), 99);
    qInfo("viewport refresh for 10,000 clips: %lld ms, %d clip rows, %d track rows", refreshMs,
          view.clips()->count(), view.tracks()->count());
}

void TimelineViewTest::scrollingRetainsRowsThatStayVisible()
{
    TimelineModel model;
    QVERIFY(loadLargeSession(model));
    TimelineViewport view;
    configure(view, model);
    const QString inserted = view.clips()->insertedTotalText();
    const QString firstKey = view.clips()->keys().first();
    const double firstX = view.clips()->get(0).value(QStringLiteral("clipX")).toDouble();
    view.scrollByPixels(10, 0);
    QCOMPARE(view.clips()->insertedTotalText(), inserted); // no delegate churn
    QCOMPARE(view.clips()->removedTotalText(), QStringLiteral("0"));
    QCOMPARE(view.clips()->keys().first(), firstKey);
    QCOMPARE(view.clips()->get(0).value(QStringLiteral("clipX")).toDouble(), firstX - 10);
    view.scrollByPixels(190, 0); // two seconds: one clip per track leaves, one enters
    QCOMPARE(view.clips()->removedTotalText(), QStringLiteral("13"));
    QCOMPARE(view.clips()->insertedTotalText(), QString::number(inserted.toInt() + 13));
    QCOMPARE(view.originTicksText(), QString::number(2 * S));
}

void TimelineViewTest::fullZoomOutStaysBounded()
{
    TimelineModel model;
    QVERIFY(loadLargeSession(model));
    TimelineViewport view;
    configure(view, model);
    view.setViewportHeight(5600); // every track at once
    view.fitToContent();
    QCOMPARE(view.tracks()->count(), 100);
    QVERIFY(view.clips()->count() <= view.maximumClipRows());
    QCOMPARE(view.summarizedClips(), 10000);
    const QVariantMap summary = view.clips()->get(0);
    QVERIFY(summary.value(QStringLiteral("summary")).toBool());
    QCOMPARE(summary.value(QStringLiteral("summaryCount")).toInt(), 100);
    QVERIFY(!view.truncated());
    qInfo("full zoom-out: %d rows for 10,000 clips", view.clips()->count());
}

void TimelineViewTest::hitTestingFindsBodiesAndHandles()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1), track(2)}, {clip(11, 1, S, S)}, {}));
    TimelineViewport view;
    configure(view, model);
    view.setHandlePixels(6);
    const double y = 20;
    QCOMPARE(view.hitTest(150, y).value(QStringLiteral("kind")).toString(), QStringLiteral("clip"));
    QCOMPARE(view.hitTest(150, y).value(QStringLiteral("clipId")).toString(), uid(11).toString(QUuid::WithoutBraces));
    QCOMPARE(view.hitTest(102, y).value(QStringLiteral("kind")).toString(), QStringLiteral("trimStart"));
    QCOMPARE(view.hitTest(198, y).value(QStringLiteral("kind")).toString(), QStringLiteral("trimEnd"));
    QCOMPARE(view.hitTest(203, y).value(QStringLiteral("kind")).toString(), QStringLiteral("trimEnd"));
    QCOMPARE(view.hitTest(250, y).value(QStringLiteral("kind")).toString(), QStringLiteral("track"));
    QCOMPARE(view.hitTest(150, 60).value(QStringLiteral("trackId")).toString(), uid(2).toString(QUuid::WithoutBraces));
    QCOMPARE(view.hitTest(150, 900).value(QStringLiteral("kind")).toString(), QStringLiteral("none"));
    QCOMPARE(view.ticksForX(150), QString::number(S + S / 2));
    QCOMPARE(view.xForTicks(QString::number(2 * S)), 200.0);
}

void TimelineViewTest::zoomKeepsTheAnchorTick()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(11, 1, 0, 10 * S)}, {}));
    TimelineViewport view;
    configure(view, model);
    QCOMPARE(view.ticksForX(300), QString::number(3 * S));
    view.zoomAround(300, 2.0);
    QCOMPARE(view.pixelsPerSecond(), 200.0);
    QCOMPARE(view.ticksForX(300), QString::number(3 * S));
    QCOMPARE(view.originTicksText(), QString::number(S + S / 2));
    view.zoomAround(300, 1e-12); // clamped to the minimum zoom, anchor kept
    QCOMPARE(view.pixelsPerSecond(), view.minimumPixelsPerSecond());
    view.setPixelsPerSecond(100); // the clip now lies millions of pixels to the right
    QVERIFY(view.xForTicks(QStringLiteral("0")) > view.viewportWidth());
    QVERIFY(view.ensureClipVisible(uid(11).toString(QUuid::WithoutBraces), 8));
    QCOMPARE(view.xForTicks(QString::number(10 * S)), view.viewportWidth() - 8);
    QCOMPARE(view.xForTicks(QStringLiteral("0")), view.viewportWidth() - 8 - 1000);
}

void TimelineViewTest::previewRowsAndGhostsFollowTheSession()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1), track(2)}, {clip(11, 1, S, S), clip(12, 2, 3 * S, S)}, {}));
    TimelineViewport view;
    configure(view, model);
    const QString key = uid(11).toString(QUuid::WithoutBraces);
    QCOMPARE(view.clips()->count(), 2);
    const QString op = model.begin(request(TimelineEditKind::Move, {uid(11)}, S));
    model.update(op, at(2 * S, 0, 1));
    QCOMPARE(view.clips()->count(), 3); // moved clip + its ghost + the other clip
    const QVariantMap moved = view.clips()->get(view.clips()->indexOf(key));
    QVERIFY(moved.value(QStringLiteral("preview")).toBool());
    QCOMPARE(moved.value(QStringLiteral("trackIndex")).toInt(), 1);
    QCOMPARE(moved.value(QStringLiteral("clipX")).toDouble(), 200.0);
    const QVariantMap ghost = view.clips()->get(view.clips()->indexOf(key + QStringLiteral(":ghost")));
    QVERIFY(ghost.value(QStringLiteral("ghost")).toBool());
    QCOMPARE(ghost.value(QStringLiteral("clipX")).toDouble(), 100.0);
    model.cancel(op);
    QCOMPARE(view.clips()->count(), 2);
    QCOMPARE(view.clips()->get(view.clips()->indexOf(key)).value(QStringLiteral("clipX")).toDouble(), 100.0);
    QVERIFY(!view.clips()->get(view.clips()->indexOf(key)).value(QStringLiteral("preview")).toBool());
    // A track resize preview moves the rows below it before anything commits.
    TimelineEditRequest resize;
    resize.kind = TimelineEditKind::ResizeTrack;
    resize.trackId = uid(1);
    const QString op2 = model.begin(resize);
    TimelineEditInput input;
    input.trackHeight = 100;
    model.update(op2, input);
    QCOMPARE(view.trackY(uid(2).toString(QUuid::WithoutBraces)), 100.0);
    model.cancel(op2);
    QCOMPARE(view.trackY(uid(2).toString(QUuid::WithoutBraces)), 56.0);
}

void TimelineViewTest::farOriginGeometryStaysExact()
{
    TimelineModel model;
    const qint64 far = qint64(1) << 60; // ~52 years of flicks; beyond 2^53
    QVERIFY(model.setContent({S, 1}, {track(1)}, {clip(11, 1, far + S, 1)}, {}));
    TimelineViewport view;
    configure(view, model);
    view.setOrigin(far);
    QCOMPARE(view.xForTicks(QString::number(far + S)), 100.0);
    QCOMPARE(view.ticksForX(100), QString::number(far + S));
    QCOMPARE(view.clips()->count(), 1);
    QCOMPARE(view.clips()->get(0).value(QStringLiteral("startTicks")).toString(), QString::number(far + S));
}

void TimelineViewTest::mediaRequestsSupersedeAndRejectStaleResults()
{
    auto backend = std::make_shared<ManualBackend>();
    TimelineMediaProvider provider;
    provider.setBackend(backend);
    QSignalSpy ready(&provider, &TimelineMediaProvider::ready);
    // Superseded in the same turn: the first request never reaches the backend.
    provider.requestPeaks(QStringLiteral("clip-a"), QStringLiteral("r1"), QStringLiteral("0"),
                          QString::number(S / 2), QString::number(S), QStringLiteral("1"), 64);
    const QString first = provider.requestPeaks(QStringLiteral("clip-a"), QStringLiteral("r1"),
        QStringLiteral("0"), QString::number(S), QString::number(S), QStringLiteral("1"), 64);
    QCOMPARE(backend->pending.size(), 0);
    QCoreApplication::processEvents();
    QCOMPARE(backend->pending.size(), 1);
    QCOMPARE(provider.cancelledCount(), QStringLiteral("1"));
    const QString second = provider.requestPeaks(QStringLiteral("clip-a"), QStringLiteral("r1"),
        QStringLiteral("0"), QString::number(2 * S), QString::number(S), QStringLiteral("1"), 64);
    QVERIFY(backend->pending.at(0).cancel.isCancelled()); // the owner moved on
    QCOMPARE(provider.cancelledCount(), QStringLiteral("2"));
    QCoreApplication::processEvents();
    QCOMPARE(provider.result(first).value(QStringLiteral("state")).toString(), QStringLiteral("unknown"));
    backend->complete(0);
    QCoreApplication::processEvents();
    QCOMPARE(provider.staleDropped(), QStringLiteral("1"));
    QCOMPARE(ready.count(), 0);
    backend->complete(1, 0.25f);
    QTRY_COMPARE(ready.count(), 1);
    QCOMPARE(ready.first().first().toString(), second);
    const QVariantMap result = provider.result(second);
    QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QCOMPARE(result.value(QStringLiteral("peaks")).toList().size(), 64);
    QCOMPARE(result.value(QStringLiteral("peaks")).toList().first().toDouble(), 0.25);
    // Invalidating the resource while a request runs drops its completion.
    const QString third = provider.requestPeaks(QStringLiteral("clip-b"), QStringLiteral("r1"),
        QString::number(S), QString::number(3 * S), QString::number(S), QStringLiteral("1"), 32);
    QCoreApplication::processEvents();
    provider.invalidateResource(QStringLiteral("r1"));
    backend->complete(2);
    QCoreApplication::processEvents();
    QCOMPARE(provider.staleDropped(), QStringLiteral("2"));
    QVERIFY(provider.result(third).value(QStringLiteral("state")).toString() != QLatin1String("ready"));
    // A completion after the provider is gone is harmless.
    auto *doomed = new TimelineMediaProvider;
    doomed->setBackend(backend);
    doomed->requestPeaks(QStringLiteral("x"), QStringLiteral("r2"), QStringLiteral("0"),
                         QString::number(S), QString::number(S), QStringLiteral("1"), 8);
    QCoreApplication::processEvents();
    delete doomed;
    backend->complete(int(backend->pending.size()) - 1);
    QCoreApplication::processEvents();
}

void TimelineViewTest::mediaRequestsAreBoundedAndCached()
{
    auto backend = std::make_shared<ManualBackend>();
    TimelineMediaProvider provider;
    provider.setBackend(backend);
    provider.setMaxInFlight(2);
    QStringList ids;
    for (int i = 0; i < 5; ++i)
        ids << provider.requestPeaks(QStringLiteral("clip-%1").arg(i), QStringLiteral("r%1").arg(i),
                                     QStringLiteral("0"), QString::number(S), QString::number(S),
                                     QStringLiteral("1"), 16);
    QCoreApplication::processEvents();
    QCOMPARE(provider.inFlightCount(), 2);
    QCOMPARE(provider.queuedCount(), 3);
    QCOMPARE(backend->pending.size(), 2);
    backend->complete(0);
    QTRY_COMPARE(backend->pending.size(), 3); // one slot freed, one queued request started
    QCOMPARE(provider.inFlightCount(), 2);
    QSignalSpy ready(&provider, &TimelineMediaProvider::ready);
    const QString again = provider.requestPeaks(QStringLiteral("clip-9"), QStringLiteral("r0"),
        QStringLiteral("0"), QString::number(S), QString::number(S), QStringLiteral("1"), 16);
    QCOMPARE(provider.result(again).value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
    QCOMPARE(backend->pending.size(), 3); // served from the cache, no backend work
    QTRY_COMPARE(ready.count(), 1);        // and announced after the caller holds the ID
    QCOMPARE(provider.requestPeaks(QString(), QString(), QStringLiteral("0"), QStringLiteral("1"),
                                   QStringLiteral("1"), QStringLiteral("1"), 1).isEmpty(), false);
}

void TimelineViewTest::mediaBackendSwapFailsPendingAndBoundsUnowned()
{
    auto first = std::make_shared<ManualBackend>();
    auto second = std::make_shared<ManualBackend>();
    TimelineMediaProvider provider;
    provider.setBackend(first);
    QSignalSpy failed(&provider, &TimelineMediaProvider::failed);
    QSignalSpy ready(&provider, &TimelineMediaProvider::ready);
    QSignalSpy swapped(&provider, &TimelineMediaProvider::backendChanged);
    const QString owned = provider.requestPeaks(QStringLiteral("clip-a"), QStringLiteral("r1"),
        QStringLiteral("0"), QString::number(S), QString::number(S), QStringLiteral("1"), 16);
    const QString unowned = provider.requestPeaks(QString(), QStringLiteral("r2"),
        QStringLiteral("0"), QString::number(S), QString::number(S), QStringLiteral("1"), 16);
    QCoreApplication::processEvents();
    QCOMPARE(first->pending.size(), 2);
    // Swapping backends fails what was pending, so no owner waits forever,
    // then tells owners to ask the new backend.
    provider.setBackend(second);
    QCOMPARE(failed.count(), 2);
    QCOMPARE(swapped.count(), 1);
    QStringList failedIds{failed.at(0).at(0).toString(), failed.at(1).at(0).toString()};
    failedIds.sort();
    QStringList expected{owned, unowned};
    expected.sort();
    QCOMPARE(failedIds, expected);
    QCOMPARE(failed.first().at(2).toString(), QStringLiteral("The media backend changed"));
    QVERIFY(first->pending.at(0).cancel.isCancelled());
    QCOMPARE(provider.requestCount(), 0);
    first->complete(0);
    QCoreApplication::processEvents();
    QCOMPARE(ready.count(), 0);
    // Finished requests without an owner are kept only up to a bound, and
    // every one is still notified before it is released.
    failed.clear();
    for (int i = 0; i < 1100; ++i)
        provider.requestPeaks(QString(), QStringLiteral("r3"), QString::number(S), QStringLiteral("0"),
                              QString::number(S), QStringLiteral("1"), 16); // an empty range fails at once
    QCoreApplication::processEvents();
    QCOMPARE(failed.count(), 1100);
    QCOMPARE(provider.requestCount(), 1024);
}

void TimelineViewTest::syntheticBackendIsDeterministicAndHonest()
{
    TimelineMediaProvider provider;
    provider.setReferenceBackend(true);
    QCOMPARE(provider.backendId(), QStringLiteral("qindatk.synthetic-reference"));
    QSignalSpy ready(&provider, &TimelineMediaProvider::ready);
    QSignalSpy failed(&provider, &TimelineMediaProvider::failed);
    const QString id = provider.requestPeaks(QStringLiteral("a"), QStringLiteral("tone"),
        QStringLiteral("0"), QString::number(4 * S), QString::number(S), QStringLiteral("1"), 200);
    QTRY_COMPARE(ready.count(), 1);
    const QVariantList peaks = provider.result(id).value(QStringLiteral("peaks")).toList();
    QCOMPARE(peaks.size(), 200);
    for (const auto &peak : peaks) QVERIFY(peak.toDouble() >= 0.0 && peak.toDouble() <= 0.95 + 1e-6);
    TimelineMediaProvider other;
    other.setReferenceBackend(true);
    QSignalSpy otherReady(&other, &TimelineMediaProvider::ready);
    const QString copy = other.requestPeaks(QStringLiteral("a"), QStringLiteral("tone"),
        QStringLiteral("0"), QString::number(4 * S), QString::number(S), QStringLiteral("1"), 200);
    QTRY_COMPARE(otherReady.count(), 1);
    QCOMPARE(other.result(copy).value(QStringLiteral("peaks")).toList(), peaks);
    provider.requestFrames(QStringLiteral("v"), QStringLiteral("tone"), QStringLiteral("0"),
                           QString::number(S), QString::number(S), QStringLiteral("1"), 2);
    QTRY_COMPARE(failed.count(), 1);
    QVERIFY(failed.first().at(2).toString().contains(QStringLiteral("no video frames")));
}

void TimelineViewTest::clipsTransferThroughTheWp016Codec()
{
    TimelineModel model;
    QVERIFY(model.setContent({S, 1}, {track(1), track(2), track(3, QStringLiteral("video"))},
                             {clip(11, 1, S, S), clip(12, 2, 2 * S, S)}, {}));
    MemoryClipboard memory;
    TimelineClipboard clipboard;
    clipboard.setAdapter(&memory);
    QVERIFY(!clipboard.canPaste());
    QVERIFY(clipboard.copyClips(&model, {uid(11).toString(QUuid::WithoutBraces),
                                         uid(12).toString(QUuid::WithoutBraces)}));
    QCOMPARE(clipboard.clipCount(), 2);
    QVERIFY(memory.data->hasFormat(QString::fromLatin1(TransferPayloadCodec::InternalMime)));
    const QVariantMap pasted = clipboard.paste(&model, uid(1).toString(QUuid::WithoutBraces),
                                               QString::number(10 * S));
    QVERIFY2(pasted.value(QStringLiteral("ok")).toBool(), qPrintable(pasted.value(QStringLiteral("error")).toString()));
    const QStringList ids = pasted.value(QStringLiteral("clipIds")).toStringList();
    QCOMPARE(ids.size(), 2);
    QCOMPARE(model.clipCount(), 4);
    const TimelineClip first = *model.clipRecord(QUuid(ids.at(0)));
    const TimelineClip second = *model.clipRecord(QUuid(ids.at(1)));
    QCOMPARE(first.start, 10 * S);          // offsets are kept exactly
    QCOMPARE(first.trackId, uid(1));
    QCOMPARE(second.start, 11 * S);
    QCOMPARE(second.trackId, uid(2));       // one track lower, as copied
    QCOMPARE(first.resourceId, QStringLiteral("resource:11"));
    QCOMPARE(model.historyCount(), 1);      // one paste, one undo step
    QVERIFY(model.undo());
    QCOMPARE(model.clipCount(), 2);
    // Pasting onto the last track would need a track below it: nothing changes.
    const QVariantMap refused = clipboard.paste(&model, uid(2).toString(QUuid::WithoutBraces), QStringLiteral("0"));
    QVERIFY(!refused.value(QStringLiteral("ok")).toBool());
    QCOMPARE(model.clipCount(), 2);
    // Another document with the same timebase needs its host to vouch for media.
    TimelineModel other;
    QVERIFY(other.setContent({S, 1}, {track(1), track(2)}, {}, {}));
    QVERIFY(!clipboard.paste(&other, uid(1).toString(QUuid::WithoutBraces), QStringLiteral("0"))
                 .value(QStringLiteral("ok")).toBool());
    clipboard.setAllowForeignResources(true);
    QVERIFY(clipboard.paste(&other, uid(1).toString(QUuid::WithoutBraces), QStringLiteral("0"))
                .value(QStringLiteral("ok")).toBool());
    QCOMPARE(other.clipCount(), 2);
    TimelineModel otherRate;
    QVERIFY(otherRate.setContent({48000, 1}, {track(1), track(2)}, {}, {}));
    QVERIFY(!clipboard.paste(&otherRate, uid(1).toString(QUuid::WithoutBraces), QStringLiteral("0"))
                 .value(QStringLiteral("ok")).toBool()); // never rounded into another timebase
    // The same payload drops through the WP-016 DropTarget with a guarded preview.
    OperationError error;
    const TransferPayload payload = TimelineTransfer::payloadFor(model, {uid(11)}, &error);
    QVERIFY(!error);
    TimelineDropTarget target(&model, QStringLiteral("timeline"), QStringLiteral("view-1"));
    const QVariantMap location{{QStringLiteral("trackId"), uid(2).toString(QUuid::WithoutBraces)},
                               {QStringLiteral("ticks"), QString::number(5 * S)}};
    DropPreview preview = target.previewDrop(QStringLiteral("s1"), payload, TransferOperation::Copy, location);
    QVERIFY2(preview.accepted, qPrintable(preview.error));
    QCOMPARE(model.clipCount(), 2);          // previews never edit
    QVERIFY(!target.commitDrop(QStringLiteral("s1"), preview.token));
    QCOMPARE(model.clipCount(), 3);
    preview = target.previewDrop(QStringLiteral("s2"), payload, TransferOperation::Copy, location);
    QVERIFY(model.undo());                   // the document moves on after the preview
    QCOMPARE(target.commitDrop(QStringLiteral("s2"), preview.token).code, ErrorCode::StaleRevision);
    QCOMPARE(model.clipCount(), 2);
    QVERIFY(!target.previewDrop(QStringLiteral("s3"), payload, TransferOperation::Move, location).accepted);
}

QTEST_MAIN(TimelineViewTest)
#include "tst_timeline_view.moc"
