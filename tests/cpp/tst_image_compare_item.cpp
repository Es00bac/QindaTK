// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/image_compare_item.h"

#include <QTest>

using QindaTK::ImageCompareItem;

class ImageCompareItemTest : public QObject {
    Q_OBJECT
private slots:
    void splitBlinkAndDifferenceUseTheSamePixels()
    {
        QImage before(4, 2, QImage::Format_RGBA8888);
        QImage after(4, 2, QImage::Format_RGBA8888);
        before.fill(Qt::red);
        after.fill(Qt::blue);
        ImageCompareItem item;
        QVERIFY(item.setImages(before, after));
        item.setSplit(0.5);
        const QImage split = item.renderImage(QSize(8, 4));
        QCOMPARE(split.pixelColor(1, 1), QColor(Qt::red));
        QCOMPARE(split.pixelColor(6, 1), QColor(Qt::blue));

        item.setMode(QStringLiteral("blink"));
        item.setShowAfter(false);
        QCOMPARE(item.renderImage(QSize(8, 4)).pixelColor(6, 1), QColor(Qt::red));
        item.setShowAfter(true);
        QCOMPARE(item.renderImage(QSize(8, 4)).pixelColor(1, 1), QColor(Qt::blue));

        item.setMode(QStringLiteral("difference"));
        QCOMPARE(item.renderImage(QSize(8, 4)).pixelColor(3, 1), QColor(255, 0, 255));
        QVERIFY(!item.setImages(before, QImage(2, 2, QImage::Format_RGBA8888)));
        QVERIFY(!item.lastError().isEmpty());
    }
};

QTEST_MAIN(ImageCompareItemTest)
#include "tst_image_compare_item.moc"
