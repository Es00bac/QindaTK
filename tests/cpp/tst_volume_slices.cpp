// SPDX-License-Identifier: LGPL-3.0-or-later
// ImageSliceItem geometry (whole voxel cells, physical aspect, image cap),
// one cursor linking the three principal planes, and TransferFunctionModel
// evaluation, validation and JSON persistence.
#include "image_slice_item.h"
#include "transfer_function_model.h"
#include "volume_cursor.h"
#include "voxel_volume.h"
#include <QJsonDocument>
#include <QtTest>
#include <cmath>
#include <limits>

using namespace QindaTK;

namespace {
// Window centre 127.5 and width 255 map a uint8 value v to gray v exactly.
void identityWindow(ImageSliceItem &item)
{
    item.setWindowCenter(127.5);
    item.setWindowWidth(255);
}
QPointF pointOf(const QVariantList &list)
{
    return list.size() == 2 ? QPointF(list[0].toDouble(), list[1].toDouble()) : QPointF(-1, -1);
}
QVariant stop(const char *id, double value, double opacity = 1, const QColor &color = Qt::white)
{
    return QVariantMap{{"id", QString::fromLatin1(id)}, {"value", value},
                       {"color", color}, {"opacity", opacity}};
}
}

class VolumeSlicesTest : public QObject {
    Q_OBJECT
private slots:
    void sliceGeometryUsesWholeCells()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 4}, {"sizeY", 2}, {"sizeZ", 1},
                                  {"spacing", QVariantList{1, 2, 1}}}));
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) QVERIFY(volume.setVoxel(x, y, 0, x + 10 * y));
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        ImageSliceItem item;
        item.setVolume(&volume);
        item.setCursor(&cursor);
        identityWindow(item);
        item.setSize(QSizeF(200, 100));
        // 4 × 1 mm by 2 × 2 mm is square: a centred 100 × 100 px target whose
        // edges are continuous indices −0.5 and size − 0.5.
        const QVariantList corner = item.voxelAt(50, 0);
        QCOMPARE(corner.size(), 3);
        QCOMPARE(corner[0].toDouble(), -0.5);
        QCOMPARE(corner[1].toDouble(), -0.5);
        const QVariantList far = item.voxelAt(150, 100);
        QCOMPARE(far[0].toDouble(), 3.5);
        QCOMPARE(far[1].toDouble(), 1.5);
        QVERIFY(item.voxelAt(49, 50).isEmpty());
        QCOMPARE(pointOf(item.positionForVoxel(0, 0, 0)), QPointF(62.5, 25));
        QCOMPARE(pointOf(item.positionForVoxel(3, 1, 0)), QPointF(137.5, 75));
        const QImage cells = item.sliceImage(4, 2);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 4; ++column)
                QCOMPARE(qRed(cells.pixel(column, row)), column + 10 * row);
        const QImage doubled = item.sliceImage(8, 4);  // every cell is 2 × 2 pixels
        for (int column = 0; column < 8; ++column)
            QCOMPARE(qRed(doubled.pixel(column, 3)), column / 2 + 10);
        QCOMPARE(item.sliceImage(5000, 3000).size(), QSize(2048, 2048));
        QCOMPARE(ImageSliceItem::MaximumImageSide, 2048);
    }
    void physicalAspectFollowsSpacing()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 40}, {"sizeY", 40}, {"sizeZ", 10},
                                  {"spacing", QVariantList{0.5, 0.5, 3}}}));
        ImageSliceItem coronal, sagittal;
        for (ImageSliceItem *item : {&coronal, &sagittal}) {
            item->setVolume(&volume);
            item->setSize(QSizeF(300, 300));
        }
        coronal.setPlane(ImageSliceItem::Coronal);
        sagittal.setPlane(ImageSliceItem::Sagittal);
        // X (or Y) spans 20 mm and Z 30 mm: a height-limited 200 × 300 target.
        QCOMPARE(pointOf(coronal.positionForVoxel(-0.5, 0, -0.5)), QPointF(50, 0));
        QCOMPARE(pointOf(coronal.positionForVoxel(39.5, 0, 9.5)), QPointF(250, 300));
        QCOMPARE(pointOf(sagittal.positionForVoxel(0, 9.5, 0)), QPointF(100, 15));
        QVERIFY(coronal.voxelAt(40, 150).isEmpty());
        const QVariantList mid = coronal.voxelAt(150, 150);  // no cursor: middle slice
        QCOMPARE(mid[0].toDouble(), 19.5);
        QCOMPARE(mid[1].toDouble(), 19.5);
        QCOMPARE(mid[2].toDouble(), 4.5);
    }
    void cursorLinksAllPlanes()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 6}, {"sizeZ", 4}, {"voxelType", "uint8"}}));
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 6; ++y)
                for (int x = 0; x < 8; ++x) QVERIFY(volume.setVoxel(x, y, z, x + 10 * y + 60 * z));
        ImageSliceItem lone;  // without a cursor a view shows its middle slice
        lone.setVolume(&volume);
        identityWindow(lone);
        QCOMPARE(qRed(lone.sliceImage(8, 6).pixel(0, 0)), 120);
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        QCOMPARE(cursor.x(), 3.5);
        ImageSliceItem axial, coronal, sagittal;
        for (ImageSliceItem *item : {&axial, &coronal, &sagittal}) {
            item->setVolume(&volume);
            item->setCursor(&cursor);
            identityWindow(*item);
            item->setSize(QSizeF(160, 120));
        }
        coronal.setPlane(ImageSliceItem::Coronal);
        sagittal.setPlane(ImageSliceItem::Sagittal);
        // A click on axial voxel (5, 1) moves the shared cursor there.
        const QPointF click = pointOf(axial.positionForVoxel(5, 1, cursor.z()));
        const QVariantList picked = axial.voxelAt(click.x(), click.y());
        QCOMPARE(picked.size(), 3);
        QVERIFY(cursor.setVoxelPoint(picked[0].toDouble(), picked[1].toDouble(), picked[2].toDouble()));
        QCOMPARE(cursor.x(), 5.0);
        QCOMPARE(cursor.y(), 1.0);
        QCOMPARE(cursor.z(), 1.5);
        const QImage coronalImage = coronal.sliceImage(8, 4);    // the y = 1 plane
        for (int z = 0; z < 4; ++z)
            for (int x = 0; x < 8; ++x)
                QCOMPARE(qRed(coronalImage.pixel(x, z)), x + 10 + 60 * z);
        const QImage sagittalImage = sagittal.sliceImage(6, 4);  // the x = 5 plane
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 6; ++y)
                QCOMPARE(qRed(sagittalImage.pixel(y, z)), 5 + 10 * y + 60 * z);
        // Every plane's crosshair runs through the same voxel centre.
        QCOMPARE(pointOf(coronal.positionForVoxel(cursor.x(), cursor.y(), cursor.z())).x(), click.x());
        QCOMPARE(pointOf(sagittal.positionForVoxel(cursor.x(), cursor.y(), cursor.z())).y(),
                 pointOf(coronal.positionForVoxel(cursor.x(), cursor.y(), cursor.z())).y());
        // Linear interpolation blends the two nearest axial slices.
        axial.setInterpolation(VoxelVolume::Linear);
        QCOMPARE(qRed(axial.sliceImage(8, 6).pixel(2, 3)), 2 + 30 + 90);
    }
    void missingSamplesAreTransparent()
    {
        struct Source : VoxelChunkSource {
            QByteArray readChunk(int cx, int, int, int, int) override
            { return cx == 1 ? QByteArray() : QByteArray(32 * 32 * 32, char(200)); }
        };
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 64}, {"sizeY", 32}, {"sizeZ", 32}}));
        volume.setSource(std::make_shared<Source>());
        ImageSliceItem axial;
        axial.setVolume(&volume);
        identityWindow(axial);
        const QImage image = axial.sliceImage(64, 32);
        QCOMPARE(qAlpha(image.pixel(10, 5)), 255);
        QCOMPARE(qRed(image.pixel(10, 5)), 200);
        QCOMPARE(qAlpha(image.pixel(40, 5)), 0);
    }
    void transferEvaluation()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 100, QStringLiteral("HU")));
        QVERIFY(transfer.replacePoints({stop("a", 0, 0, Qt::black), stop("b", 50, 1, Qt::red),
                                        stop("c", 100, 0.5, Qt::blue)}));
        QCOMPARE(transfer.colorAt(25), QColor(128, 0, 0, 128));
        QCOMPARE(transfer.colorAt(50), QColor(255, 0, 0, 255));
        QCOMPARE(transfer.colorAt(75), QColor(128, 0, 128, 191));
        QCOMPARE(transfer.opacityAt(75), 0.75);
        QCOMPARE(transfer.colorAt(-10), QColor(0, 0, 0, 0));
        QCOMPARE(transfer.colorAt(1000), QColor(0, 0, 255, 128));
        QCOMPARE(transfer.colorAt(std::numeric_limits<double>::quiet_NaN()).alpha(), 0);
        QCOMPARE(transfer.opacityAt(std::numeric_limits<double>::infinity()), 0.0);

        const quint64 revision = transfer.revision();
        const QVariantList before = transfer.points();
        QVERIFY(!transfer.replacePoints({stop("a", 0), stop("b", 60), stop("c", 40), stop("d", 100)}));
        QVERIFY(!transfer.replacePoints({stop("a", 0), stop("a", 50), stop("c", 100)}));
        QVERIFY(!transfer.replacePoints({stop("a", 10), stop("c", 100)}));
        QVERIFY(!transfer.replacePoints({stop("a", 0), stop("b", 50, 1.5), stop("c", 100)}));
        QVERIFY(!transfer.replacePoints({stop("a", 0)}));
        QVERIFY(transfer.addPoint(50, Qt::green, 1).isEmpty());  // value already used
        QVERIFY(!transfer.movePoint(QStringLiteral("a"), 10));   // endpoints stay pinned
        QVERIFY(!transfer.movePoint(QStringLiteral("b"), 100));
        QVERIFY(!transfer.removePoint(QStringLiteral("c")));
        QVERIFY(!transfer.lastError().isEmpty());
        QCOMPARE(transfer.revision(), revision);
        QCOMPARE(transfer.points(), before);

        // A stop inserted at the current mapping leaves the function unchanged.
        const QColor quarter = transfer.colorAt(12.5);
        const QString inserted = transfer.addPoint(25, transfer.colorAt(25), transfer.opacityAt(25));
        QVERIFY(!inserted.isEmpty());
        QCOMPARE(transfer.colorAt(12.5), quarter);
        QCOMPARE(transfer.opacityAt(25), 0.5);
        QVERIFY(transfer.setPointColor(inserted, QColor(10, 20, 30, 40)));
        QCOMPARE(transfer.point(inserted).value(QStringLiteral("color")).value<QColor>(), QColor(10, 20, 30));
        QVERIFY(transfer.setPointOpacity(inserted, 0.25));
        QVERIFY(!transfer.setPointOpacity(inserted, 2));
        QVERIFY(transfer.movePoint(inserted, 30));
        QVERIFY(transfer.removePoint(inserted));
        QCOMPARE(transfer.points().size(), 3);
        QVERIFY(transfer.lastError().isEmpty());
    }
    void transferSettingsSurviveJson()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(-1000, 3000, QStringLiteral("HU")));
        const QString bone = transfer.addPoint(700, QColor(240, 230, 200), 0.8);
        QVERIFY(!bone.isEmpty());
        const QVariantMap saved = transfer.saveSettings();
        const QVariantMap json = QJsonDocument::fromVariant(saved).toVariant().toMap();
        TransferFunctionModel restored;
        QVERIFY(restored.restoreSettings(json));
        QCOMPARE(restored.domainMin(), -1000.0);
        QCOMPARE(restored.domainMax(), 3000.0);
        QCOMPARE(restored.unit(), QStringLiteral("HU"));
        QCOMPARE(restored.point(bone).value(QStringLiteral("color")).value<QColor>(), QColor(240, 230, 200));
        QCOMPARE(restored.colorAt(1850), transfer.colorAt(1850));
        QVariantMap broken = saved;
        broken.insert(QStringLiteral("domainMax"), 500);  // the saved stops reach 3000
        const quint64 revision = restored.revision();
        QVERIFY(!restored.restoreSettings(broken));
        QCOMPARE(restored.domainMax(), 3000.0);
        QCOMPARE(restored.revision(), revision);
        QCOMPARE(restored.points().size(), 3);
    }
};

QTEST_MAIN(VolumeSlicesTest)
#include "tst_volume_slices.moc"
