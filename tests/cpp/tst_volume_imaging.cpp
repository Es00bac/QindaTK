// SPDX-License-Identifier: LGPL-3.0-or-later
#include "image_slice_item.h"
#include "transfer_function_model.h"
#include "volume_cursor.h"
#include "voxel_volume.h"
#include <QtTest>
#include <cmath>

using namespace QindaTK;

class VolumeImagingTest : public QObject {
    Q_OBJECT
private slots:
    void typedSamplingAndPhysicalCoordinates()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 3}, {"sizeY", 3}, {"sizeZ", 3},
            {"voxelType", "uint16"}, {"spacing", QVariantList{2, 3, 4}},
            {"origin", QVariantList{10, 20, 30}},
            {"orientation", QVariantList{0, -1, 0, 1, 0, 0, 0, 0, 1}},
            {"unit", "mm"}, {"valueUnit", "HU"}}));
        QVERIFY(volume.setVoxel(1, 1, 1, 800));
        QCOMPARE(volume.valueAt(1, 1, 1), 800.0);
        QCOMPARE(volume.sampleValue(0.5, 0.5, 0.5, 0, 0, VoxelVolume::Linear), 100.0);
        QCOMPARE(volume.sampleValue(0.5, 0.5, 0.5, 0, 0, VoxelVolume::Nearest), 800.0);
        QCOMPARE(volume.worldFromVoxel(1, 2, 2), (QVariantList{4.0, 22.0, 38.0}));
        QCOMPARE(volume.voxelFromWorld(4, 22, 38), (QVariantList{1.0, 2.0, 2.0}));
        const quint64 revision = volume.revision();
        QVERIFY(!volume.configure({{"sizeX", 3}, {"sizeY", 3}, {"sizeZ", 3},
            {"orientation", QVariantList{1, 0, 0, 0, 2, 0, 0, 0, 1}}}));
        QCOMPARE(volume.revision(), revision);
        QCOMPARE(volume.valueAt(1, 1, 1), 800.0);
    }
    void boundedProviderAndLinkedSlices()
    {
        struct Source : VoxelChunkSource {
            QByteArray readChunk(int cx, int, int, int, int) override
            { return QByteArray(32 * 32 * 32, char(cx + 1)); }
        };
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 96}, {"sizeY", 32}, {"sizeZ", 32},
                                  {"voxelType", "uint8"}, {"spacing", QVariantList{1, 1, 2}}}));
        volume.setMaximumResidentBytes(32 * 32 * 32);
        volume.setSource(std::make_shared<Source>());
        QCOMPARE(volume.valueAt(0, 0, 0), 1.0);
        QCOMPARE(volume.valueAt(40, 0, 0), 2.0);
        QCOMPARE(volume.valueAt(80, 0, 0), 3.0);
        QVERIFY(volume.residentBytes() <= 32 * 32 * 32);
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        QVERIFY(cursor.setVoxelPoint(40, 12, 8));
        ImageSliceItem axial, coronal, sagittal;
        axial.setVolume(&volume); axial.setCursor(&cursor);
        coronal.setVolume(&volume); coronal.setCursor(&cursor);
        sagittal.setVolume(&volume); sagittal.setCursor(&cursor);
        axial.setPlane(ImageSliceItem::Axial);
        coronal.setPlane(ImageSliceItem::Coronal);
        sagittal.setPlane(ImageSliceItem::Sagittal);
        QVERIFY(axial.sliceImage(3, 3).pixelColor(1, 1).red() < 5);
        QCOMPARE(coronal.sliceImage(3, 3).size(), QSize(3, 3));
        QCOMPARE(sagittal.sliceImage(3, 3).size(), QSize(3, 3));
        QVERIFY(cursor.setWorldPoint(80, 6, 8));
        QCOMPARE(cursor.x(), 80.0);
        QCOMPARE(cursor.y(), 6.0);
        QCOMPARE(cursor.z(), 4.0);
    }
    void phantomAndTransferSettings()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 33}, {"sizeY", 33}, {"sizeZ", 33},
                                  {"voxelType", "uint8"}, {"spacing", QVariantList{1, 1, 1}}}));
        QVERIFY(volume.fillSpherePhantom());
        QCOMPARE(volume.valueAt(16, 16, 16), 220.0);
        QCOMPARE(volume.valueAt(0, 0, 0), 0.0);
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 255, QStringLiteral("HU")));
        const QString id = transfer.addPoint(128, QColor(Qt::red), 0.5);
        QVERIFY(!id.isEmpty());
        QCOMPARE(transfer.colorAt(128).red(), 255);
        QCOMPARE(transfer.colorAt(128).alpha(), 128);
        const QVariantMap saved = transfer.saveSettings();
        QVERIFY(!transfer.movePoint(id, 255));
        QVERIFY(transfer.movePoint(id, 120));
        QVERIFY(transfer.restoreSettings(saved));
        QCOMPARE(transfer.point(id).value(QStringLiteral("value")).toDouble(), 128.0);
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        ImageSliceItem image;
        image.setVolume(&volume); image.setCursor(&cursor); image.setTransfer(&transfer);
        const QImage slice = image.sliceImage(33, 33);
        QCOMPARE(slice.size(), QSize(33, 33));
        QVERIFY(slice.pixelColor(16, 16).alpha() > slice.pixelColor(0, 0).alpha());
    }
};

QTEST_MAIN(VolumeImagingTest)
#include "tst_volume_imaging.moc"
