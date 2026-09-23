// SPDX-License-Identifier: LGPL-3.0-or-later
#include "font_catalog.h"
#include <QFont>
#include <QFontDatabase>
#include <QRawFont>
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
    void selectedFaceGlyphCoverageIsExplicitAndBounded()
    {
        FontCatalog catalog;
        QString family;
        QString style;
        for (const auto &value : catalog.families()) {
            const auto face = value.toMap();
            const QString candidate = face.value(QStringLiteral("family")).toString();
            const QString candidateStyle = face.value(QStringLiteral("styles")).toStringList().value(0);
            const QRawFont raw = QRawFont::fromFont(QFontDatabase::font(candidate, candidateStyle, 12));
            if (raw.isValid() && raw.supportsCharacter(uint('A'))
                && !raw.supportsCharacter(0x10ffffu)) {
                family = candidate;
                style = candidateStyle;
                break;
            }
        }
        if (family.isEmpty()) QSKIP("No installed face with the coverage fixture pair");
        QVERIFY(catalog.faceMetadata(family, style).value(QStringLiteral("coverageMetadataAvailable")).toBool());
        QString sample = QStringLiteral("AA");
        sample.append(QChar(0xdbff));
        sample.append(QChar(0xdfff));
        const auto coverage = catalog.checkGlyphCoverage(family, style, sample);
        QVERIFY(coverage.value(QStringLiteral("available")).toBool());
        QVERIFY(!coverage.value(QStringLiteral("complete")).toBool());
        QCOMPARE(coverage.value(QStringLiteral("checkedCount")).toInt(), 3);
        QCOMPARE(coverage.value(QStringLiteral("unsupportedCodepoints")).toStringList(),
                 QStringList{QStringLiteral("U+10FFFF")});
        QCOMPARE(coverage.value(QStringLiteral("unsupportedCount")).toInt(), 1);
        QVERIFY(catalog.checkGlyphCoverage(family, style, QStringLiteral("A"))
                    .value(QStringLiteral("complete")).toBool());
        QVERIFY(!catalog.checkGlyphCoverage(QStringLiteral("QindaTK Fixture Missing Font 89B56"),
                                            style, sample).value(QStringLiteral("available")).toBool());
        QVERIFY(!catalog.checkGlyphCoverage(family, style, QString(4097, QChar('A')))
                    .value(QStringLiteral("available")).toBool());
        QVERIFY(!catalog.checkGlyphCoverage(family, style, QString(QChar(0xd800)))
                    .value(QStringLiteral("available")).toBool());
    }
};

QTEST_MAIN(TstFontCatalog)
#include "tst_font_catalog.moc"
