// SPDX-License-Identifier: LGPL-3.0-or-later
// Oblique world reslicing (D-149) and typed, undoable transfer-function
// transactions (D-148): known reslice geometry for identity and rotated
// volumes, stepping along the normal, one history step per gesture, cancel,
// loads resetting history, and a session adopting the history.
#include "image_slice_item.h"
#include "transfer_function_model.h"
#include "undo_service.h"
#include "volume_cursor.h"
#include "voxel_volume.h"
#include <QSignalSpy>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

namespace {
void identityWindow(ImageSliceItem &item)
{
    item.setWindowCenter(127.5);
    item.setWindowWidth(255);
}
bool near(const QVariant &value, double expected) { return std::abs(value.toDouble() - expected) < 1e-9; }
}

class VolumeEditTest : public QObject {
    Q_OBJECT
private slots:
    void obliqueIdentityPlaneSamplesThroughTheCursor()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8}}));
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) QVERIFY(volume.setVoxel(x, y, z, x + 8 * y + 64 * (z % 4)));
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        QVERIFY(cursor.setVoxelPoint(3, 4, 5));
        ImageSliceItem item;
        item.setVolume(&volume);
        item.setCursor(&cursor);
        item.setPlane(ImageSliceItem::Oblique);
        identityWindow(item);
        item.setSize(QSizeF(100, 100));
        // Square frame √192 mm wide, centred on (3.5, 3.5) in the plane z = 5.
        const QVariantList position = item.positionForVoxel(2, 6, 5);
        QCOMPARE(position.size(), 2);
        QVERIFY(near(position[0], (-1.5 / std::sqrt(192.0) + 0.5) * 100));
        QVERIFY(near(position[1], (2.5 / std::sqrt(192.0) + 0.5) * 100));
        const QVariantList voxel = item.voxelAt(position[0].toDouble(), position[1].toDouble());
        QCOMPARE(voxel.size(), 3);
        QVERIFY(near(voxel[0], 2));
        QVERIFY(near(voxel[1], 6));
        QVERIFY(near(voxel[2], 5));
        const QImage image = item.sliceImage(100, 100);
        QCOMPARE(image.pixel(39, 68), qRgb(114, 114, 114)); // voxel (2, 6, 5)
        QCOMPARE(item.sliceCount(), 14);
        QCOMPARE(item.sliceIndex(), 8);
    }

    void obliquePlaneFollowsWorldAxesOfARotatedVolume()
    {
        // Index +X is world +Y. A world-axis reslice shows the hot voxel
        // (4, 2, 2) two millimetres below the centre (image rows run along
        // world +Y); the voxel-index axial plane shows it to the right.
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 5}, {"sizeY", 5}, {"sizeZ", 5},
                                  {"orientation", QVariantList{0, -1, 0, 1, 0, 0, 0, 0, 1}}}));
        QVERIFY(volume.setVoxel(4, 2, 2, 200));
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        ImageSliceItem oblique;
        oblique.setVolume(&volume);
        oblique.setCursor(&cursor);
        oblique.setPlane(ImageSliceItem::Oblique);
        identityWindow(oblique);
        oblique.setSize(QSizeF(100, 100));
        const QVariantList position = oblique.positionForVoxel(4, 2, 2);
        QVERIFY(near(position[0], 50));
        QVERIFY(near(position[1], (2 / std::sqrt(75.0) + 0.5) * 100));
        QCOMPARE(oblique.sliceImage(100, 100).pixel(50, 73), qRgb(200, 200, 200));
        ImageSliceItem axial;
        axial.setVolume(&volume);
        axial.setCursor(&cursor);
        axial.setSize(QSizeF(100, 100));
        const QVariantList indexPosition = axial.positionForVoxel(4, 2, 2);
        QVERIFY(near(indexPosition[0], 90));
        QVERIFY(near(indexPosition[1], 50));
    }

    void obliqueNavigationStepsAlongTheNormal()
    {
        VoxelVolume volume;
        QVERIFY(volume.configure({{"sizeX", 8}, {"sizeY", 8}, {"sizeZ", 8},
                                  {"spacing", QVariantList{1, 1, 2}}}));
        VolumeCursor cursor;
        cursor.setVolume(&volume);
        ImageSliceItem item;
        item.setVolume(&volume);
        item.setCursor(&cursor);
        item.setPlane(ImageSliceItem::Oblique);
        QSignalSpy sliceSpy(&item, &ImageSliceItem::sliceChanged);
        QCOMPARE(item.sliceCount(), 20); // √384 mm diagonal in 1 mm steps
        QCOMPARE(item.sliceIndex(), 10);
        QVERIFY(item.stepSlice(1)); // 1 mm along world Z is half a 2 mm voxel
        QCOMPARE(cursor.z(), 4.0);
        QCOMPARE(item.sliceIndex(), 11);
        QVERIFY(sliceSpy.count() >= 1);
        QVERIFY(item.stepSlice(100));
        QCOMPARE(cursor.z(), 7.0);
        QVERIFY(!item.stepSlice(1)); // clamped at the last voxel centre
        QVERIFY(item.moveCrosshair(1, 0));
        QCOMPARE(cursor.x(), 4.5);
        QVERIFY(item.moveCrosshair(0, 1));
        QCOMPARE(cursor.y(), 4.5);
        item.setResliceOrientation({1, 0, 0, 0, 1, 0, 0, 0, 2});
        QVERIFY(!item.lastError().isEmpty());
        QCOMPARE(item.resliceOrientation(), (QVariantList{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}));
        // A 90° turn about X: the normal becomes world −Y.
        item.setResliceOrientation({1, 0, 0, 0, 0, -1, 0, 1, 0});
        QVERIFY(item.lastError().isEmpty());
        QVERIFY(item.stepSlice(1));
        QCOMPARE(cursor.y(), 3.5);
        item.setPlane(ImageSliceItem::Axial);
        QVERIFY(item.stepSlice(-2));
        QCOMPARE(cursor.z(), 5.0);
    }

    void directEditsAreUndoableSteps()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 255));
        QVERIFY(!transfer.canUndo());
        const QString id = transfer.addPoint(100, Qt::red, 0.5);
        QVERIFY(!id.isEmpty());
        QVERIFY(transfer.canUndo());
        QCOMPARE(transfer.undoText(), QStringLiteral("Add transfer point"));
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.points().size(), 2);
        QVERIFY(transfer.canRedo());
        QVERIFY(transfer.redo());
        QCOMPARE(transfer.point(id).value(QStringLiteral("value")).toDouble(), 100.0);
        QVERIFY(transfer.movePoint(id, 120));
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.point(id).value(QStringLiteral("value")).toDouble(), 100.0);
        // A rejected edit records nothing.
        QVERIFY(!transfer.movePoint(QStringLiteral("low"), 5));
        QCOMPARE(transfer.undoText(), QStringLiteral("Add transfer point"));
    }

    void gestureIsOneStepAndCancelRestores()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 255));
        QVERIFY(transfer.beginEdit(QStringLiteral("Nothing")));
        QVERIFY(transfer.commitEdit()); // an unchanged gesture records nothing
        QVERIFY(!transfer.canUndo());
        const QString id = transfer.addPoint(100, Qt::red, 0.5);
        QSignalSpy committed(&transfer, &TransferFunctionModel::committed);
        QSignalSpy changed(&transfer, &TransferFunctionModel::changed);
        QVERIFY(transfer.beginEdit(QStringLiteral("Drag stop")));
        QVERIFY(transfer.editing());
        QVERIFY(!transfer.canUndo());
        QVERIFY(!transfer.beginEdit(QStringLiteral("Second")));
        QVERIFY(transfer.movePoint(id, 130));
        QVERIFY(transfer.movePoint(id, 140));
        QCOMPARE(changed.count(), 2); // previews repaint views
        QCOMPARE(committed.count(), 0); // but are not recorded
        QVERIFY(!transfer.undo());
        QCOMPARE(transfer.lastError(), QStringLiteral("Finish the transfer edit first"));
        QVERIFY(transfer.commitEdit());
        QVERIFY(!transfer.editing());
        QCOMPARE(committed.count(), 1);
        QCOMPARE(transfer.undoText(), QStringLiteral("Drag stop"));
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.point(id).value(QStringLiteral("value")).toDouble(), 100.0);
        QVERIFY(transfer.redo());
        QCOMPARE(transfer.point(id).value(QStringLiteral("value")).toDouble(), 140.0);

        QVERIFY(transfer.beginEdit(QStringLiteral("Opacity")));
        QVERIFY(transfer.setPointOpacity(id, 0.9));
        QCOMPARE(transfer.opacityAt(140), 0.9);
        QVERIFY(transfer.cancelEdit());
        QCOMPARE(transfer.opacityAt(140), 0.5);
        QCOMPARE(transfer.undoText(), QStringLiteral("Drag stop"));
        QVERIFY(!transfer.cancelEdit());
    }

    void loadsResetAndSessionsAdoptHistory()
    {
        TransferFunctionModel transfer;
        QVERIFY(transfer.configure(0, 255));
        const QString id = transfer.addPoint(60, Qt::green, 0.25);
        QVERIFY(transfer.canUndo());
        const QVariantMap saved = transfer.saveSettings();
        QVERIFY(transfer.restoreSettings(saved));
        QVERIFY(!transfer.canUndo());
        QCOMPARE(transfer.point(id).value(QStringLiteral("opacity")).toDouble(), 0.25);
        UndoService session;
        QVERIFY(session.adoptModel(&transfer));
        QCOMPARE(transfer.undoService(), &session);
        QVERIFY(transfer.setPointColor(id, Qt::blue));
        QVERIFY(session.canUndo(&transfer));
        QVERIFY(transfer.undo());
        QCOMPARE(transfer.point(id).value(QStringLiteral("color")).value<QColor>(), QColor(Qt::green));
        QVERIFY(transfer.configure(0, 1000));
        QVERIFY(!transfer.canUndo());
        QVERIFY(!transfer.canRedo());
    }
};

QTEST_MAIN(VolumeEditTest)
#include "tst_volume_edit.moc"
