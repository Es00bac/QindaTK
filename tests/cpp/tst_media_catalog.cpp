// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_catalog.h"
#include "media_fixture.h"
#include "media_frame_feed.h"
#include "media_player.h"
#include "media_program.h"
#include "motion_composition.h"
#include "motion_frame_renderer.h"
#include "node_graph_model.h"
#include "resource_resolver.h"
#include "time_map.h"
#include "timeline_model.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace MediaFixture;

namespace {
constexpr qint64 F = kFrame25;

QString state(const MediaCatalog &catalog, const QString &assetId)
{
    return catalog.asset(assetId).value(QStringLiteral("metadataState")).toString();
}

qint64 renderedCode(const QVariantMap &graphRender)
{
    const QString url = graphRender.value(QStringLiteral("imageDataUrl")).toString();
    const QString prefix = QStringLiteral("data:image/png;base64,");
    if (!url.startsWith(prefix)) return -1;
    return MediaExport::readTestSignalCode(QImage::fromData(QByteArray::fromBase64(url.mid(prefix.size()).toLatin1()), "PNG"));
}
} // namespace

class TstMediaCatalog final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
    }

    void importReadsMetadataIntoBinsAndSearch()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("harbor.mkv")), 50, 128, 72, 2, 25);
        const QString b = signal(dir.filePath(QStringLiteral("studio.mkv")), 30);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        ResourceResolver resolver;
        MediaCatalog catalog;
        catalog.setResolver(&resolver);
        const QString first = catalog.importMedia(fileUri(a));
        QVERIFY2(!first.isEmpty(), qPrintable(catalog.lastError()));
        const QString second = catalog.importMedia(b, {}, QStringLiteral("Interview"));
        QVERIFY(!second.isEmpty());
        QCOMPARE(catalog.count(), 2);
        QTRY_COMPARE(state(catalog, first), QStringLiteral("ready"));
        QTRY_COMPARE(state(catalog, second), QStringLiteral("ready"));
        const QVariantMap harbor = catalog.asset(first);
        QCOMPARE(harbor.value(QStringLiteral("label")).toString(), QStringLiteral("harbor.mkv"));
        QCOMPARE(harbor.value(QStringLiteral("frameCount")).toString(), QStringLiteral("50"));
        QCOMPARE(harbor.value(QStringLiteral("resolution")).toString(), QStringLiteral("128×72"));
        QCOMPARE(harbor.value(QStringLiteral("duration")).toString(), QStringLiteral("00:00:02:00"));
        QCOMPARE(harbor.value(QStringLiteral("codec")).toString(), QStringLiteral("ffv1"));
        QVERIFY(harbor.value(QStringLiteral("online")).toBool());
        QVERIFY(!harbor.value(QStringLiteral("audio")).toString().isEmpty());
        QCOMPARE(catalog.data(catalog.index(0), MediaCatalog::CaptionRole).toString(),
                 QStringLiteral("00:00:02:00 · 128×72"));

        const QString selects = catalog.addBin(QStringLiteral("Selects"));
        QVERIFY(!selects.isEmpty());
        QVERIFY(catalog.moveToBin({second}, selects));
        QCOMPARE(catalog.bins().first().toMap().value(QStringLiteral("count")).toInt(), 1);
        catalog.setBinFilter(selects);
        QCOMPARE(catalog.count(), 1);
        QCOMPARE(catalog.data(catalog.index(0), MediaCatalog::AssetIdRole).toString(), second);
        catalog.setBinFilter({});
        catalog.setSearchText(QStringLiteral("interview"));
        QCOMPARE(catalog.count(), 1);
        catalog.setSearchText(QStringLiteral("FFV1"));
        QCOMPARE(catalog.count(), 2);
        QVERIFY(catalog.setTags(first, {QStringLiteral("aerial"), QStringLiteral("selects")}));
        catalog.setSearchText(QStringLiteral("aerial"));
        QCOMPARE(catalog.count(), 1);
        QCOMPARE(catalog.data(catalog.index(0), MediaCatalog::AssetIdRole).toString(), first);
        catalog.setSearchText({});
        QVERIFY(catalog.removeBin(selects));
        QCOMPARE(catalog.asset(second).value(QStringLiteral("binId")).toString(), QString());
        // A file that is not there yet imports as an offline asset.
        const QString offline = catalog.importMedia(dir.filePath(QStringLiteral("absent.mkv")));
        QVERIFY(!offline.isEmpty());
        QCOMPARE(state(catalog, offline), QStringLiteral("missing"));
        QVERIFY(!catalog.asset(offline).value(QStringLiteral("online")).toBool());
        QCOMPARE(catalog.asset(offline).value(QStringLiteral("caption")).toString(), QStringLiteral("Offline"));
    }

    void relinkKeepsSequenceReferences()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("moved")));
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 50);
        QVERIFY(!a.isEmpty());
        ResourceResolver resolver;
        MediaCatalog catalog;
        catalog.setResolver(&resolver);
        const QString assetId = catalog.importMedia(a);
        QTRY_COMPARE(state(catalog, assetId), QStringLiteral("ready"));
        const QString resourceId = catalog.asset(assetId).value(QStringLiteral("resourceId")).toString();
        TimeMap map;
        QVERIFY(map.setFrameRate(25, 1));
        TimelineModel timeline;
        timeline.setTimeMap(&map);
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        const TimelineClip shot = clip(1, v1, 0, 50 * F, 0, resourceId);
        OperationError error;
        QVERIFY(timeline.setContent(kTimebase, {v1}, {shot}, {}, &error));
        const auto resolve = [&resolver](const QString &id) {
            const QVariantMap entry = resolver.lookup(id);
            return std::pair<QString, QString>{entry.value(QStringLiteral("uri")).toString(),
                                               entry.value(QStringLiteral("revision")).toString()};
        };

        // The file moves: the asset goes offline, the sequence keeps its reference.
        const QString moved = QDir(dir.filePath(QStringLiteral("moved"))).filePath(QStringLiteral("a.mkv"));
        QVERIFY(QFile::rename(a, moved));
        catalog.refresh(assetId);
        QTRY_COMPARE(state(catalog, assetId), QStringLiteral("missing"));
        QVERIFY(!catalog.asset(assetId).value(QStringLiteral("online")).toBool());
        QCOMPARE(catalog.relinkMissingFrom(fileUri(dir.filePath(QStringLiteral("moved")))), 1);
        QTRY_COMPARE(state(catalog, assetId), QStringLiteral("ready"));
        QCOMPARE(resolver.lookup(resourceId).value(QStringLiteral("availability")).toString(), QStringLiteral("ready"));
        QCOMPARE(timeline.clipRecord(shot.id)->resourceId, resourceId);
        const auto program = MediaProgram::fromTimeline(timeline, resolve, QSize(128, 72), 2);
        QVERIFY(program && program->unresolved.isEmpty());
        QCOMPARE(mediaLocalPath(program->video.first().uri), moved);
        MediaDecoderPool pool;
        QCOMPARE(MediaExport::readTestSignalCode(program->render(7, program->frameSize, pool, {}).image), qint64(7));

        // Relinking to different content is allowed but inspectable as modified.
        const QString other = signal(dir.filePath(QStringLiteral("other.mkv")), 20);
        QVERIFY(catalog.relink(assetId, other));
        QCOMPARE(resolver.lookup(resourceId).value(QStringLiteral("availability")).toString(), QStringLiteral("modified"));
        QTRY_COMPARE(catalog.asset(assetId).value(QStringLiteral("frameCount")).toString(), QStringLiteral("20"));
        QVERIFY(!catalog.relink(assetId, dir.filePath(QStringLiteral("absent.mkv"))));
    }

    void proxiesReplacePicturesNotIdentity()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 50, 320, 180, 2, 25);
        QVERIFY(!a.isEmpty());
        ResourceResolver resolver;
        MediaCatalog catalog;
        catalog.setResolver(&resolver);
        catalog.setProxyDirectory(dir.filePath(QStringLiteral("proxies")));
        const QString assetId = catalog.importMedia(a);
        QTRY_COMPARE(state(catalog, assetId), QStringLiteral("ready"));
        const QString resourceId = catalog.asset(assetId).value(QStringLiteral("resourceId")).toString();
        QVERIFY(!catalog.setProxyActive(assetId, true)); // no proxy yet
        QSignalSpy finished(&catalog, &MediaCatalog::jobFinished);
        const QString job = catalog.createProxy(assetId, 90);
        QVERIFY2(!job.isEmpty(), qPrintable(catalog.lastError()));
        QCOMPARE(catalog.asset(assetId).value(QStringLiteral("proxyStatus")).toString(), QStringLiteral("creating"));
        QTRY_COMPARE_WITH_TIMEOUT(catalog.asset(assetId).value(QStringLiteral("proxyStatus")).toString(),
                                  QStringLiteral("ready"), 60000);
        QVERIFY(finished.last().at(3).toBool());
        QVERIFY(catalog.activeProxyUri(resourceId).isEmpty()); // ready, not yet in use
        QVERIFY(catalog.setProxyActive(assetId, true));
        const QString proxy = catalog.activeProxyUri(resourceId);
        QVERIFY(!proxy.isEmpty());
        MediaDecoder decoder;
        QVERIFY(!decoder.open(proxy));
        QCOMPARE(decoder.info()->frameCount(), qint64(50));
        QCOMPARE(decoder.info()->width, 160);
        QCOMPARE(decoder.info()->height, 90);
        QCOMPARE(decoder.info()->audioSampleCount, qint64(96000));

        MediaPlayer player;
        player.setResolver(&resolver);
        player.setCatalog(&catalog);
        player.setResourceId(resourceId);
        QTRY_COMPARE(player.state(), QStringLiteral("ready"));
        QVERIFY(player.proxyActive());
        QTRY_VERIFY(player.transport()->canSeek());
        QVERIFY(!player.seekFrame(QStringLiteral("30")).isEmpty());
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("30"));
        QCOMPARE(player.presentedImage().size(), QSize(160, 90));
        QCOMPARE(MediaExport::readTestSignalCode(player.presentedImage()), qint64(30));
        QCOMPARE(player.presentedSources().first().toMap().value(QStringLiteral("frame")).toString(), QStringLiteral("30"));
        QVERIFY(catalog.setProxyActive(assetId, false));
        QTRY_COMPARE(player.presentedImage().size(), QSize(320, 180));
        QVERIFY(!player.proxyActive());
        QCOMPARE(MediaExport::readTestSignalCode(player.presentedImage()), qint64(30));
    }

    void contentRoundTripsBinsAndProxies()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 10);
        QVERIFY(!a.isEmpty());
        ResourceResolver resolver;
        MediaCatalog catalog;
        catalog.setResolver(&resolver);
        const QString bin = catalog.addBin(QStringLiteral("Day one"));
        const QString assetId = catalog.importMedia(a, bin, QStringLiteral("Harbor"));
        QVERIFY(catalog.setTags(assetId, {QStringLiteral("wide")}));
        const QVariantMap saved = catalog.contentMap();
        MediaCatalog reopened;
        reopened.setResolver(&resolver);
        QVERIFY(reopened.loadContent(saved));
        QCOMPARE(reopened.totalCount(), 1);
        QCOMPARE(reopened.bins().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Day one"));
        const QVariantMap asset = reopened.asset(assetId);
        QCOMPARE(asset.value(QStringLiteral("label")).toString(), QStringLiteral("Harbor"));
        QCOMPARE(asset.value(QStringLiteral("binId")).toString(), bin);
        QCOMPARE(asset.value(QStringLiteral("tags")).toStringList(), QStringList{QStringLiteral("wide")});
        QTRY_COMPARE(state(reopened, assetId), QStringLiteral("ready"));
        QVariantMap broken = saved;
        broken.insert(QStringLiteral("schema"), QStringLiteral("other"));
        QVERIFY(!reopened.loadContent(broken));
        QCOMPARE(reopened.totalCount(), 1);
    }

    void frameFeedFeedsLayersAndGraphSources()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 50);
        QVERIFY(!a.isEmpty());
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("clip"), fileUri(a)));
        QVERIFY(resolver.registerResource(QStringLiteral("late"), fileUri(dir.filePath(QStringLiteral("late.mkv")))));
        MotionComposition composition;
        const auto layer = [](const QString &id, const QString &source, int z, const QString &offset) {
            return QVariantMap{{QStringLiteral("layerId"), id}, {QStringLiteral("sourceId"), source},
                               {QStringLiteral("zIndex"), z}, {QStringLiteral("visible"), true},
                               {QStringLiteral("timeOffsetTicks"), offset},
                               {QStringLiteral("transform"), QVariantMap{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                                                                          {QStringLiteral("opacity"), 1.0}}}};
        };
        composition.setLayers({layer(QStringLiteral("shot"), QStringLiteral("clip"), 0, QString::number(10 * F))});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        NodeGraphModel graph;
        const QString sourceNode = graph.addNode(QStringLiteral("source"), QStringLiteral("Clip"), 0, 0,
                                                 {{QStringLiteral("sourceId"), QStringLiteral("clip")}});
        const QString output = graph.addNode(QStringLiteral("output"), QStringLiteral("Out"), 200, 0);
        QVERIFY(!graph.connectPorts(sourceNode, QStringLiteral("image"), output, QStringLiteral("image")).isEmpty());
        MediaFrameFeed feed;
        feed.setResolver(&resolver);
        feed.setComposition(&composition);
        feed.setRenderer(&renderer);
        feed.setGraph(&graph);
        QSignalSpy prepared(&feed, &MediaFrameFeed::prepared);
        const QString ticks = QString::number(20 * F);
        feed.prepare(QString::number(3 * F)); // superseded before it can apply
        const QString request = feed.prepare(ticks);
        QVERIFY(!request.isEmpty());
        QTRY_COMPARE(prepared.count(), 1);
        QCOMPARE(prepared.first().at(0).toString(), request);
        QCOMPARE(feed.fedSources().size(), 2);
        const QVariantMap frame = renderer.render(ticks, 128, 72);
        QVERIFY2(frame.value(QStringLiteral("ok")).toBool(), qPrintable(renderer.lastError()));
        QCOMPARE(frame.value(QStringLiteral("drawnLayers")).toInt(), 1);
        QCOMPARE(MediaExport::readTestSignalCode(renderer.frame()), qint64(10)); // layer time 20 - 10 frames
        QCOMPARE(renderedCode(graph.render(output, 128, 72)), qint64(20));

        // An offline layer source is removed, then fed again once it appears.
        composition.setLayers({layer(QStringLiteral("shot"), QStringLiteral("late"), 0, QStringLiteral("0"))});
        feed.prepare(ticks);
        QTRY_COMPARE(prepared.count(), 2);
        QCOMPARE(feed.missingResourceIds(), QStringList{QStringLiteral("late")});
        QCOMPARE(renderer.render(ticks, 128, 72).value(QStringLiteral("missingSourceIds")).toStringList(),
                 QStringList{QStringLiteral("late")});
        QVERIFY(!signal(dir.filePath(QStringLiteral("late.mkv")), 30).isEmpty());
        QVERIFY(resolver.refresh(QStringLiteral("late")));
        QTRY_COMPARE(prepared.count(), 3);
        QVERIFY(feed.missingResourceIds().isEmpty());
        const QVariantMap relinked = renderer.render(ticks, 128, 72);
        QCOMPARE(relinked.value(QStringLiteral("drawnLayers")).toInt(), 1);
        QCOMPARE(MediaExport::readTestSignalCode(renderer.frame()), qint64(20));
        QVERIFY(feed.prepare(QStringLiteral("1.5")).isEmpty());
    }
};

QTEST_MAIN(TstMediaCatalog)
#include "tst_media_catalog.moc"
