// SPDX-License-Identifier: LGPL-3.0-or-later
// D-261: VolumeView reads volumes off the scene-graph and GUI threads, a
// newer request rejects the older frame, cancelRendering() stops provider
// reads, and a volume deleted mid-render is waited for, not read after free.
#include "transfer_function_model.h"
#include "volume_view_item.h"
#include "voxel_volume.h"
#include <QMutex>
#include <QQuickWindow>
#include <QSet>
#include <QThread>
#include <QtTest>
#include <atomic>

using namespace QindaTK;

namespace {
// Every chunk holds `value`; each read sleeps `delayMs` and records its thread.
class SlowSource : public VoxelChunkSource {
public:
    explicit SlowSource(int delayMs) : delay(delayMs) {}
    QByteArray readChunk(int, int, int, int, int) override
    {
        {
            QMutexLocker lock(&mutex);
            threads.insert(QThread::currentThread());
        }
        ++reads;
        QThread::msleep(ulong(delay.load()));
        return QByteArray(32 * 32 * 32, char(value));
    }
    std::atomic<int> reads{0}, delay;
    int value = 200;
    QMutex mutex;
    QSet<QThread *> threads;
};
struct Scene {
    QQuickWindow window;
    VolumeViewItem *view = nullptr;
    explicit Scene(VoxelVolume *volume)
    {
        window.setColor(Qt::black);
        window.resize(48, 48);
        view = new VolumeViewItem(window.contentItem());
        view->setSize(QSizeF(48, 48));
        view->setVolume(volume);
        view->setWindowCenter(127.5);
        view->setWindowWidth(255);
        view->setMode(VolumeViewItem::MaximumIntensity);
        view->setYaw(0);
        view->setPitch(0);
    }
    int stat(const char *name) const { return view->renderStatistics().value(QLatin1String(name)).toInt(); }
};
} // namespace

class VolumeAsyncTest : public QObject {
    Q_OBJECT
private slots:
    void rendersOffTheGuiThread()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 64}, {"sizeY", 64}, {"sizeZ", 64}}));
        auto source = std::make_shared<SlowSource>(5);
        volume.setSource(source);
        Scene scene(&volume);
        QSignalSpy busy(scene.view, &VolumeViewItem::busyChanged);
        scene.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
        QTRY_VERIFY(scene.view->busy());
        // The GUI event loop keeps running while the provider is read.
        QTRY_COMPARE(scene.stat("completed"), 1);
        QVERIFY(!scene.view->busy());
        QCOMPARE(busy.count(), 2);
        QVERIFY(!source->threads.contains(QThread::currentThread()));
        QVERIFY(!source->threads.contains(scene.window.thread()));
        // The shown frame is the exact reference.
        const QImage expected = scene.view->renderReference(48, 48);
        QCOMPARE(qRed(scene.window.grabWindow().pixel(24, 24)), qRed(expected.pixel(24, 24)));
        QVERIFY(qRed(expected.pixel(24, 24)) > 100); // value 200: gray 200 at opacity 200/255
    }

    void newerSettingsRejectTheObsoleteFrame()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 64}, {"sizeY", 64}, {"sizeZ", 64}}));
        auto source = std::make_shared<SlowSource>(40);
        volume.setSource(source);
        Scene scene(&volume);
        scene.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
        QTRY_VERIFY(source->reads > 0); // the first frame is being rendered
        scene.view->setClipHigh(QVector3D(0.5f, 1, 1)); // …and is now obsolete
        source->delay = 0;
        QTRY_COMPARE(scene.stat("completed"), 1);
        QTRY_VERIFY(!scene.view->busy());
        QCOMPARE(scene.stat("rejected"), 1);
        QCOMPARE(scene.stat("started"), 2);
        // What is on screen is the clipped frame: the right half is empty.
        const QImage grabbed = scene.window.grabWindow();
        const QImage expected = scene.view->renderReference(48, 48);
        QVERIFY(qRed(expected.pixel(12, 24)) > 100);
        QCOMPARE(qRed(grabbed.pixel(12, 24)), qRed(expected.pixel(12, 24)));
        QCOMPARE(qRed(grabbed.pixel(40, 24)), 0);
    }

    void cancelStopsProviderReads()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 256}, {"sizeY", 256}, {"sizeZ", 256}}));
        volume.setMaximumResidentBytes(4 * 32 * 32 * 32); // thrashes: every ray reads
        auto source = std::make_shared<SlowSource>(20);
        volume.setSource(source);
        Scene scene(&volume);
        scene.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
        QTRY_VERIFY(scene.view->busy());
        QTRY_VERIFY(source->reads > 2);
        scene.view->cancelRendering();
        QTRY_VERIFY(!scene.view->busy());
        QCOMPARE(scene.stat("canceled"), 1);
        QCOMPARE(scene.stat("completed"), 0);
        const int readsAfterCancel = source->reads;
        QTest::qWait(150);
        scene.window.requestUpdate();
        QTest::qWait(50);
        QCOMPARE(int(source->reads), readsAfterCancel); // nothing restarted
        QCOMPARE(scene.stat("started"), 1);
        // The next change asks for a new frame again.
        source->delay = 0;
        volume.setMaximumResidentBytes(64 * 1024 * 1024);
        QTRY_COMPARE(scene.stat("completed"), 1);
    }

    void deletingTheVolumeWaitsForTheWorker()
    {
        auto *volume = new VoxelVolume;
        QVERIFY(volume->configure({{"sizeX", 128}, {"sizeY", 128}, {"sizeZ", 128}}));
        volume->setMaximumResidentBytes(4 * 32 * 32 * 32);
        auto source = std::make_shared<SlowSource>(10);
        volume->setSource(source);
        Scene scene(volume);
        scene.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
        QTRY_VERIFY(source->reads > 0);
        delete volume; // aboutToBeDestroyed cancels and waits: no use after free
        const int reads = source->reads;
        QTest::qWait(60);
        QCOMPARE(int(source->reads), reads);
        QCOMPARE(scene.view->volume(), nullptr);
        scene.window.grabWindow(); // renders the empty view without touching the volume
    }
};

QTEST_MAIN(VolumeAsyncTest)
#include "tst_volume_async.moc"
