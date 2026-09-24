// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

class TstAssetDependencyIndex final : public QObject {
    Q_OBJECT
private slots:
    void scansRealQmlAndAnswersImpactWithoutAcceptingStaleResults()
    {
        AssetDependencyIndex index;
        index.setProjectRoot(QString::fromUtf8(QINDATK_ASSET_FIXTURE));
        QVERIFY(index.lastError().isEmpty());
        QVERIFY(index.scanQmlFile(QStringLiteral("Main.qml")));
        QVERIFY(index.scanQmlFile(QStringLiteral("components/Panel.qml")));
        QVERIFY(index.scanQmlFile(QStringLiteral("components/Badge.qml")));
        QVERIFY(index.scanQmlFile(QStringLiteral("CycleA.qml")));
        QVERIFY(index.scanQmlFile(QStringLiteral("CycleB.qml")));

        const auto main = index.dependencies(QStringLiteral("Main.qml"));
        QCOMPARE(main.size(), 4);
        QVERIFY(main.contains(QStringLiteral("components/Panel.qml")));
        QVERIFY(main.contains(QStringLiteral("components/Badge.qml")));
        QVERIFY(main.contains(QStringLiteral("images/logo.svg")));
        QVERIFY(main.contains(QStringLiteral("images/lost.png")));
        QVERIFY(!main.contains(QStringLiteral("images/commented-out.png")));
        QCOMPARE(index.asset(QStringLiteral("images/lost.png"))
                 .value(QStringLiteral("available")).toBool(), false);
        const auto diagnostics = index.diagnostics();
        QCOMPARE(diagnostics.size(), 2);
        QCOMPARE(diagnostics.first().toMap().value(QStringLiteral("type")).toString(),
                 QStringLiteral("missing"));
        QCOMPARE(diagnostics.last().toMap().value(QStringLiteral("type")).toString(),
                 QStringLiteral("cycle"));
        const auto impact = index.affectedBy(QStringLiteral("images/logo.svg"));
        QCOMPARE(impact, (QStringList{QStringLiteral("Main.qml"),
                                      QStringLiteral("components/Panel.qml")}));

        QVERIFY(index.upsertAsset(QStringLiteral("generated/output"),
                                  QStringLiteral("Output"), QStringLiteral("generated/output"),
                                  true, QStringLiteral("output")));
        const QString oldToken = index.beginScan(QStringLiteral("generated/output"));
        const QString newToken = index.beginScan(QStringLiteral("generated/output"));
        QSignalSpy rejected(&index, &AssetDependencyIndex::scanRejected);
        QVERIFY(!index.commitScan(QStringLiteral("generated/output"), oldToken,
                                  {QStringLiteral("images/lost.png")}));
        QCOMPARE(rejected.size(), 1);
        QVERIFY(index.dependencies(QStringLiteral("generated/output")).isEmpty());
        QVERIFY(index.commitScan(QStringLiteral("generated/output"), newToken,
                                 {QStringLiteral("images/logo.svg")}));
        QVERIFY(index.affectedBy(QStringLiteral("images/logo.svg"))
                .contains(QStringLiteral("generated/output")));
    }
};

QTEST_GUILESS_MAIN(TstAssetDependencyIndex)
#include "tst_asset_dependency_index.moc"
