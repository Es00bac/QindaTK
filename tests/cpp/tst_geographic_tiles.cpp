// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/geographic_map_canvas.h"
#include "../../src/core/geographic_tile_source.h"
#include <QFile>
#include <QBuffer>
#include <QPainter>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace QindaTK;
class TestGeographicTiles : public QObject {
    Q_OBJECT
private slots:
    void localTilesAndStaleRequests()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage red(256,256,QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::red);
        QImage blue(256,256,QImage::Format_ARGB32_Premultiplied);
        blue.fill(Qt::blue);
        QVERIFY(red.save(dir.filePath("red.png")));
        QVERIFY(blue.save(dir.filePath("blue.png")));
        QFile manifest(dir.filePath("tiles.json"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"tileSize":256,"minZoom":0,"maxZoom":1,"attribution":"Fixture tiles",
          "tiles":[{"z":0,"x":0,"y":0,"file":"red.png"},
                   {"z":1,"x":0,"y":0,"file":"red.png"},
                   {"z":1,"x":1,"y":0,"file":"blue.png"}]})");
        manifest.close();
        GeographicTileSource source;
        QVERIFY(source.loadManifest(manifest.fileName()));
        QCOMPARE(source.attribution(), QStringLiteral("Fixture tiles"));
        source.setCacheLimitBytes(1024 * 1024);
        source.setVisibleTiles({GeographicTileSource::key(1,0,0)});
        source.setVisibleTiles({GeographicTileSource::key(1,1,0)});
        QTRY_COMPARE(source.tileState(GeographicTileSource::key(1,1,0)), 2);
        QCOMPARE(source.tileState(GeographicTileSource::key(1,0,0)), 1);
        QCOMPARE(source.cachedTileCount(), 1);
        QVERIFY(source.cacheBytes() <= source.cacheLimitBytes());
        GeographicMapCanvas canvas;
        canvas.setWidth(256); canvas.setHeight(256);
        canvas.setTileSource(&source);
        canvas.setZoomLevel(0);
        QTRY_COMPARE(canvas.tileStatus(), QStringLiteral("1/1 ready, 0 missing, 0 failed"));
        QImage composited(256,256,QImage::Format_ARGB32_Premultiplied);
        composited.fill(Qt::transparent);
        QPainter painter(&composited);
        canvas.paint(&painter);
        painter.end();
        QCOMPARE(composited.pixelColor(100,100), QColor(Qt::red));
        canvas.setZoomLevel(1);
        QVERIFY(canvas.tileStatus().contains(QStringLiteral("missing")));
        QVERIFY(!source.loadManifest("https://example.org/tiles.json"));
        QCOMPARE(source.attribution(), QStringLiteral("Fixture tiles"));
    }
    void networkTilesUseDiskCacheAndDropStaleReplies()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage tile(256,256,QImage::Format_RGB32);
        tile.fill(Qt::green);
        QByteArray png;
        QBuffer output(&png);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QVERIFY(tile.save(&output, "PNG"));
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QHash<QByteArray,int> hits;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                auto request = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, &server, [&, socket, request, handled] {
                    if (*handled) return;
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n")) return;
                    *handled = true;
                    const auto path = request->split(' ').value(1);
                    ++hits[path];
                    if (path.endsWith("/0/0.png")) return; // held until pan aborts it
                    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\n"
                        "Cache-Control: public, max-age=3600\r\nContent-Length: "
                        + QByteArray::number(png.size()) + "\r\nConnection: close\r\n\r\n" + png;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        const QString url = QStringLiteral("http://127.0.0.1:%1/tiles/{z}/{x}/{y}.png")
                                .arg(server.serverPort());
        GeographicTileSource source;
        source.setDiskCacheDirectory(dir.filePath("cache"));
        source.setDiskCacheLimitBytes(1024 * 1024);
        QVERIFY(source.useNetworkTemplate(url, QStringLiteral("Local HTTP fixture"), 1, 1, 256));
        const auto oldKey = GeographicTileSource::key(1,0,0);
        const auto newKey = GeographicTileSource::key(1,1,0);
        source.setVisibleTiles({oldKey});
        QTRY_COMPARE(hits.value("/tiles/1/0/0.png"), 1);
        source.setVisibleTiles({newKey});
        QTRY_COMPARE(source.tileState(newKey), 2);
        QCOMPARE(source.tileState(oldKey), 1);
        QTRY_VERIFY(source.diskCacheBytes() > 0);
        QVERIFY(source.diskCacheBytes() <= source.diskCacheLimitBytes());
        source.setVisibleTiles({});
        GeographicTileSource cached;
        cached.setDiskCacheDirectory(dir.filePath("cache"));
        QVERIFY(cached.useNetworkTemplate(url, QStringLiteral("Local HTTP fixture"), 1, 1, 256));
        cached.setVisibleTiles({newKey});
        QTRY_COMPARE(cached.tileState(newKey), 2);
        QCOMPARE(hits.value("/tiles/1/1/0.png"), 1); // served from disk on second source
    }
};
QTEST_MAIN(TestGeographicTiles)
#include "tst_geographic_tiles.moc"
