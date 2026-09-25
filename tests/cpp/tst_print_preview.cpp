// SPDX-License-Identifier: LGPL-3.0-or-later
// Print previews (WP-053, OFF-12): requests render on the preview thread and
// arrive through previewReady with the pixels a synchronous render gives;
// results for an older document are dropped; and the cache's byte limit holds
// for every image, including very large zooms.
#include "print_adapter.h"
#include "rich_text_fixtures.h"
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichTextFixtures;

class PrintPreviewTest : public QObject {
    Q_OBJECT
private slots:
    void previewsRenderOffTheCallingThread()
    {
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        QCOMPARE(adapter.pageCount(), 3);
        QSignalSpy ready(&adapter, &PrintAdapter::previewReady);

        // The request returns at once: nothing was rendered on this thread.
        QVERIFY(!adapter.requestPreview(1, 0.5));
        QCOMPARE(adapter.previewRenderCount(), 0);
        QCOMPARE(adapter.previewPendingCount(), 1);
        QVERIFY(adapter.cachedPreview(1, 0.5).isNull());
        QVERIFY(!adapter.requestPreview(1, 0.5)); // already queued, not queued twice
        QCOMPARE(adapter.previewPendingCount(), 1);
        QVERIFY(!adapter.requestPreview(7, 0.5)); // no such page: nothing queued
        QCOMPARE(adapter.previewPendingCount(), 1);

        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(ready.first().first().toInt(), 1);
        QCOMPARE(adapter.previewPendingCount(), 0);
        QCOMPARE(adapter.previewRenderCount(), 1);
        QVERIFY(adapter.requestPreview(1, 0.5)); // cached now
        const QImage threaded = adapter.cachedPreview(1, 0.5);
        QVERIFY(!threaded.isNull());

        // The same pixels as a render on the calling thread (one painter, D-130).
        PrintAdapter reference;
        reference.setDocument(&doc);
        QCOMPARE(threaded, reference.previewImage(1, 0.5));

        // A result for the document before an edit never reaches the cache.
        const QImage uneditedFirst = reference.previewImage(0, 0.5);
        QVERIFY(!adapter.requestPreview(0, 0.5));
        QVERIFY(doc.insertText(doc.startPosition(), QStringLiteral("Edited ")));
        QCOMPARE(adapter.previewPendingCount(), 0);
        QVERIFY(!adapter.requestPreview(2, 0.5));
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(ready.last().first().toInt(), 2);
        QTest::qWait(50); // let any late result of the old request arrive
        QVERIFY(adapter.cachedPreview(0, 0.5).isNull());
        QCOMPARE(adapter.previewRenderCount(), 2);

        // The new generation renders the edited text.
        QVERIFY(!adapter.requestPreview(0, 0.5));
        QTRY_VERIFY(!adapter.cachedPreview(0, 0.5).isNull());
        PrintAdapter edited;
        edited.setDocument(&doc);
        QCOMPARE(adapter.cachedPreview(0, 0.5), edited.previewImage(0, 0.5));
        QVERIFY(adapter.cachedPreview(0, 0.5) != uneditedFirst);
    }

    void destroyingTheAdapterWithQueuedRendersIsSafe()
    {
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        auto adapter = std::make_unique<PrintAdapter>();
        adapter->setDocument(&doc);
        for (int page = 0; page < 3; ++page) QVERIFY(!adapter->requestPreview(page, 1.0));
        adapter.reset(); // joins the preview thread; queued results are dropped
        QTest::qWait(20);
    }

    void everyPreviewImageFitsTheByteLimit()
    {
        RichTextDocument doc;
        buildLines(doc, 6, 2);
        PrintAdapter adapter;
        adapter.setDocument(&doc);
        QVERIFY(adapter.applySettings({{QStringLiteral("paperId"), QStringLiteral("a3")}}));
        const Print::Sheet sheet = adapter.sheetOf(0);
        const QSize full = Print::sheetPixelSize(sheet, 8.0);
        QVERIFY(qint64(full.width()) * full.height() * 4 > PrintAdapter::PreviewCacheByteLimit); // A3 at 8x: ~456 MB

        const double zoom = adapter.previewZoomFor(0, 8.0);
        QVERIFY(zoom > 1.0 && zoom < 8.0);
        const QImage large = adapter.previewImage(0, 8.0);
        QCOMPARE(large.size(), Print::sheetPixelSize(sheet, zoom));
        QVERIFY(large.sizeInBytes() <= PrintAdapter::PreviewImageByteLimit);
        // One more 1/64 step would not fit: the zoom is as large as allowed.
        const QSize next = Print::sheetPixelSize(sheet, zoom + 1.0 / 64);
        QVERIFY(qint64(next.width()) * next.height() * 4 > PrintAdapter::PreviewImageByteLimit);
        // Any zoom above the fitting one is the same image, not another render.
        adapter.previewImage(0, 6.0);
        QCOMPARE(adapter.previewRenderCount(), 1);
        QCOMPARE(adapter.previewCacheCount(), 1);

        // Many large images: the byte limit evicts before the count limit.
        const QVector<double> zooms{8.0, 1.5, 1.45, 1.4};
        for (double z : zooms)
            for (int page = 0; page < 3; ++page) {
                QVERIFY(!adapter.previewImage(page, z).isNull());
                QVERIFY2(adapter.previewCacheBytes() <= PrintAdapter::PreviewCacheByteLimit,
                         qPrintable(QString::number(adapter.previewCacheBytes())));
            }
        QVERIFY(adapter.previewRenderCount() > adapter.previewCacheLimit());
        QVERIFY(adapter.previewCacheCount() < adapter.previewCacheLimit());

        // The preview thread keeps to the same limit.
        adapter.requestPreview(2, 7.0);
        QTRY_VERIFY(!adapter.cachedPreview(2, 7.0).isNull());
        QVERIFY(adapter.cachedPreview(2, 7.0).sizeInBytes() <= PrintAdapter::PreviewImageByteLimit);
        QVERIFY(adapter.previewCacheBytes() <= PrintAdapter::PreviewCacheByteLimit);

        // A sheet at the 16384 px edge limit still comes back within the limit.
        Print::Sheet poster;
        poster.sizePt = QSizeF(14400, 14400);
        const double posterZoom = Print::previewZoom(poster, 8.0, PrintAdapter::PreviewImageByteLimit);
        const QSize posterPixels = Print::sheetPixelSize(poster, posterZoom);
        QVERIFY(qint64(posterPixels.width()) * posterPixels.height() * 4 <= PrintAdapter::PreviewImageByteLimit);
        QCOMPARE(Print::previewZoom(poster, 0.1, PrintAdapter::PreviewImageByteLimit), 0.1);
    }
};

QTEST_MAIN(PrintPreviewTest)
#include "tst_print_preview.moc"
