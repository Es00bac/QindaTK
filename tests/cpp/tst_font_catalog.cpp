// SPDX-License-Identifier: LGPL-3.0-or-later
#include "font_catalog.h"
#include <QFontDatabase>
#include <QtTest>

using namespace QindaTK;

class TstFontCatalog final : public QObject {
    Q_OBJECT
private slots:
    void installedAndMissingFontsRemainDistinct()
    {
        FontCatalog catalog;
        QVERIFY(!catalog.families().isEmpty());
        const QString installed = catalog.families().first().toMap()
            .value(QStringLiteral("family")).toString();
        const auto present = catalog.resolveFamily(installed);
        QVERIFY(present.value(QStringLiteral("installed")).toBool());
        QVERIFY(!present.value(QStringLiteral("styles")).toStringList().isEmpty());
        const QString missing = QStringLiteral("QindaTK Fixture Missing Font 89B56");
        const auto absent = catalog.resolveFamily(missing);
        QCOMPARE(absent.value(QStringLiteral("family")).toString(), missing);
        QVERIFY(!absent.value(QStringLiteral("installed")).toBool());
        QVERIFY(!absent.value(QStringLiteral("fallbackFamily")).toString().isEmpty());
        const auto unknown = catalog.faceMetadata(missing);
        QVERIFY(!unknown.value(QStringLiteral("available")).toBool());
        QVERIFY(!unknown.value(QStringLiteral("axisMetadataAvailable")).toBool());
        QVERIFY(!unknown.value(QStringLiteral("featureMetadataAvailable")).toBool());
        QVERIFY(unknown.value(QStringLiteral("axes")).toList().isEmpty());
        QVERIFY(unknown.value(QStringLiteral("features")).toList().isEmpty());
    }
    void searchIsBoundedAndReflectsInstalledFamilies()
    {
        FontCatalog catalog;
        const auto few = catalog.search(QString(), 3);
        QCOMPARE(few.size(), std::min<qsizetype>(catalog.families().size(), 3));
        QVERIFY(catalog.search(QString(), 0).isEmpty());
        const QString family = catalog.families().first().toMap()
            .value(QStringLiteral("family")).toString();
        QVERIFY(!catalog.search(family).isEmpty());
    }
    void installedOpenTypeFaceReportsRealFeatureAndAxisBounds()
    {
        FontCatalog catalog;
        QVariantMap variableFace;
        QVariantMap layoutFace;
        for (const auto &value : catalog.families()) {
            const auto entry = value.toMap();
            const QString family = entry.value(QStringLiteral("family")).toString();
            const QString style = entry.value(QStringLiteral("styles")).toStringList().value(0);
            const auto metadata = catalog.faceMetadata(family, style);
            if (variableFace.isEmpty() && !metadata.value(QStringLiteral("axes")).toList().isEmpty())
                variableFace = metadata;
            if (layoutFace.isEmpty() && !metadata.value(QStringLiteral("features")).toList().isEmpty())
                layoutFace = metadata;
            if (!variableFace.isEmpty() && !layoutFace.isEmpty()) break;
        }
        if (layoutFace.isEmpty()) QSKIP("No installed OpenType layout font available");
        QVERIFY(layoutFace.value(QStringLiteral("available")).toBool());
        QVERIFY(layoutFace.value(QStringLiteral("featureMetadataAvailable")).toBool());
        for (const auto &value : layoutFace.value(QStringLiteral("features")).toList()) {
            const auto feature = value.toMap();
            QCOMPARE(feature.value(QStringLiteral("tag")).toString().size(), 4);
            QVERIFY(feature.value(QStringLiteral("supported")).toBool());
        }
        if (variableFace.isEmpty()) QSKIP("No installed variable font available");
        QVERIFY(variableFace.value(QStringLiteral("axisMetadataAvailable")).toBool());
        for (const auto &value : variableFace.value(QStringLiteral("axes")).toList()) {
            const auto axis = value.toMap();
            QVERIFY(axis.value(QStringLiteral("minimum")).toDouble()
                    <= axis.value(QStringLiteral("default")).toDouble());
            QVERIFY(axis.value(QStringLiteral("default")).toDouble()
                    <= axis.value(QStringLiteral("maximum")).toDouble());
        }
    }
};

QTEST_MAIN(TstFontCatalog)
#include "tst_font_catalog.moc"
