// SPDX-License-Identifier: LGPL-3.0-or-later
// Volume rendering known answers (D-146/D-147): the transfer table, composite
// accumulation with opacity correction, clip box, maximum intensity,
// orientation and physical spacing, picking, missing chunks, bounded
// residency, GPU texture packing, the GPU fallback order, and VolumeViewItem
// drawing the reference image (software) or the GPU equivalent (RHI run).
#include "transfer_function_model.h"
#include "volume_cursor.h"
#include "volume_raycast.h"
#include "volume_view_item.h"
#include "voxel_volume.h"
#include <QJsonDocument>
#include <QQuickWindow>
#include <QtTest>
#include <cmath>
#include <cstring>

using namespace QindaTK;

namespace {
QVariant stop(const char *id, double value, double opacity, const QColor &color = Qt::white)
{
    return QVariantMap{{"id", QString::fromLatin1(id)}, {"value", value},
                       {"color", color}, {"opacity", opacity}};
}
void fill(VoxelVolume &volume, double value)
{
    for (int z = 0; z < volume.sizeZ(); ++z)
        for (int y = 0; y < volume.sizeY(); ++y)
            for (int x = 0; x < volume.sizeX(); ++x) QVERIFY(volume.setVoxel(x, y, z, value));
}
// Opacity 0.6 at every value: table alpha 153 = 0.6 exactly, white.
void constantOpacity(TransferFunctionModel &transfer)
{
    QVERIFY(transfer.configure(0, 255));
    QVERIFY(transfer.replacePoints({stop("low", 0, 0.6), stop("high", 255, 0.6)}));
}
// Value v maps to gray v with opacity v / 255.
void rampTransfer(TransferFunctionModel &transfer)
{
    QVERIFY(transfer.configure(0, 255));
    QVERIFY(transfer.replacePoints({stop("low", 0, 0, Qt::black), stop("high", 255, 1)}));
}
VolumeRayParams topView()
{
    VolumeRayParams params;
    params.yawDegrees = 0;
    params.pitchDegrees = 0;
    params.samplingRate = 1;
    params.interpolation = VoxelVolume::Nearest;
    return params;
}
// Constant chunks of `value`; chunk x index `missingX` fails. Counts reads.
class ConstantSource : public VoxelChunkSource {
public:
    ConstantSource(char value, int missingX) : m_value(value), m_missingX(missingX) {}
    QByteArray readChunk(int cx, int, int, int, int) override
    {
        ++reads;
        if (cx == m_missingX) return {};
        return QByteArray(32 * 32 * 32, m_value);
    }
    int reads = 0;
private:
    char m_value;
    int m_missingX;
};
}

class VolumeRenderTest : public QObject {
    Q_OBJECT
private slots:
    void transferTableSamplesTheModel()
    {
        TransferFunctionModel transfer;
        rampTransfer(transfer);
        const VolumeTransferTable table = VolumeTransferTable::fromModel(transfer);
        QCOMPARE(int(table.rgba[200 * 4 + 0]), 200);
        QCOMPARE(int(table.rgba[200 * 4 + 3]), 200);
        double rgba[4];
        table.lookup(200, rgba);
        for (double channel : rgba) QVERIFY(qAbs(channel - 200.0 / 255) < 1e-9);
        table.lookup(-40, rgba);
        QCOMPARE(rgba[3], 0.0);
        table.lookup(std::nan(""), rgba);
        QCOMPARE(rgba[0] + rgba[1] + rgba[2] + rgba[3], 0.0);
        // Halfway between entries 10 and 11 of a ramp.
        table.lookup(10.5, rgba);
        QVERIFY(qAbs(rgba[3] - 10.5 / 255) < 1e-9);
        const VolumeTransferTable window = VolumeTransferTable::fromWindow(127.5, 255);
        QCOMPARE(window.domainMin, 0.0);
        QCOMPARE(window.domainMax, 255.0);
        QCOMPARE(int(window.rgba[77 * 4 + 3]), 77);
        QCOMPARE(window.image().pixel(77, 0), qRgba(77, 77, 77, 77));
    }

    void compositeAccumulatesWithOpacityCorrection()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 4}, {"sizeY", 4}, {"sizeZ", 4}}));
        fill(volume, 100);
        TransferFunctionModel transfer;
        constantOpacity(transfer);
        const VolumeTransferTable table = VolumeTransferTable::fromModel(transfer);
        VolumeRayParams params = topView();
        // The centre ray crosses 4 world units: four samples of α 0.6 give
        // A = 1 − 0.4⁴ = 0.9744 → 248 (no early termination below 0.99).
        QImage image = VolumeRaycaster::render(volume, table, params, QSize(9, 9));
        QCOMPARE(image.pixel(4, 4), qRgba(248, 248, 248, 248));
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0); // corner rays miss the box
        // Twice the samples with α' = 1 − 0.4^½ reach the same opacity.
        params.samplingRate = 2;
        image = VolumeRaycaster::render(volume, table, params, QSize(9, 9));
        QCOMPARE(image.pixel(4, 4), qRgba(248, 248, 248, 248));
        // Linear interpolation of a constant field is the same field.
        params.interpolation = VoxelVolume::Linear;
        image = VolumeRaycaster::render(volume, table, params, QSize(9, 9));
        QCOMPARE(image.pixel(4, 4), qRgba(248, 248, 248, 248));
    }

    void clipBoxShortensRays()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 4}, {"sizeY", 4}, {"sizeZ", 4}}));
        fill(volume, 100);
        TransferFunctionModel transfer;
        constantOpacity(transfer);
        VolumeRayParams params = topView();
        params.clipHigh[2] = 0.5; // keep z indices −0.5 … 1.5: two samples
        const QImage image = VolumeRaycaster::render(volume, VolumeTransferTable::fromModel(transfer),
                                                     params, QSize(9, 9));
        QCOMPARE(image.pixel(4, 4), qRgba(214, 214, 214, 214)); // 1 − 0.4² = 0.84
        params.clipLow[2] = 0.5; // empty box: no rays
        QVERIFY(!VolumeRayGeometry::compute(volume, params, QSizeF(9, 9)).valid);
    }

    void earlyTerminationAndPhysicalSpacing()
    {
        // 2 slices of 2 mm: the centre ray still crosses 4 world units, so it
        // matches the isotropic 4×4×4 answer; spacing is physical, not counts.
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 4}, {"sizeY", 4}, {"sizeZ", 2},
                                  {"spacing", QVariantList{1, 1, 2}}}));
        fill(volume, 100);
        TransferFunctionModel transfer;
        constantOpacity(transfer);
        const VolumeTransferTable table = VolumeTransferTable::fromModel(transfer);
        QCOMPARE(VolumeRaycaster::render(volume, table, topView(), QSize(9, 9)).pixel(4, 4),
                 qRgba(248, 248, 248, 248));
        // 8 world units: accumulation stops after the 6th sample (0.9959).
        QVERIFY(volume.configure({{"sizeX", 4}, {"sizeY", 4}, {"sizeZ", 4},
                                  {"spacing", QVariantList{1, 1, 2}}}));
        fill(volume, 100);
        const QImage image = VolumeRaycaster::render(volume, table, topView(), QSize(15, 15));
        QCOMPARE(image.pixel(7, 7), qRgba(254, 254, 254, 254));
    }

    void maximumIntensityOfKnownPhantom()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 17}, {"sizeY", 17}, {"sizeZ", 17}}));
        QVERIFY(volume.fillSpherePhantom());
        TransferFunctionModel transfer;
        rampTransfer(transfer);
        VolumeRayParams params = topView();
        params.mode = VolumeRenderMode::MaximumIntensity;
        const QImage image = VolumeRaycaster::render(volume, VolumeTransferTable::fromModel(transfer),
                                                     params, QSize(17, 17));
        // Core 220: gray 220 × opacity 220/255, premultiplied → 190.
        QCOMPARE(image.pixel(8, 8), qRgba(190, 190, 190, 220));
        // A ray that misses the volume box is transparent.
        QCOMPARE(image.pixel(8, 1), qRgba(0, 0, 0, 0));
    }

    void orientationPlacesVoxelsInWorld()
    {
        // Index +X is world +Y: a hot voxel at index (4, 2, 2) lies 2 mm
        // above the centre on screen (yaw 0, pitch 0: +Y up), not to its right.
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 5}, {"sizeY", 5}, {"sizeZ", 5},
                                  {"orientation", QVariantList{0, -1, 0, 1, 0, 0, 0, 0, 1}}}));
        QVERIFY(volume.setVoxel(4, 2, 2, 200));
        TransferFunctionModel transfer;
        rampTransfer(transfer);
        VolumeRayParams params = topView();
        params.mode = VolumeRenderMode::MaximumIntensity;
        const QImage image = VolumeRaycaster::render(volume, VolumeTransferTable::fromModel(transfer),
                                                     params, QSize(64, 64));
        // 64 px / √75 mm = 7.39 px per mm: row 17 is 1.96 mm above the centre.
        QCOMPARE(image.pixel(32, 17), qRgba(157, 157, 157, 200));
        QCOMPARE(qAlpha(image.pixel(47, 32)), 0);
    }

    void pickFindsTheShellSurface()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 17}, {"sizeY", 17}, {"sizeZ", 17}}));
        QVERIFY(volume.fillSpherePhantom());
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 255));
        QVERIFY(transfer.replacePoints({stop("low", 0, 0, Qt::black), stop("shell", 110, 1, Qt::cyan),
                                        stop("high", 255, 1)}));
        VolumeRayParams params = topView();
        params.samplingRate = 2;
        double voxel[3];
        // Samples at z = 16.25, 15.75, …; the first in the shell cell z = 13
        // (radius 5.44, |13 − 8| = 5) is 13.25 and is fully opaque.
        QVERIFY(VolumeRaycaster::pick(volume, VolumeTransferTable::fromModel(transfer), params,
                                      QSizeF(17, 17), QPointF(8.5, 8.5), voxel));
        QCOMPARE(voxel[0], 8.0);
        QCOMPARE(voxel[1], 8.0);
        QVERIFY(qAbs(voxel[2] - 13.25) < 1e-9);
        // Maximum intensity picks the core sample with the largest value.
        params.mode = VolumeRenderMode::MaximumIntensity;
        QVERIFY(VolumeRaycaster::pick(volume, VolumeTransferTable::fromModel(transfer), params,
                                      QSizeF(17, 17), QPointF(8.5, 8.5), voxel));
        QCOMPARE(qRound(volume.sampleValue(voxel[0], voxel[1], voxel[2], 0, 0, VoxelVolume::Nearest)), 220);
        QVERIFY(!VolumeRaycaster::pick(volume, VolumeTransferTable::fromModel(transfer), params,
                                       QSizeF(17, 17), QPointF(0.5, 0.5), voxel));
    }

    void missingChunksAreTransparent()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 64}, {"sizeY", 32}, {"sizeZ", 32}}));
        volume.setMaximumResidentBytes(32 * 32 * 32);
        auto source = std::make_shared<ConstantSource>(char(100), 1);
        volume.setSource(source);
        TransferFunctionModel transfer;
        constantOpacity(transfer);
        const QImage image = VolumeRaycaster::render(volume, VolumeTransferTable::fromModel(transfer),
                                                     topView(), QSize(64, 32));
        QCOMPARE(image.pixel(25, 16), qRgba(254, 254, 254, 254)); // chunk 0, six samples
        QCOMPARE(qAlpha(image.pixel(40, 16)), 0);                 // chunk 1 is missing
        QVERIFY(volume.residentBytes() <= volume.maximumResidentBytes());
        QByteArray packed;
        QVERIFY(!VolumeRaycaster::packTexture(volume, 0, 0, &packed));
        QVERIFY(packed.isEmpty());
    }

    void residencyStaysBoundedWhileRendering()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 128}, {"sizeY", 128}, {"sizeZ", 64}}));
        volume.setMaximumResidentBytes(4 * 32 * 32 * 32); // 4 of the 32 chunks
        auto source = std::make_shared<ConstantSource>(char(100), -1);
        volume.setSource(source);
        TransferFunctionModel transfer;
        constantOpacity(transfer);
        VolumeRayParams params;
        params.interpolation = VoxelVolume::Linear;
        const QImage image = VolumeRaycaster::render(volume, VolumeTransferTable::fromModel(transfer),
                                                     params, QSize(48, 48));
        QVERIFY(qAlpha(image.pixel(24, 24)) >= 250);
        QVERIFY(volume.residentBytes() <= 4 * 32 * 32 * 32);
        QVERIFY(source->reads > 32); // chunks were evicted and read again
        QByteArray packed;
        QVERIFY(VolumeRaycaster::packTexture(volume, 0, 0, &packed));
        QCOMPARE(packed.size(), qsizetype(128) * 128 * 64);
        QCOMPARE(packed.count(char(100)), packed.size());
        QVERIFY(volume.residentBytes() <= 4 * 32 * 32 * 32);
    }

    void packTextureLayout()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 40}, {"sizeY", 33}, {"sizeZ", 2}, {"voxelType", "uint16"}}));
        for (int z = 0; z < 2; ++z)
            for (int y = 0; y < 33; ++y)
                for (int x = 0; x < 40; ++x) QVERIFY(volume.setVoxel(x, y, z, x + 100 * y + 5000 * z));
        QByteArray packed;
        QVERIFY(VolumeRaycaster::packTexture(volume, 0, 0, &packed));
        QCOMPARE(VolumeRaycaster::textureBytes(volume), qint64(40) * 33 * 2 * 2);
        QCOMPARE(packed.size(), qsizetype(VolumeRaycaster::textureBytes(volume)));
        const auto at = [&packed](int x, int y, int z) {
            quint16 value = 0;
            std::memcpy(&value, packed.constData() + ((qsizetype(z) * 33 + y) * 40 + x) * 2, 2);
            return int(value);
        };
        QCOMPARE(at(0, 0, 0), 0);
        QCOMPARE(at(39, 32, 1), 39 + 3200 + 5000);
        QCOMPARE(at(33, 1, 1), 33 + 100 + 5000);
        QVERIFY(!VolumeRaycaster::packTexture(volume, 1, 0, &packed)); // no channel 1
    }

    void gpuFallbackReasons()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8}, {"voxelType", "uint16"}}));
        VolumeGpuProbe probe{true, true, true, true, 4096};
        const qint64 budget = 1 << 20;
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, budget, false), QString());
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, budget, true),
                 QStringLiteral("Reference renderer requested"));
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(nullptr, probe, budget, false), QStringLiteral("No volume"));
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, 1023, false),
                 QStringLiteral("The volume exceeds the GPU texture budget"));
        probe.maximumTextureSide = 4;
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, budget, false),
                 QStringLiteral("The volume exceeds the 4-voxel GPU texture side"));
        probe.formatSupported = false;
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, budget, false),
                 QStringLiteral("The GPU cannot sample uint16 3D textures"));
        probe.threeDimensionalTextures = false;
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, budget, false),
                 QStringLiteral("The GPU has no 3D textures"));
        probe.shaders = false;
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, budget, false),
                 QStringLiteral("GPU volume shaders are not built"));
        probe.rhi = false;
        QCOMPARE(VolumeRaycaster::gpuUnavailableReason(&volume, probe, budget, false),
                 QStringLiteral("The scene graph has no GPU (software renderer)"));
    }

    void viewSettingsRoundTripWithoutTouchingVoxels()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8}}));
        const quint64 revision = volume.revision();
        VolumeViewItem view;
        view.setVolume(&volume);
        view.setYaw(200); // wraps to −160
        QCOMPARE(view.yaw(), -160.0);
        view.setPitch(120);
        QCOMPARE(view.pitch(), 90.0);
        view.setMode(VolumeViewItem::MaximumIntensity);
        view.setClipHigh(QVector3D(1, 0.5f, 1));
        const QVariantMap saved = view.saveSettings();
        const QVariantMap restored = QJsonDocument::fromJson(
            QJsonDocument::fromVariant(saved).toJson()).toVariant().toMap();
        view.resetView();
        view.setMode(VolumeViewItem::Composite);
        view.setClipHigh(QVector3D(1, 1, 1));
        QVERIFY(view.restoreSettings(restored));
        QCOMPARE(view.yaw(), -160.0);
        QCOMPARE(view.pitch(), 90.0);
        QCOMPARE(view.mode(), VolumeViewItem::MaximumIntensity);
        QCOMPARE(view.clipHigh(), QVector3D(1, 0.5f, 1));
        QVariantMap bad = restored;
        bad.insert("clipLow", QVariantList{0, 0.6, 0}); // low above high on Y
        QVERIFY(!view.restoreSettings(bad));
        QCOMPARE(view.lastError(), QStringLiteral("Invalid volume view settings"));
        QCOMPARE(view.clipLow(), QVector3D(0, 0, 0));
        view.setClipLow(QVector3D(0, 0.7f, 0));
        QCOMPARE(view.clipLow(), QVector3D(0, 0, 0));
        QCOMPARE(volume.revision(), revision);
    }

    void viewItemDrawsTheReferenceImage()
    {
        if (qEnvironmentVariable("QT_QUICK_BACKEND") == QLatin1String("rhi"))
            QSKIP("the software registration checks the reference path");
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 17}, {"sizeY", 17}, {"sizeZ", 17}}));
        QVERIFY(volume.fillSpherePhantom());
        TransferFunctionModel transfer;
        rampTransfer(transfer);
        VolumeCursor cursor;
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(17, 17);
        VolumeViewItem view(window.contentItem());
        view.setSize(QSizeF(17, 17));
        view.setVolume(&volume);
        view.setTransfer(&transfer);
        view.setCursor(&cursor);
        view.setYaw(0);
        view.setPitch(0);
        view.setMode(VolumeViewItem::MaximumIntensity);
        view.setInterpolation(VoxelVolume::Nearest);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(view.renderPath(), QStringLiteral("reference"));
        QCOMPARE(view.fallbackReason(), QStringLiteral("The scene graph has no GPU (software renderer)"));
        QCOMPARE(view.referenceSize(), QSize(17, 17));
        const QImage expected = view.renderReference(17, 17);
        QCOMPARE(expected.pixel(8, 8), qRgba(190, 190, 190, 220));
        // Premultiplied over a black window: the grabbed RGB is the reference RGB.
        const QImage grabbed = window.grabWindow();
        QCOMPARE(qRed(grabbed.pixel(8, 8)), 190);
        QCOMPARE(qGreen(grabbed.pixel(8, 8)), 190);
        // Picking moves the shared cursor to the maximum sample.
        QVERIFY(view.setCursorFromPick(8.5, 8.5));
        QCOMPARE(cursor.x(), 8.0);
        QCOMPARE(cursor.y(), 8.0);
        view.setRenderer(VolumeViewItem::Reference);
        QTRY_COMPARE(view.fallbackReason(), QStringLiteral("Reference renderer requested"));
    }

    void gpuPathMatchesReference()
    {
        // AGENT-NOTE: registered twice in CMake; only tst_volume_render_rhi
        // (QT_QUICK_BACKEND=rhi) runs this, and it skips without a hardware
        // RHI context. GPU qualification across drivers is WP-088..094 work.
        if (qEnvironmentVariable("QT_QUICK_BACKEND") != QLatin1String("rhi"))
            QSKIP("GPU comparison runs only in the tst_volume_render_rhi registration");
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 33}, {"sizeY", 33}, {"sizeZ", 33}}));
        QVERIFY(volume.fillSpherePhantom());
        TransferFunctionModel transfer;
        rampTransfer(transfer);
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(64, 64);
        VolumeViewItem view(window.contentItem());
        view.setSize(QSizeF(64, 64));
        view.setVolume(&volume);
        view.setTransfer(&transfer);
        view.setMode(VolumeViewItem::MaximumIntensity);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(!view.renderPath().isEmpty());
        if (view.renderPath() != QLatin1String("gpu"))
            QSKIP(qPrintable(QStringLiteral("GPU path unavailable: %1").arg(view.fallbackReason())));
        const QImage expected = view.renderReference(64, 64);
        const QImage grabbed = window.grabWindow();
        for (const QPoint point : {QPoint(32, 32), QPoint(31, 33), QPoint(1, 1)}) {
            const QRgb want = expected.pixel(point), got = grabbed.pixel(point);
            QVERIFY2(qAbs(qRed(want) - qRed(got)) <= 4 && qAbs(qGreen(want) - qGreen(got)) <= 4
                         && qAbs(qBlue(want) - qBlue(got)) <= 4,
                     qPrintable(QStringLiteral("pixel %1,%2: reference %3 GPU %4").arg(point.x())
                                    .arg(point.y()).arg(want, 8, 16).arg(got, 8, 16)));
        }
    }
};

QTEST_MAIN(VolumeRenderTest)
#include "tst_volume_render.moc"
