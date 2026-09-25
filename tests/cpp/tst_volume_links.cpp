// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-086 audit gaps: multi-channel and time volumes end to end, coordinate
// mapping after a reload (D-260), linked views and overlays rejecting
// incompatible geometry and units, saved slice/volume settings bound to their
// dataset, explicit below/above/missing transfer colours, presets, the
// exported ramp (D-263) and undoable settings restore (D-262).
#include "image_slice_item.h"
#include "transfer_function_model.h"
#include "volume_cursor.h"
#include "volume_raycast.h"
#include "volume_view_item.h"
#include "voxel_volume.h"
#include <QJsonDocument>
#include <QMutex>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {
QVariant stop(const char *id, double value, double opacity, const QColor &color = Qt::white)
{
    return QVariantMap{{"id", QString::fromLatin1(id)}, {"value", value},
                       {"color", color}, {"opacity", opacity}};
}
QVariantMap viaJson(const QVariantMap &map)
{
    return QJsonDocument::fromJson(QJsonDocument::fromVariant(map).toJson()).toVariant().toMap();
}
// Chunk (channel c, frame f) holds 100·c + 10·f everywhere; records requests.
class LayerSource : public VoxelChunkSource {
public:
    QByteArray readChunk(int, int, int, int channel, int frame) override
    {
        QMutexLocker lock(&mutex);
        requests.append({channel, frame});
        return QByteArray(32 * 32 * 32, char(100 * channel + 10 * frame));
    }
    QMutex mutex;
    QList<QPair<int, int>> requests;
};
double worldDistance(const QVariantList &a, const QVariantList &b)
{
    double sum = 0;
    for (int i = 0; i < 3; ++i) sum += std::pow(a[i].toDouble() - b[i].toDouble(), 2);
    return std::sqrt(sum);
}
} // namespace

class VolumeLinksTest : public QObject {
    Q_OBJECT
private slots:
    void channelsAndFramesAreIndependentLayers()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 40}, {"sizeY", 6}, {"sizeZ", 6}, {"channels", 2}, {"frames", 3}}));
        for (int channel = 0; channel < 2; ++channel)
            for (int frame = 0; frame < 3; ++frame)
                for (int x = 0; x < 40; ++x)
                    QVERIFY(volume.setVoxel(x, 2, 3, 100 * channel + 10 * frame + (x >= 32 ? 5 : 0), channel, frame));
        // Chunk keys separate channels and frames on both sides of a chunk edge.
        QCOMPARE(volume.valueAt(1, 2, 3, 1, 2), 120.0);
        QCOMPARE(volume.valueAt(33, 2, 3, 1, 2), 125.0);
        QCOMPARE(volume.valueAt(33, 2, 3, 0, 1), 15.0);
        QCOMPARE(volume.sampleValue(31.5, 2, 3, 1, 1, VoxelVolume::Linear), 112.5);
        QVERIFY(!std::isfinite(volume.valueAt(1, 2, 3, 2, 0)));
        const QVariantMap histogram = volume.histogram(4, 0, 200, 1, 2);
        QCOMPARE(histogram.value("bins").toList().at(2).toInt(), 40); // 120 and 125 → bin 2
        QCOMPARE(histogram.value("frame").toInt(), 2);

        // A slice view shows the layer it names.
        ImageSliceItem slice;
        slice.setVolume(&volume);
        slice.setPlane(ImageSliceItem::Axial);
        slice.setWindowCenter(127.5);
        slice.setWindowWidth(255);
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        QVERIFY(cursor.setVoxelPoint(0, 2, 3));
        slice.setCursor(&cursor);
        slice.setChannel(1);
        slice.setFrame(2);
        QCOMPARE(qRed(slice.sliceImage(40, 6).pixel(1, 2)), 120);
        slice.setFrame(0);
        QCOMPARE(qRed(slice.sliceImage(40, 6).pixel(1, 2)), 100);

        // The GPU texture packs exactly one channel/frame.
        QByteArray packed;
        QVERIFY(VolumeRaycaster::packTexture(volume, 1, 2, &packed));
        QCOMPARE(quint8(packed.at((3 * 6 + 2) * 40 + 33)), quint8(125));
        QVERIFY(VolumeRaycaster::packTexture(volume, 0, 1, &packed));
        QCOMPARE(quint8(packed.at((3 * 6 + 2) * 40 + 33)), quint8(15));
        QVERIFY(!VolumeRaycaster::packTexture(volume, 2, 0, &packed));

        // The reference render and the saved view follow channel/frame.
        VolumeViewItem view;
        view.setVolume(&volume);
        view.setMode(VolumeViewItem::MaximumIntensity);
        view.setWindowCenter(127.5);
        view.setWindowWidth(255);
        view.setChannel(1);
        view.setFrame(2);
        const QImage hot = view.renderReference(24, 24);
        view.setChannel(0);
        view.setFrame(0);
        const QImage cold = view.renderReference(24, 24);
        QVERIFY(hot != cold);
        view.setChannel(1);
        view.setFrame(2);
        const QVariantMap saved = viaJson(view.saveSettings());
        QCOMPARE(saved.value("channel").toInt(), 1);
        view.setChannel(0);
        view.setFrame(0);
        QVERIFY(view.restoreSettings(saved));
        QCOMPARE(view.channel(), 1);
        QCOMPARE(view.frame(), 2);
        QCOMPARE(view.renderReference(24, 24), hot);
        QVariantMap outside = saved;
        outside.insert("frame", 3);
        QVERIFY(!view.restoreSettings(outside));
        QCOMPARE(view.lastError(), QStringLiteral("The saved channel or frame is outside the volume"));
        QCOMPARE(view.frame(), 2);
    }

    void providerReadsAreKeyedByChannelAndFrame()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8}, {"channels", 3}, {"frames", 2}}));
        auto source = std::make_shared<LayerSource>();
        volume.setSource(source);
        QCOMPARE(volume.valueAt(1, 1, 1, 2, 1), 210.0);
        QCOMPARE(volume.valueAt(1, 1, 1, 1, 0), 100.0);
        QCOMPARE(volume.valueAt(1, 1, 1, 2, 1), 210.0); // cached, no new read
        QCOMPARE(source->requests, (QList<QPair<int, int>>{{2, 1}, {1, 0}}));
    }

    void reloadKeepsCalibratedCoordinates()
    {
        VoxelVolume volume;
        const QVariantMap coarse{{"sizeX", 17}, {"sizeY", 17}, {"sizeZ", 17}, {"spacing", QVariantList{1, 1, 1}},
                                 {"origin", QVariantList{-8, -8, -8}}, {"datasetId", "phantom-1"},
                                 {"frameOfReference", "scanner"}};
        QVERIFY(volume.configure(coarse));
        QVERIFY(volume.fillSpherePhantom());
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        QVERIFY(cursor.setWorldPoint(2, -3, 4));
        VolumeViewItem view;
        view.setSize(QSizeF(64, 64));
        view.setVolume(&volume);
        view.setCursor(&cursor);
        view.setYaw(0);
        view.setPitch(0);
        view.setMode(VolumeViewItem::MaximumIntensity);
        const QVariantList pickedVoxel = view.pick(32, 32);
        QCOMPARE(pickedVoxel.size(), 3);
        const QVariantList pickedWorld = volume.worldFromVoxel(pickedVoxel[0].toDouble(),
                                                               pickedVoxel[1].toDouble(), pickedVoxel[2].toDouble());
        const QVariantMap saved = viaJson(view.saveSettings());
        const QVariantList cursorWorld = cursor.worldPoint();

        // Reload the same dataset at twice the resolution over the same extent.
        QVariantMap fine = coarse;
        fine.insert("sizeX", 33); fine.insert("sizeY", 33); fine.insert("sizeZ", 33);
        fine.insert("spacing", QVariantList{0.5, 0.5, 0.5});
        QVERIFY(volume.configure(fine));
        QVERIFY(volume.fillSpherePhantom());
        QVERIFY(worldDistance(cursor.worldPoint(), cursorWorld) < 1e-9);
        QCOMPARE(cursor.x(), 20.0); // (2 − −8) / 0.5
        view.setYaw(45);
        QVERIFY(view.restoreSettings(saved));
        QCOMPARE(view.yaw(), 0.0);
        const QVariantList again = view.pick(32, 32);
        QCOMPARE(again.size(), 3);
        // The same ray reaches the same calibrated point, within a voxel.
        QVERIFY(worldDistance(volume.worldFromVoxel(again[0].toDouble(), again[1].toDouble(),
                                                    again[2].toDouble()), pickedWorld) <= 1.0);

        // Another dataset (or frame of reference) refuses the saved view.
        QVariantMap other = fine;
        other.insert("datasetId", "phantom-2");
        QVERIFY(volume.configure(other));
        QVERIFY(!view.restoreSettings(saved));
        QVERIFY(view.lastError().contains(QStringLiteral("phantom-1")));
        // …and the cursor, no longer on the same dataset, recentres.
        QCOMPARE(cursor.x(), 16.0);
    }

    void geometryMismatchNamesTheDifference()
    {
        VoxelVolume base, same, spaced, framed, axes;
        const QVariantMap description{{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 4}, {"spacing", QVariantList{1, 1, 2}},
                                      {"frameOfReference", "scanner"}};
        QVERIFY(base.configure(description));
        QVERIFY(same.configure(description));
        QVariantMap changed = description;
        changed.insert("spacing", QVariantList{1, 1, 2.5});
        QVERIFY(spaced.configure(changed));
        changed = description;
        changed.insert("frameOfReference", "atlas");
        QVERIFY(framed.configure(changed));
        changed = description;
        changed.insert("axisIds", QStringList{"i", "j", "k"});
        QVERIFY(axes.configure(changed));
        QCOMPARE(base.geometryMismatch(&same), QString());
        QCOMPARE(base.geometryMismatch(&spaced), QStringLiteral("Voxel spacing differs on axis z"));
        QCOMPARE(base.geometryMismatch(&framed), QStringLiteral("Frames of reference differ (scanner vs atlas)"));
        QCOMPARE(base.geometryMismatch(&axes), QStringLiteral("Axis IDs differ (x,y,z vs i,j,k)"));
        changed = description;
        changed.insert("axisIds", QStringList{"i", "i", "k"});
        QVERIFY(!axes.configure(changed));
    }

    void overlaysAndLinksRejectIncompatibleVolumes()
    {
        VoxelVolume base, labels, coarse;
        const QVariantMap grid{{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8}, {"valueUnit", "HU"}};
        QVERIFY(base.configure(grid));
        QVariantMap labelGrid = grid;
        labelGrid.insert("valueUnit", "label");
        QVERIFY(labels.configure(labelGrid));
        QVERIFY(labels.setVoxel(2, 3, 4, 1));
        QVariantMap coarseGrid = labelGrid;
        coarseGrid.insert("spacing", QVariantList{2, 2, 2});
        QVERIFY(coarse.configure(coarseGrid));
        TransferFunctionModel labelColors;
        QVERIFY(labelColors.configure(0, 1, "label"));
        QVERIFY(labelColors.replacePoints({stop("none", 0, 0, Qt::black), stop("organ", 1, 1, Qt::red)}));

        ImageSliceItem slice;
        slice.setVolume(&base);
        slice.setPlane(ImageSliceItem::Axial);
        VolumeCursor cursor;
        cursor.setVolume(&base);
        QVERIFY(cursor.setVoxelPoint(2, 3, 4));
        slice.setCursor(&cursor);
        slice.setOverlayOpacity(1);
        slice.setOverlay(&labels);
        QCOMPARE(slice.overlayError(), QStringLiteral("The overlay needs a transfer function"));
        slice.setOverlayTransfer(&labelColors);
        QCOMPARE(slice.overlayError(), QString());
        QCOMPARE(slice.sliceImage(8, 8).pixel(2, 3), qRgba(255, 0, 0, 255));
        QCOMPARE(qRed(slice.sliceImage(8, 8).pixel(3, 3)), 0); // label 0 is transparent

        slice.setOverlay(&coarse);
        QCOMPARE(slice.overlayError(), QStringLiteral("Voxel spacing differs on axis x"));
        QCOMPARE(qRed(slice.sliceImage(8, 8).pixel(2, 3)), 0); // not drawn

        // A transfer function in another value unit is ignored, visibly.
        TransferFunctionModel labelTransfer;
        QVERIFY(labelTransfer.configure(0, 1, "label"));
        slice.setTransfer(&labelTransfer);
        QCOMPARE(slice.linkError(), QStringLiteral("The transfer function is in label but the volume stores HU"));
        VolumeViewItem view;
        view.setVolume(&base);
        view.setTransfer(&labelTransfer);
        QCOMPARE(view.linkError(), QStringLiteral("The transfer function is in label but the volume stores HU"));
        QCOMPARE(view.transferTable().domainMax, 256.0); // the window ramp 128 ± 128, not 0…1
        slice.setTransfer(nullptr);

        // A cursor on another grid is not linked: no crosshair moves.
        VolumeCursor foreign;
        foreign.setVolume(&coarse);
        slice.setCursor(&foreign);
        QVERIFY(!slice.cursorLinked());
        QVERIFY(slice.linkError().startsWith(QStringLiteral("Cursor not linked: Voxel spacing differs")));
        QVERIFY(!slice.stepSlice(1));
        QCOMPARE(foreign.z(), 3.5);
        view.setCursor(&foreign);
        QVERIFY(view.linkError().contains(QStringLiteral("Cursor not linked")));
        slice.setCursor(&cursor);
        QVERIFY(slice.cursorLinked());
        QVERIFY(slice.stepSlice(1));
        QCOMPARE(cursor.z(), 5.0);
    }

    void sliceSettingsRoundTripAndBelongToTheirDataset()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8}, {"frames", 2}, {"datasetId", "ct"}}));
        ImageSliceItem slice;
        slice.setVolume(&volume);
        slice.setPlane(ImageSliceItem::Oblique);
        const QVariantList tilted{1, 0, 0, 0, 0.8660254037844387, -0.5, 0, 0.5, 0.8660254037844387};
        slice.setResliceOrientation(tilted);
        slice.setWindowCenter(40);
        slice.setWindowWidth(400);
        slice.setInterpolation(VoxelVolume::Linear);
        slice.setFrame(1);
        slice.setOverlayOpacity(0.25);
        const QVariantMap saved = viaJson(slice.saveSettings());
        slice.setPlane(ImageSliceItem::Axial);
        slice.setResliceOrientation({1, 0, 0, 0, 1, 0, 0, 0, 1});
        slice.setWindowCenter(0);
        slice.setFrame(0);
        QVERIFY(slice.restoreSettings(saved));
        QCOMPARE(slice.plane(), ImageSliceItem::Oblique);
        QCOMPARE(slice.resliceOrientation().at(4).toDouble(), 0.8660254037844387);
        QCOMPARE(slice.windowCenter(), 40.0);
        QCOMPARE(slice.windowWidth(), 400.0);
        QCOMPARE(slice.interpolation(), VoxelVolume::Linear);
        QCOMPARE(slice.frame(), 1);
        QCOMPARE(slice.overlayOpacity(), 0.25);
        // Atomic rejection: a bad matrix changes nothing.
        QVariantMap bad = saved;
        bad.insert("plane", "axial");
        bad.insert("resliceOrientation", QVariantList{1, 1, 0, 0, 1, 0, 0, 0, 1});
        QVERIFY(!slice.restoreSettings(bad));
        QCOMPARE(slice.plane(), ImageSliceItem::Oblique);
        QVERIFY(!slice.lastError().isEmpty());
        // Another dataset refuses it.
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8}, {"frames", 2}, {"datasetId", "mr"}}));
        QVERIFY(!slice.restoreSettings(saved));
        QVERIFY(slice.lastError().contains(QStringLiteral("“ct”")));
    }

    void outOfRangeAndMissingValuesAreExplicit()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 100, "HU"));
        QVERIFY(transfer.replacePoints({stop("low", 0, 0.5, Qt::blue), stop("high", 100, 1, Qt::white)}));
        QCOMPARE(transfer.classify(-1), QStringLiteral("below"));
        QCOMPARE(transfer.classify(100), QStringLiteral("inside"));
        QCOMPARE(transfer.classify(101), QStringLiteral("above"));
        QCOMPARE(transfer.classify(qQNaN()), QStringLiteral("missing"));
        // Defaults: out-of-range clamps (and says so), missing is transparent.
        QCOMPARE(transfer.belowRange().value("mode").toString(), QStringLiteral("clamp"));
        QCOMPARE(transfer.belowRange().value("color").toString(), QStringLiteral("#0000ff"));
        QCOMPARE(transfer.colorAt(-50), QColor(0, 0, 255, 128));
        QCOMPARE(transfer.colorAt(qQNaN()).alpha(), 0);
        QVERIFY(transfer.setRangeColor("below", Qt::green, 1));
        QVERIFY(transfer.setRangeColor("above", Qt::red, 0.75));
        QVERIFY(transfer.setRangeColor("missing", Qt::magenta, 1));
        QCOMPARE(transfer.colorAt(-50), QColor(0, 255, 0, 255));
        QCOMPARE(transfer.colorAt(0), QColor(0, 0, 255, 128)); // the endpoint is inside
        QCOMPARE(transfer.colorAt(150), QColor(255, 0, 0, 191));
        QCOMPARE(transfer.colorAt(qQNaN()), QColor(255, 0, 255, 255));
        QVERIFY(!transfer.clampRange("missing"));

        // Preview, export and the render table agree, specials included.
        const VolumeTransferTable table = VolumeTransferTable::fromModel(transfer);
        QCOMPARE(table.below, (std::array<quint8, 4>{0, 255, 0, 255}));
        QCOMPARE(table.above, (std::array<quint8, 4>{255, 0, 0, 191}));
        QCOMPARE(table.missing, (std::array<quint8, 4>{255, 0, 255, 255}));
        double rgba[4];
        table.lookup(-1, rgba);
        QCOMPARE(rgba[1], 1.0);
        table.lookup(qQNaN(), rgba);
        QCOMPARE(rgba[2], 1.0);
        const QVariantMap exported = viaJson(transfer.exportRamp(256));
        const QVariantList samples = exported.value("samples").toList();
        const QVariantList preview = transfer.previewColors(256);
        QCOMPARE(samples.size(), 256);
        for (int k = 0; k < 256; k += 51) {
            const QVariantMap sample = samples[k].toMap();
            QCOMPARE(sample.value("color").toString(), preview[k].toString());
            const QColor color = QColor::fromString(sample.value("color").toString());
            QCOMPARE(int(table.rgba[k * 4]), color.red());
            QCOMPARE(int(table.rgba[k * 4 + 3]), int(std::lround(sample.value("opacity").toDouble() * 255)));
        }
        QCOMPARE(exported.value("above").toMap().value("color").toString(), QStringLiteral("#ff0000"));
        QCOMPARE(exported.value("missing").toMap().value("opacity").toDouble(), 1.0);
        // The settings round trip keeps them, and a slice paints them.
        TransferFunctionModel restored;
        QVERIFY(restored.restoreSettings(viaJson(transfer.saveSettings())));
        QCOMPARE(restored.colorAt(qQNaN()), QColor(255, 0, 255, 255));
        QCOMPARE(restored.aboveRange().value("opacity").toDouble(), 0.75);
        QVERIFY(restored.clampRange("above"));
        QCOMPARE(restored.colorAt(150), QColor(255, 255, 255, 255));
        QVERIFY(restored.undo());
        QCOMPARE(restored.colorAt(150), QColor(255, 0, 0, 191));
        // A missing provider chunk inside the volume takes the missing colour;
        // outside the volume stays transparent.
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 4}, {"sizeY", 4}, {"sizeZ", 4}, {"valueUnit", "HU"}}));
        struct Missing : VoxelChunkSource { QByteArray readChunk(int, int, int, int, int) override { return {}; } };
        volume.setSource(std::make_shared<Missing>());
        ImageSliceItem slice;
        slice.setVolume(&volume);
        slice.setTransfer(&restored);
        QCOMPARE(slice.sliceImage(4, 4).pixel(1, 1), qRgba(255, 0, 255, 255));
    }

    void presetsAreKnownRampsAndUndoable()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(-1000, 1000, "HU"));
        QStringList ids;
        for (const QVariant &preset : transfer.presets()) ids << preset.toMap().value("id").toString();
        QCOMPARE(ids, (QStringList{"grayscale", "hot", "viridis", "cool-warm"}));
        const QVariantList before = transfer.points();
        QVERIFY(transfer.applyPreset("hot"));
        QCOMPARE(transfer.presetId(), QStringLiteral("hot"));
        QCOMPARE(transfer.colorAt(-250).name(), QStringLiteral("#ff0000")); // anchor 0.375
        QCOMPARE(transfer.colorAt(500).name(), QStringLiteral("#ffff00"));  // anchor 0.75
        QCOMPARE(transfer.colorAt(1000).name(), QStringLiteral("#ffffff"));
        QCOMPARE(transfer.undoText(), QStringLiteral("Apply Hot preset"));
        QVERIFY(transfer.applyPreset("viridis"));
        QCOMPARE(transfer.colorAt(0).name(), QStringLiteral("#21918c"));
        // A stop edit departs from the preset.
        QVERIFY(transfer.setPointOpacity(transfer.points().at(2).toMap().value("id").toString(), 0.1));
        QCOMPARE(transfer.presetId(), QString());
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.presetId(), QStringLiteral("viridis"));
        QVERIFY(transfer.undo());
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.points(), before);
        QVERIFY(!transfer.applyPreset("rainbow"));
        QCOMPARE(transfer.saveSettings().value("presetId").toString(), QString());
    }

    void applySettingsIsAnUndoableRestore()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 255, "a.u."));
        QVERIFY(transfer.replacePoints({stop("low", 0, 0), stop("mid", 110, 0.4), stop("high", 255, 1)}));
        const QVariantMap saved = viaJson(transfer.saveSettings());
        QVERIFY(transfer.movePoint("mid", 180));
        QVERIFY(transfer.setRangeColor("above", Qt::red, 1));
        // Restoring the saved view keeps the user's edits one undo away.
        QVERIFY(transfer.applySettings(saved));
        QCOMPARE(transfer.point("mid").value("value").toDouble(), 110.0);
        QVERIFY(transfer.canUndo());
        QCOMPARE(transfer.undoText(), QStringLiteral("Restore transfer settings"));
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.point("mid").value("value").toDouble(), 180.0);
        QCOMPARE(transfer.aboveRange().value("mode").toString(), QStringLiteral("color"));
        QVERIFY(transfer.redo());
        QCOMPARE(transfer.point("mid").value("value").toDouble(), 110.0);
        // A different domain is part of the step too.
        QVariantMap wider = saved;
        wider.insert("domainMax", 300);
        QVariantList points = wider.value("points").toList();
        QVariantMap last = points.last().toMap();
        last.insert("value", 300);
        points.last() = last;
        wider.insert("points", points);
        QVERIFY(transfer.applySettings(wider));
        QCOMPARE(transfer.domainMax(), 300.0);
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.domainMax(), 255.0);
        // Refused during a gesture; the load form still resets history.
        QVERIFY(transfer.beginEdit("drag"));
        QVERIFY(!transfer.applySettings(saved));
        QVERIFY(transfer.cancelEdit());
        QVERIFY(transfer.restoreSettings(saved));
        QVERIFY(!transfer.canUndo());
    }

    void histogramCountsTheKnownPhantom()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 9}, {"sizeY", 9}, {"sizeZ", 9}}));
        QVERIFY(volume.fillSpherePhantom(0.4));
        const QVariantMap histogram = volume.histogram(256, 0, 255);
        const QVariantList bins = histogram.value("bins").toList();
        qint64 zero = bins[0].toLongLong(), shell = bins[110].toLongLong(), core = bins[220].toLongLong();
        QCOMPARE(zero + shell + core, qint64(9 * 9 * 9));
        // r = 3.6: |d| ≤ 1.98 → core, ≤ 3.6 → shell (counted independently).
        int expectedCore = 0, expectedShell = 0;
        for (int z = 0; z < 9; ++z)
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x) {
                    const double d = std::sqrt(double((x - 4) * (x - 4) + (y - 4) * (y - 4) + (z - 4) * (z - 4)));
                    if (d <= 3.6 * 0.55) ++expectedCore;
                    else if (d <= 3.6) ++expectedShell;
                }
        QCOMPARE(core, qint64(expectedCore));
        QCOMPARE(shell, qint64(expectedShell));
        const QVariantMap narrow = volume.histogram(2, 100, 200);
        QCOMPARE(narrow.value("below").toLongLong(), zero);
        QCOMPARE(narrow.value("above").toLongLong(), core);
        QCOMPARE(narrow.value("missing").toLongLong(), 0);
    }
};

QTEST_MAIN(VolumeLinksTest)
#include "tst_volume_links.moc"
