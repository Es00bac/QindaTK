// SPDX-License-Identifier: LGPL-3.0-or-later
// VoxelVolume data contract: typed little-endian chunks, calibrated world
// coordinates, cell-centred sampling, provider residency and the sphere
// phantom's known answers. Slice geometry and transfer mapping live in
// tst_volume_slices.cpp.
#include "image_slice_item.h"
#include "transfer_function_model.h"
#include "volume_cursor.h"
#include "voxel_volume.h"
#include <QSignalSpy>
#include <QtEndian>
#include <QtTest>
#include <cmath>
#include <cstring>
#include <limits>

using namespace QindaTK;

namespace {
constexpr int ChunkVoxels = 32 * 32 * 32;

// Records every provider read; chunks listed in `missing` fail (empty block).
struct CountingSource : VoxelChunkSource {
    QList<int> reads;
    QList<int> missing;
    char fill = 0;
    QByteArray readChunk(int cx, int, int, int, int) override
    {
        reads.append(cx);
        if (missing.contains(cx)) return {};
        return QByteArray(ChunkVoxels, fill ? fill : char(cx + 1));
    }
};

QVariantMap cube(int size, const QString &type)
{
    return {{"sizeX", size}, {"sizeY", size}, {"sizeZ", size}, {"voxelType", type}};
}
QVariantList point(double x, double y, double z) { return {x, y, z}; }
}

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
        QCOMPARE(volume.worldFromVoxel(1, 2, 2), point(4, 22, 38));
        QCOMPARE(volume.voxelFromWorld(4, 22, 38), point(1, 2, 2));
        const quint64 revision = volume.revision();
        QVERIFY(!volume.configure({{"sizeX", 3}, {"sizeY", 3}, {"sizeZ", 3},
            {"orientation", QVariantList{1, 0, 0, 0, 2, 0, 0, 0, 1}}}));
        QCOMPARE(volume.revision(), revision);
        QCOMPARE(volume.valueAt(1, 1, 1), 800.0);
    }
    void invalidConfigureLeavesVolumeUnchanged_data()
    {
        QTest::addColumn<QVariantMap>("patch");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        QTest::newRow("zero size") << QVariantMap{{"sizeX", 0}};
        QTest::newRow("oversize") << QVariantMap{{"sizeY", 8193}};
        QTest::newRow("fractional size") << QVariantMap{{"sizeZ", 3.5}};
        QTest::newRow("missing size") << QVariantMap{{"sizeZ", QVariant()}};
        QTest::newRow("no channels") << QVariantMap{{"channels", 0}};
        QTest::newRow("too many channels") << QVariantMap{{"channels", 65}};
        QTest::newRow("too many frames") << QVariantMap{{"frames", 4097}};
        QTest::newRow("unknown type") << QVariantMap{{"voxelType", "int32"}};
        QTest::newRow("zero spacing") << QVariantMap{{"spacing", QVariantList{1, 0, 1}}};
        QTest::newRow("negative spacing") << QVariantMap{{"spacing", QVariantList{1, 1, -2}}};
        QTest::newRow("nan spacing") << QVariantMap{{"spacing", QVariantList{1, nan, 1}}};
        QTest::newRow("two spacings") << QVariantMap{{"spacing", QVariantList{1, 1}}};
        QTest::newRow("infinite origin") << QVariantMap{{"origin", QVariantList{0, inf, 0}}};
        QTest::newRow("scaled axis") << QVariantMap{{"orientation", QVariantList{2, 0, 0, 0, 1, 0, 0, 0, 1}}};
        QTest::newRow("skewed axes") << QVariantMap{{"orientation", QVariantList{1, 0.1, 0, 0, 1, 0, 0, 0, 1}}};
        QTest::newRow("eight entries") << QVariantMap{{"orientation", QVariantList{1, 0, 0, 0, 1, 0, 0, 0}}};
        QTest::newRow("empty unit") << QVariantMap{{"unit", ""}};
    }
    void invalidConfigureLeavesVolumeUnchanged()
    {
        QFETCH(QVariantMap, patch);
        VoxelVolume volume;
        QVERIFY(volume.configure(cube(3, "uint16")));
        QVERIFY(volume.setVoxel(1, 1, 1, 800));
        QVariantMap description = cube(3, "uint16");
        for (auto it = patch.cbegin(); it != patch.cend(); ++it) {
            if (it.value().isValid()) description.insert(it.key(), it.value());
            else description.remove(it.key());
        }
        const quint64 revision = volume.revision();
        QVERIFY(!volume.configure(description));
        QVERIFY(!volume.lastError().isEmpty());
        QCOMPARE(volume.revision(), revision);
        QCOMPARE(volume.sizeX(), 3);
        QCOMPARE(volume.voxelType(), QStringLiteral("uint16"));
        QCOMPARE(volume.valueAt(1, 1, 1), 800.0);
    }
    void orientationFlipsRoundTrip()
    {
        VoxelVolume volume;
        const QVariantMap base{{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8},
                               {"spacing", QVariantList{0.5, 2, 3}}, {"origin", QVariantList{10, 20, 30}}};
        QVariantMap rotated = base;  // 180° about Y: X and Z flip, determinant +1
        rotated.insert("orientation", QVariantList{-1, 0, 0, 0, 1, 0, 0, 0, -1});
        QVERIFY(volume.configure(rotated));
        QCOMPARE(volume.worldFromVoxel(2, 3, 4), point(9, 26, 18));
        QCOMPARE(volume.voxelFromWorld(9, 26, 18), point(2, 3, 4));
        QVariantMap mirrored = base;  // X and Y swapped: a reflection, determinant −1
        mirrored.insert("orientation", QVariantList{0, 1, 0, 1, 0, 0, 0, 0, 1});
        QVERIFY(volume.configure(mirrored));
        QCOMPARE(volume.worldFromVoxel(2, 3, 4), point(16, 21, 42));
        QCOMPARE(volume.voxelFromWorld(16, 21, 42), point(2, 3, 4));
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        QVERIFY(cursor.setWorldPoint(16, 21, 42));
        QCOMPARE(cursor.x(), 2.0);
        QCOMPARE(cursor.y(), 3.0);
        QCOMPARE(cursor.z(), 4.0);
        QCOMPARE(cursor.worldPoint(), point(16, 21, 42));
    }
    void littleEndianTypedChunks()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure(cube(32, "uint16")));
        QByteArray words(ChunkVoxels * 2, '\0');
        const int wordOffset = (3 * 32 * 32 + 2 * 32 + 1) * 2;  // voxel (1, 2, 3)
        words[wordOffset] = char(0x34);
        words[wordOffset + 1] = char(0x12);
        QVERIFY(volume.putChunk(0, 0, 0, 0, 0, words));
        QCOMPARE(volume.valueAt(1, 2, 3), double(0x1234));
        QCOMPARE(volume.valueAt(0, 0, 0), 0.0);
        QVERIFY(!volume.putChunk(0, 0, 0, 0, 0, QByteArray(ChunkVoxels, '\0')));
        QVERIFY(!volume.putChunk(1, 0, 0, 0, 0, words));
        QVERIFY(!volume.putChunk(-1, 0, 0, 0, 0, words));
        QVERIFY(!volume.putChunk(1 << 28, 0, 0, 0, 0, words));
        QVERIFY(!volume.lastError().isEmpty());

        QVERIFY(volume.configure(cube(32, "float32")));
        QByteArray floats(ChunkVoxels * 4, '\0');
        const int last = (ChunkVoxels - 1) * 4;  // voxel (31, 31, 31)
        const char minusTwoAndHalf[] = {0x00, 0x00, 0x20, char(0xc0)};  // -2.5f, LE
        std::memcpy(floats.data() + last, minusTwoAndHalf, 4);
        const quint32 nanBits = qToLittleEndian<quint32>(0x7fc00000u);
        std::memcpy(floats.data() + 4, &nanBits, 4);  // voxel (1, 0, 0)
        QVERIFY(volume.putChunk(0, 0, 0, 0, 0, floats));
        QCOMPARE(volume.valueAt(31, 31, 31), -2.5);
        QVERIFY(!volume.sample(1, 0, 0).value(QStringLiteral("valid")).toBool());
        QVERIFY(volume.setVoxel(2, 0, 0, 1.0e-3));
        QCOMPARE(volume.valueAt(2, 0, 0), double(1.0e-3f));
        QVERIFY(!volume.setVoxel(2, 0, 0, 1.0e39));

        QVERIFY(volume.configure(cube(4, "uint8")));
        QVERIFY(volume.setVoxel(0, 0, 0, 255.4));
        QCOMPARE(volume.valueAt(0, 0, 0), 255.0);
        QVERIFY(!volume.setVoxel(0, 0, 0, 255.6));
        QVERIFY(volume.setVoxel(1, 0, 0, -0.4));
        QCOMPARE(volume.valueAt(1, 0, 0), 0.0);
        QVERIFY(!volume.setVoxel(1, 0, 0, -0.6));
        QVERIFY(!volume.setVoxel(1, 0, 0, std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!volume.setVoxel(4, 0, 0, 1));
        QVERIFY(!volume.lastError().isEmpty());
    }
    void sphereSamplesAtKnownWorldPoints()
    {
        // Anisotropic voxels with the centre voxel (20, 20, 10) at world 0:
        // r = 0.32 × min(32.8, 32.8, 33.6) mm = 10.496 mm, core within 5.7728.
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 41}, {"sizeY", 41}, {"sizeZ", 21},
            {"voxelType", "uint8"}, {"spacing", QVariantList{0.8, 0.8, 1.6}},
            {"origin", QVariantList{-16, -16, -16}}, {"unit", "mm"}, {"valueUnit", "a.u."}}));
        QVERIFY(volume.fillSpherePhantom());
        const auto valueAtWorld = [&volume](double x, double y, double z) {
            const QVariantList voxel = volume.voxelFromWorld(x, y, z);
            const QVariantMap sample = volume.sample(voxel[0].toDouble(), voxel[1].toDouble(),
                                                     voxel[2].toDouble());
            return sample.value(QStringLiteral("valid")).toBool()
                ? sample.value(QStringLiteral("value")).toDouble() : -1.0;
        };
        QCOMPARE(valueAtWorld(0, 0, 0), 220.0);
        QCOMPARE(valueAtWorld(2.4, 0, 0), 220.0);
        QCOMPARE(valueAtWorld(8, 0, 0), 110.0);
        QCOMPARE(valueAtWorld(0, 8.8, 0), 110.0);
        QCOMPARE(valueAtWorld(0, 11.2, 0), 0.0);
        QCOMPARE(valueAtWorld(0, 0, 9.6), 110.0);
        QCOMPARE(valueAtWorld(0, 0, 11.2), 0.0);
        QCOMPARE(valueAtWorld(16.4, 0, 0), 0.0);    // outer half cell of x = 40
        QCOMPARE(valueAtWorld(16.5, 0, 0), -1.0);   // beyond the volume
        QCOMPARE(volume.sample(20, 20, 10).value(QStringLiteral("unit")).toString(), QStringLiteral("a.u."));
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        QCOMPARE(cursor.worldPoint(), point(0, 0, 0));
        QVERIFY(cursor.setWorldPoint(8, 0, 9.6));
        QCOMPARE(cursor.x(), 30.0);
        QCOMPARE(cursor.z(), 16.0);
    }
    void linearInterpolationAndEdges()
    {
        VoxelVolume volume;
        QVERIFY(!volume.sample(0, 0, 0).value(QStringLiteral("valid")).toBool());
        QVERIFY(volume.configure(cube(2, "float32")));
        for (int z = 0; z < 2; ++z)
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x) QVERIFY(volume.setVoxel(x, y, z, 1 + x + 2 * y + 4 * z));
        const auto linear = [&volume](double x, double y, double z) {
            return volume.sampleValue(x, y, z, 0, 0, VoxelVolume::Linear);
        };
        QCOMPARE(linear(0.5, 0.5, 0.5), 4.5);
        QCOMPARE(linear(0.25, 0, 1), 5.25);
        QCOMPARE(linear(1, 1, 1), 8.0);
        QCOMPARE(linear(-0.25, 0, 0), 1.0);   // edge half cell clamps to the centre
        QCOMPARE(linear(1.5, 1, 1), 8.0);
        QVERIFY(std::isnan(linear(-0.51, 0, 0)));
        QVERIFY(std::isnan(linear(0, 1.51, 0)));
        QVERIFY(std::isnan(linear(0, std::numeric_limits<double>::infinity(), 0)));
        QCOMPARE(volume.sampleValue(0.49, 0.5, 0, 0, 0, VoxelVolume::Nearest), 3.0);
        QCOMPARE(volume.sampleValue(-0.5, 1.5, 0, 0, 0, VoxelVolume::Nearest), 3.0);
        QVERIFY(std::isnan(volume.sampleValue(0, 0, 0, 1, 0, VoxelVolume::Nearest)));
        QVERIFY(std::isnan(volume.sampleValue(0, 0, 0, 0, 1, VoxelVolume::Linear)));
    }
    void missingProviderChunksAreInvalid()
    {
        auto source = std::make_shared<CountingSource>();
        source->missing = {1};
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 96}, {"sizeY", 32}, {"sizeZ", 32}}));
        volume.setSource(source);
        QCOMPARE(volume.valueAt(0, 0, 0), 1.0);
        QVERIFY(std::isnan(volume.valueAt(40, 0, 0)));
        QVERIFY(!volume.sample(40, 0, 0).value(QStringLiteral("valid")).toBool());
        QVERIFY(std::isnan(volume.sampleValue(31.5, 0, 0, 0, 0, VoxelVolume::Linear)));
        QCOMPARE(volume.sampleValue(31, 0, 0, 0, 0, VoxelVolume::Linear), 1.0);
        QCOMPARE(volume.valueAt(80, 0, 0), 3.0);
        QVERIFY(!volume.setVoxel(40, 0, 0, 5));  // nothing to copy on write
        QVERIFY(!volume.lastError().isEmpty());
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
    void providerCacheIsBoundedLeastRecentlyUsed()
    {
        auto source = std::make_shared<CountingSource>();
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 96}, {"sizeY", 32}, {"sizeZ", 32}}));
        volume.setMaximumResidentBytes(2 * ChunkVoxels);
        volume.setSource(source);
        const auto read = [&volume](int chunk) { return volume.valueAt(chunk * 32 + 5, 3, 4); };
        QCOMPARE(read(0), 1.0);
        QCOMPARE(read(1), 2.0);
        QCOMPARE(read(0), 1.0);                   // hit: chunk 0 becomes most recent
        QCOMPARE(source->reads, (QList<int>{0, 1}));
        QCOMPARE(read(2), 3.0);                   // evicts chunk 1, the least recent
        QCOMPARE(volume.residentBytes(), qint64(2 * ChunkVoxels));
        QCOMPARE(read(0), 1.0);
        QCOMPARE(source->reads, (QList<int>{0, 1, 2}));
        QCOMPARE(read(1), 2.0);                   // miss again; evicts chunk 2
        QCOMPARE(read(0), 1.0);
        QCOMPARE(source->reads, (QList<int>{0, 1, 2, 1}));
        QVERIFY(volume.residentBytes() <= volume.maximumResidentBytes());
        volume.setMaximumResidentBytes(ChunkVoxels);
        QCOMPARE(volume.residentBytes(), qint64(ChunkVoxels));
        volume.setMaximumResidentBytes(1000);    // below one chunk: rejected
        QCOMPARE(volume.maximumResidentBytes(), qint64(ChunkVoxels));
        QVERIFY(!volume.lastError().isEmpty());
    }
    void ownedChunksRespectResidentLimit()
    {
        VoxelVolume volume;
        volume.setMaximumResidentBytes(ChunkVoxels);
        QVERIFY(volume.configure({{"sizeX", 64}, {"sizeY", 32}, {"sizeZ", 32}}));
        QVERIFY(volume.setVoxel(0, 0, 0, 1));
        QVERIFY(!volume.setVoxel(40, 0, 0, 1));
        QVERIFY(volume.lastError().contains(QStringLiteral("Resident")));
        QCOMPARE(volume.residentBytes(), qint64(ChunkVoxels));
        QCOMPARE(volume.valueAt(40, 0, 0), 0.0);  // unwritten owned voxels read as zero
        const quint64 revision = volume.revision();
        QVERIFY(!volume.configure(cube(32, "float32")));  // one float32 chunk is 128 KiB
        QCOMPARE(volume.revision(), revision);
        QVERIFY(!volume.fillSpherePhantom(0.6));
        QVERIFY(!volume.lastError().isEmpty());
    }
    void copyOnWriteKeepsProviderVoxels()
    {
        auto source = std::make_shared<CountingSource>();
        source->fill = 7;
        VoxelVolume volume;
        QVERIFY(volume.configure(cube(32, "uint8")));
        volume.setSource(source);
        QVERIFY(volume.setVoxel(1, 1, 1, 9));
        QCOMPARE(volume.valueAt(1, 1, 1), 9.0);
        QCOMPARE(volume.valueAt(2, 2, 2), 7.0);
        QCOMPARE(volume.residentBytes(), qint64(ChunkVoxels));
        QCOMPARE(source->reads.size(), 1);        // the owned copy now serves reads
        QVERIFY(volume.putChunk(0, 0, 0, 0, 0, QByteArray(ChunkVoxels, char(3))));
        QCOMPARE(volume.valueAt(1, 1, 1), 3.0);
        QCOMPARE(volume.residentBytes(), qint64(ChunkVoxels));
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
    void cursorRecentresOnNewShapeOnly()
    {
        VoxelVolume volume;
        VolumeCursor cursor;
        cursor.setVolume(&volume);  // bound before the volume is configured
        QVERIFY(volume.configure({{"sizeX", 9}, {"sizeY", 7}, {"sizeZ", 5}}));
        QCOMPARE(cursor.x(), 4.0);
        QCOMPARE(cursor.y(), 3.0);
        QCOMPARE(cursor.z(), 2.0);
        QVERIFY(cursor.setVoxelPoint(1, 1, 1));
        QSignalSpy spy(&cursor, &VolumeCursor::changed);
        QVERIFY(volume.setVoxel(2, 2, 2, 50));    // data edit: point and world unchanged
        QCOMPARE(spy.count(), 0);
        QVERIFY(volume.configure({{"sizeX", 9}, {"sizeY", 7}, {"sizeZ", 5},
                                  {"spacing", QVariantList{2, 2, 2}}}));
        QCOMPARE(spy.count(), 1);                 // same shape: point kept, world moved
        QCOMPARE(cursor.x(), 1.0);
        QCOMPARE(cursor.worldPoint(), point(2, 2, 2));
        QVERIFY(volume.configure(cube(11, "uint8")));
        QCOMPARE(cursor.x(), 5.0);
        QVERIFY(!cursor.setVoxelPoint(std::numeric_limits<double>::quiet_NaN(), 0, 0));
        QVERIFY(cursor.setVoxelPoint(-4, 40, 5.5));
        QCOMPARE(cursor.x(), 0.0);
        QCOMPARE(cursor.y(), 10.0);
        QCOMPARE(cursor.z(), 5.5);
    }
};

QTEST_MAIN(VolumeImagingTest)
#include "tst_volume_imaging.moc"
