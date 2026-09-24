// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/asset_provider.h"
#include "../../src/core/resource_resolver.h"
#include "../../src/core/rich_text_asset_renderer.h"
#include "../../src/core/rich_text_document.h"

#include <QPainter>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace QindaTK;

class RichTextAssetRendererTest final : public QObject {
    Q_OBJECT
private:
    static QColor paintedCenter(RichTextAssetRenderer &renderer, const RichText::RichObject &object)
    {
        QImage target(80, 80, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        QPainter painter(&target);
        RichText::PaintContext context;
        context.border = Qt::black;
        context.objectFill = Qt::white;
        context.objectText = Qt::black;
        renderer.paint(&painter, QRectF(0, 0, 80, 80), object, context);
        painter.end();
        return target.pixelColor(40, 40);
    }

private slots:
    void managedImageLoadsAndRelinkInvalidatesPixels()
    {
        QTemporaryDir files;
        QVERIFY(files.isValid());
        const QString redPath = files.filePath(QStringLiteral("red.png"));
        const QString bluePath = files.filePath(QStringLiteral("blue.png"));
        QImage source(32, 32, QImage::Format_ARGB32);
        source.fill(Qt::red);
        QVERIFY(source.save(redPath));
        source.fill(Qt::blue);
        QVERIFY(source.save(bluePath));

        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("art/one"),
                                          QUrl::fromLocalFile(redPath).toString()));
        AssetProvider provider;
        provider.setResolver(&resolver);
        RichTextDocument document;
        RichText::RichObject object;
        object.kind = QStringLiteral("image");
        object.source = QStringLiteral("asset:art/one");
        object.widthPt = 60;
        object.heightPt = 60;
        object.altText = QStringLiteral("Managed red square");
        QVERIFY(document.insertObject(document.startPosition(), object).has_value());

        RichTextAssetRenderer renderer;
        renderer.setProvider(&provider);
        renderer.setDocument(&document);
        QTRY_COMPARE_WITH_TIMEOUT(paintedCenter(renderer, object), QColor(Qt::red), 5000);
        QCOMPARE(provider.subscriptionCount(), 1);

        QVERIFY(resolver.relink(QStringLiteral("art/one"),
                                QUrl::fromLocalFile(bluePath).toString()));
        QTRY_COMPARE_WITH_TIMEOUT(paintedCenter(renderer, object), QColor(Qt::blue), 5000);
        QCOMPARE(provider.subscriptionCount(), 1);

        renderer.setDocument(nullptr);
        QCOMPARE(provider.subscriptionCount(), 0);
    }
};

QTEST_MAIN(RichTextAssetRendererTest)
#include "tst_rich_text_asset_renderer.moc"
