// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
// A private copy of the fixture project, so tests can edit and delete files.
bool copyFixture(const QString &to)
{
    const QString from = QString::fromUtf8(QINDATK_ASSET_FIXTURE);
    QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString relative = QDir(from).relativeFilePath(it.filePath());
        const QString target = QDir(to).filePath(relative);
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(it.filePath(), target))
            return false;
    }
    return true;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
           && file.write(bytes) == bytes.size();
}

QVariantMap referenceTo(const AssetDependencyIndex &index, const QString &source,
                        const QString &target)
{
    for (const QVariant &row : index.references(source))
        if (row.toMap().value(QStringLiteral("targetId")).toString() == target)
            return row.toMap();
    return {};
}
}

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

    void projectScanRunsOffThreadWithLinesAndDeletions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid() && copyFixture(dir.path()));
        AssetDependencyIndex index;
        index.setProjectRoot(dir.path());
        QSignalSpy finished(&index, &AssetDependencyIndex::projectScanFinished);
        const QString request = index.scanProjectAsync();
        QVERIFY(!request.isEmpty());
        QVERIFY(index.scanning());
        QVERIFY(finished.wait(5000));
        QVERIFY(!index.scanning());
        QCOMPARE(finished.first().at(0).toString(), request);
        QCOMPARE(finished.first().at(1).toInt(), 5);  // Main, Panel, Badge, CycleA, CycleB
        QCOMPARE(finished.first().at(2).toInt(), 0);
        QVERIFY(index.lastError().isEmpty());

        const QVariantMap lost = referenceTo(index, QStringLiteral("Main.qml"),
                                             QStringLiteral("images/lost.png"));
        QCOMPARE(lost.value(QStringLiteral("kind")).toString(), QStringLiteral("reference"));
        QCOMPARE(lost.value(QStringLiteral("line")).toInt(), 10);
        QCOMPARE(lost.value(QStringLiteral("reference")).toString(),
                 QStringLiteral("images/lost.png"));
        const QVariantMap panel = referenceTo(index, QStringLiteral("Main.qml"),
                                              QStringLiteral("components/Panel.qml"));
        QCOMPARE(panel.value(QStringLiteral("kind")).toString(), QStringLiteral("import"));
        QCOMPARE(panel.value(QStringLiteral("line")).toInt(), 3);
        QCOMPARE(panel.value(QStringLiteral("reference")).toString(), QStringLiteral("components"));
        const QVariantMap logo = referenceTo(index, QStringLiteral("components/Panel.qml"),
                                             QStringLiteral("images/logo.svg"));
        QCOMPARE(logo.value(QStringLiteral("reference")).toString(),
                 QStringLiteral("../images/logo.svg"));
        auto diagnostics = index.diagnostics();
        QCOMPARE(diagnostics.size(), 2);
        QCOMPARE(diagnostics.first().toMap().value(QStringLiteral("message")).toString(),
                 QStringLiteral("Main.qml line 10 needs missing images/lost.png"));

        // Delete a file in the cycle and drop a reference from Main.qml.
        QVERIFY(QFile::remove(QDir(dir.path()).filePath(QStringLiteral("CycleB.qml"))));
        QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("Main.qml")),
            "import QtQuick\nimport \"components\" as Components\n"
            "Item { Image { source: \"images/logo.svg\" } }\n"));
        finished.clear();
        index.scanProjectAsync();
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(1).toInt(), 4);
        QCOMPARE(index.dependencies(QStringLiteral("Main.qml")),
                 (QStringList{QStringLiteral("components/Badge.qml"),
                              QStringLiteral("components/Panel.qml"),
                              QStringLiteral("images/logo.svg")}));
        QCOMPARE(referenceTo(index, QStringLiteral("Main.qml"), QStringLiteral("images/logo.svg"))
                 .value(QStringLiteral("line")).toInt(), 3);
        QCOMPARE(index.asset(QStringLiteral("CycleB.qml"))
                 .value(QStringLiteral("available")).toBool(), false);
        QVERIFY(index.dependencies(QStringLiteral("CycleB.qml")).isEmpty());
        diagnostics = index.diagnostics();
        QCOMPARE(diagnostics.size(), 1);
        const QVariantMap missing = diagnostics.first().toMap();
        QCOMPARE(missing.value(QStringLiteral("type")).toString(), QStringLiteral("missing"));
        QCOMPARE(missing.value(QStringLiteral("sourceId")).toString(), QStringLiteral("CycleA.qml"));
        QCOMPARE(missing.value(QStringLiteral("targetId")).toString(), QStringLiteral("CycleB.qml"));
        QCOMPARE(missing.value(QStringLiteral("line")).toInt(), 4);
    }

    void obsoleteAndCancelledProjectScansAreDiscarded()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid() && copyFixture(dir.path()));
        AssetDependencyIndex index;
        index.setProjectRoot(dir.path());

        // Cancel: nothing from the worker reaches the index.
        QSignalSpy discarded(&index, &AssetDependencyIndex::projectScanDiscarded);
        const QString cancelled = index.scanProjectAsync();
        index.cancelProjectScan();
        QVERIFY(!index.scanning());
        QVERIFY(discarded.wait(5000));
        QCOMPARE(discarded.first().at(0).toString(), cancelled);
        QCOMPARE(index.assetCount(), 0);

        // A newer scan supersedes an older one.
        QSignalSpy finished(&index, &AssetDependencyIndex::projectScanFinished);
        discarded.clear();
        const QString older = index.scanProjectAsync();
        const QString newer = index.scanProjectAsync();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(discarded.size(), 1, 5000);
        QCOMPARE(discarded.first().at(0).toString(), older);
        QCOMPARE(finished.first().at(0).toString(), newer);

        // A source rescanned after a project scan starts keeps its newer edges.
        QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("components/Badge.qml")),
            "import QtQuick\nImage { source: \"../images/logo.svg\" }\n"));
        finished.clear();
        index.scanProjectAsync();
        QVERIFY(writeFile(QDir(dir.path()).filePath(QStringLiteral("CycleA.qml")),
            "import QtQuick\nItem {}\n"));
        QVERIFY(index.scanQmlFile(QStringLiteral("CycleA.qml")));
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(2).toInt(), 1);
        QVERIFY(index.dependencies(QStringLiteral("CycleA.qml")).isEmpty());
        QCOMPARE(index.dependencies(QStringLiteral("components/Badge.qml")),
                 QStringList{QStringLiteral("images/logo.svg")});
    }

    void relinkSuggestsCandidatesAndPlansEdits()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid() && copyFixture(dir.path()));
        AssetDependencyIndex index;
        index.setProjectRoot(dir.path());
        QVERIFY(index.scanQmlFile(QStringLiteral("Main.qml")));
        QCOMPARE(index.relinkCandidates(QStringLiteral("images/lost.png")),
                 QStringList{QStringLiteral("archive/lost.png")});
        const QVariantList plan = index.relinkPlan(QStringLiteral("images/lost.png"),
                                                   QStringLiteral("archive/lost.png"));
        QCOMPARE(plan.size(), 1);
        const QVariantMap edit = plan.first().toMap();
        QCOMPARE(edit.value(QStringLiteral("sourceId")).toString(), QStringLiteral("Main.qml"));
        QCOMPARE(edit.value(QStringLiteral("line")).toInt(), 10);
        QCOMPARE(edit.value(QStringLiteral("reference")).toString(),
                 QStringLiteral("images/lost.png"));
        QCOMPARE(edit.value(QStringLiteral("replacement")).toString(),
                 QStringLiteral("archive/lost.png"));
        QVERIFY(index.relinkPlan(QStringLiteral("images/lost.png"),
                                 QStringLiteral("archive/none.png")).isEmpty());

        // A reference one directory down gets a path relative to its source.
        QVERIFY(index.scanQmlFile(QStringLiteral("components/Panel.qml")));
        QVERIFY(index.setAvailable(QStringLiteral("images/logo.svg"), false));
        const QVariantList panelPlan = index.relinkPlan(QStringLiteral("images/logo.svg"),
                                                        QStringLiteral("archive/lost.png"));
        QCOMPARE(panelPlan.size(), 2);  // Main.qml line 9 and Panel.qml line 5
        QCOMPARE(panelPlan.at(0).toMap().value(QStringLiteral("replacement")).toString(),
                 QStringLiteral("archive/lost.png"));
        QCOMPARE(panelPlan.at(1).toMap().value(QStringLiteral("sourceId")).toString(),
                 QStringLiteral("components/Panel.qml"));
        QCOMPARE(panelPlan.at(1).toMap().value(QStringLiteral("line")).toInt(), 5);
        QCOMPARE(panelPlan.at(1).toMap().value(QStringLiteral("replacement")).toString(),
                 QStringLiteral("../archive/lost.png"));
    }

    void hostEdgesCarryValidatedMetadata()
    {
        AssetDependencyIndex index;
        QVERIFY(index.upsertAsset(QStringLiteral("comp"), QStringLiteral("Comp"),
                                  QStringLiteral("comp.sloom"), true));
        QString token = index.beginScan(QStringLiteral("comp"));
        QVERIFY(!index.commitScanEdges(QStringLiteral("comp"), token,
            {QVariantMap{{QStringLiteral("targetId"), QStringLiteral("clip")},
                         {QStringLiteral("kind"), QStringLiteral("guess")}}}));
        QVERIFY(index.commitScanEdges(QStringLiteral("comp"), token,
            {QVariantMap{{QStringLiteral("targetId"), QStringLiteral("clip")},
                         {QStringLiteral("kind"), QStringLiteral("reference")},
                         {QStringLiteral("line"), 9},
                         {QStringLiteral("reference"), QStringLiteral("media/clip.mov")}},
             QVariantMap{{QStringLiteral("targetId"), QStringLiteral("clip")},
                         {QStringLiteral("kind"), QStringLiteral("reference")},
                         {QStringLiteral("line"), 4},
                         {QStringLiteral("reference"), QStringLiteral("media/clip.mov")}}}));
        QCOMPARE(index.edgeCount(), 1);
        QCOMPARE(referenceTo(index, QStringLiteral("comp"), QStringLiteral("clip"))
                 .value(QStringLiteral("line")).toInt(), 4);
        token = index.beginScan(QStringLiteral("comp"));
        QVERIFY(index.commitScan(QStringLiteral("comp"), token, {QStringLiteral("clip")}));
        QCOMPARE(referenceTo(index, QStringLiteral("comp"), QStringLiteral("clip"))
                 .value(QStringLiteral("kind")).toString(), QStringLiteral("host"));
    }
};

QTEST_GUILESS_MAIN(TstAssetDependencyIndex)
#include "tst_asset_dependency_index.moc"
