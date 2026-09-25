// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include "document_resource_links.h"
#include <QCryptographicHash>
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
        // Main, Panel, Badge, CycleA, CycleB and the markup source images/logo.svg
        QCOMPARE(finished.first().at(1).toInt(), 6);
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
        QCOMPARE(finished.first().at(1).toInt(), 5);
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
        // The removed reference's placeholder is gone, not left to pile up.
        QVERIFY(index.asset(QStringLiteral("images/lost.png")).isEmpty());

        // With CycleA deleted too, nothing uses either file: both are pruned.
        const int before = index.assetCount();
        QVERIFY(QFile::remove(QDir(dir.path()).filePath(QStringLiteral("CycleA.qml"))));
        finished.clear();
        index.scanProjectAsync();
        QVERIFY(finished.wait(5000));
        QVERIFY(index.asset(QStringLiteral("CycleA.qml")).isEmpty());
        QVERIFY(index.asset(QStringLiteral("CycleB.qml")).isEmpty());
        QCOMPARE(index.assetCount(), before - 2);
        QVERIFY(index.diagnostics().isEmpty());
    }

    void changingRootMidScanDiscardsTheOldScan()
    {
        QTemporaryDir first, second;
        QVERIFY(first.isValid() && second.isValid() && copyFixture(first.path()));
        QVERIFY(writeFile(QDir(second.path()).filePath(QStringLiteral("Other.qml")),
                          "import QtQuick\nItem {}\n"));
        AssetDependencyIndex index;
        index.setProjectRoot(first.path());
        QSignalSpy discarded(&index, &AssetDependencyIndex::projectScanDiscarded);
        QSignalSpy finished(&index, &AssetDependencyIndex::projectScanFinished);
        const QString request = index.scanProjectAsync();
        index.setProjectRoot(second.path());
        QVERIFY(index.lastError().isEmpty());
        QVERIFY(!index.scanning());
        QVERIFY(discarded.wait(5000));
        QCOMPARE(discarded.first().at(0).toString(), request);
        QCOMPARE(finished.size(), 0);
        QCOMPARE(index.assetCount(), 0);  // nothing of the first project leaked in
        index.scanProjectAsync();
        QVERIFY(finished.wait(5000));
        QCOMPARE(index.assetCount(), 1);
        QCOMPARE(index.asset(QStringLiteral("Other.qml")).value(QStringLiteral("uri")).toString(),
                 QDir(second.path()).canonicalPath() + QStringLiteral("/Other.qml"));
    }

    void removalAndPruningReclaimCapacity()
    {
        AssetDependencyIndex index;
        for (int i = 0; i < 4096; ++i)
            QVERIFY(index.upsertAsset(QStringLiteral("a%1").arg(i), QStringLiteral("A"), {},
                                      i >= 10));  // a0..a9 are missing
        QVERIFY(!index.upsertAsset(QStringLiteral("new"), QStringLiteral("New"), {}, true));
        QCOMPARE(index.lastError(), QStringLiteral("Asset index is full"));
        // a0 is missing but used by a10: pruning keeps it, so a10 stays truthful.
        QVERIFY(index.commitScan(QStringLiteral("a10"), index.beginScan(QStringLiteral("a10")),
                                 {QStringLiteral("a0")}));
        QCOMPARE(index.pruneMissing(), 9);
        QVERIFY(!index.asset(QStringLiteral("a0")).isEmpty());
        QVERIFY(index.upsertAsset(QStringLiteral("new"), QStringLiteral("New"), {}, true));

        // Removing a used asset leaves a missing placeholder; removing the
        // last user then lets pruning take it.
        QVERIFY(index.removeAsset(QStringLiteral("a11")));
        QVERIFY(index.asset(QStringLiteral("a11")).isEmpty());
        QVERIFY(index.commitScan(QStringLiteral("a12"), index.beginScan(QStringLiteral("a12")),
                                 {QStringLiteral("a13")}));
        QVERIFY(index.removeAsset(QStringLiteral("a13")));
        QCOMPARE(index.asset(QStringLiteral("a13")).value(QStringLiteral("available")).toBool(),
                 false);
        QCOMPARE(index.diagnostics().size(), 2);  // a10 -> a0, a12 -> a13
        QVERIFY(index.removeAsset(QStringLiteral("a12")));
        QCOMPARE(index.pruneMissing(), 1);
        QVERIFY(index.asset(QStringLiteral("a13")).isEmpty());
        QVERIFY(!index.removeAsset(QStringLiteral("nothing")));
    }

    void rebuildsAreGatedOnTheWholeClosure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid() && copyFixture(dir.path()));
        AssetDependencyIndex index;
        index.setProjectRoot(dir.path());
        auto host = [&](const QString &id, const QStringList &inputs, bool available = true) {
            QVERIFY(index.upsertAsset(id, id, id, available, QStringLiteral("output")));
            QVERIFY(index.commitScan(id, index.beginScan(id), inputs));
        };
        QVERIFY(index.upsertAsset(QStringLiteral("input.svg"), QStringLiteral("Input"),
                                  {}, true));
        host(QStringLiteral("mid"), {QStringLiteral("lost.png")});
        host(QStringLiteral("output"), {QStringLiteral("mid"), QStringLiteral("input.svg")});
        host(QStringLiteral("other"), {QStringLiteral("input.svg")});
        host(QStringLiteral("final"), {QStringLiteral("other")}, false);  // not rendered yet

        // output -> mid -> lost.png: the missing input is two steps away.
        const QVariantList blockers = index.rebuildBlockers(QStringLiteral("output"));
        QCOMPARE(blockers.size(), 1);
        QCOMPARE(blockers.first().toMap().value(QStringLiteral("message")).toString(),
                 QStringLiteral("mid needs missing lost.png"));
        QVERIFY(index.rebuildBlockers(QStringLiteral("other")).isEmpty());
        QVERIFY(index.rebuildBlockers(QStringLiteral("final")).isEmpty());  // regenerable
        QCOMPARE(index.rebuildBlockers(QStringLiteral("lost.png")).size(), 1);

        const QVariantMap plan = index.rebuildPlan(QStringLiteral("input.svg"));
        QCOMPARE(plan.value(QStringLiteral("valid")).toStringList(),
                 (QStringList{QStringLiteral("other"), QStringLiteral("final")}));
        const QVariantList blocked = plan.value(QStringLiteral("blocked")).toList();
        QCOMPARE(blocked.size(), 1);
        QCOMPARE(blocked.first().toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("output"));
        QCOMPARE(blocked.first().toMap().value(QStringLiteral("reason")).toString(),
                 QStringLiteral("lost.png is missing"));

        // Render queue rows become generated outputs of their sources.
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("renders")));
        const QString still = QDir(dir.path()).filePath(QStringLiteral("renders/main.png"));
        QVERIFY(writeFile(still, "png"));
        const QVariantList jobs{
            QVariantMap{{QStringLiteral("jobId"), QStringLiteral("j1")},
                        {QStringLiteral("state"), QStringLiteral("succeeded")},
                        {QStringLiteral("sourceSnapshot"), QStringLiteral("Main.qml")},
                        {QStringLiteral("outputPath"), still}},
            QVariantMap{{QStringLiteral("jobId"), QStringLiteral("j2")},
                        {QStringLiteral("state"), QStringLiteral("queued")},
                        {QStringLiteral("sourceSnapshot"), QStringLiteral("CycleA.qml")},
                        {QStringLiteral("dependencies"), QStringList{QStringLiteral("j1")}},
                        {QStringLiteral("outputPath"),
                         QDir(dir.path()).filePath(QStringLiteral("renders/cycle.mp4"))}}};
        QCOMPARE(index.indexRenderJobs(jobs), 2);
        QCOMPARE(index.asset(QStringLiteral("renders/main.png")).value(QStringLiteral("kind"))
                 .toString(), QStringLiteral("output"));
        QVERIFY(index.asset(QStringLiteral("renders/main.png")).value(QStringLiteral("available"))
                .toBool());
        QCOMPARE(index.dependencies(QStringLiteral("renders/cycle.mp4")),
                 (QStringList{QStringLiteral("CycleA.qml"), QStringLiteral("renders/main.png")}));
        QVERIFY(index.affectedBy(QStringLiteral("Main.qml"))
                .contains(QStringLiteral("renders/cycle.mp4")));
        QVERIFY(index.scanQmlFile(QStringLiteral("CycleA.qml")));
        QVERIFY(index.scanQmlFile(QStringLiteral("CycleB.qml")));
        const QVariantMap renders = index.rebuildPlan(QStringLiteral("Main.qml"));
        QCOMPARE(renders.value(QStringLiteral("valid")).toStringList(),
                 QStringList{QStringLiteral("renders/main.png")});
        QCOMPARE(renders.value(QStringLiteral("blocked")).toList().first().toMap()
                 .value(QStringLiteral("reason")).toString(),
                 QStringLiteral("Cycle: CycleA.qml → CycleB.qml"));
    }

    void markupSourcesExtractDocumentAndMediaReferences()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QDir root(dir.path());
        QVERIFY(root.mkpath(QStringLiteral("css")) && root.mkpath(QStringLiteral("media")));
        QVERIFY(writeFile(root.filePath(QStringLiteral("page.html")),
            "<!doctype html>\n<link rel=\"stylesheet\" href=\"css/site.css?v=2\">\n"
            "<!-- <img src=\"media/old.png\"> -->\n"
            "<video poster=\"media/still.png\" src=\"media/clip.webm\"></video>\n"
            "<a href=\"https://example.com/x.png\">x</a><a href=\"#top\">top</a>\n"));
        QVERIFY(writeFile(root.filePath(QStringLiteral("css/site.css")),
            "@import \"base.css\";\n/* url(../media/none.png) */\n"
            "body { background: url('../media/bg.png'); }\n"));
        QVERIFY(writeFile(root.filePath(QStringLiteral("css/base.css")), "html {}\n"));
        QVERIFY(writeFile(root.filePath(QStringLiteral("media/icons.svg")),
            "<svg xmlns:xlink=\"http://www.w3.org/1999/xlink\">\n"
            "<image xlink:href=\"still.png#frame\"/>\n<use href=\"#local\"/>\n</svg>\n"));
        QVERIFY(writeFile(root.filePath(QStringLiteral("media/still.png")), "png"));
        AssetDependencyIndex index;
        index.setProjectRoot(dir.path());
        QSignalSpy finished(&index, &AssetDependencyIndex::projectScanFinished);
        index.scanProjectAsync();
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.first().at(1).toInt(), 4);  // page.html, two CSS files, the SVG
        QCOMPARE(index.dependencies(QStringLiteral("page.html")),
                 (QStringList{QStringLiteral("css/site.css"), QStringLiteral("media/clip.webm"),
                              QStringLiteral("media/still.png")}));
        const QVariantMap css = referenceTo(index, QStringLiteral("page.html"),
                                            QStringLiteral("css/site.css"));
        QCOMPARE(css.value(QStringLiteral("line")).toInt(), 2);
        QCOMPARE(css.value(QStringLiteral("reference")).toString(),
                 QStringLiteral("css/site.css?v=2"));
        QCOMPARE(index.dependencies(QStringLiteral("css/site.css")),
                 (QStringList{QStringLiteral("css/base.css"), QStringLiteral("media/bg.png")}));
        QCOMPARE(referenceTo(index, QStringLiteral("css/site.css"), QStringLiteral("css/base.css"))
                 .value(QStringLiteral("kind")).toString(), QStringLiteral("import"));
        QCOMPARE(referenceTo(index, QStringLiteral("css/site.css"), QStringLiteral("media/bg.png"))
                 .value(QStringLiteral("line")).toInt(), 3);
        QCOMPARE(index.dependencies(QStringLiteral("media/icons.svg")),
                 QStringList{QStringLiteral("media/still.png")});
        QCOMPARE(index.asset(QStringLiteral("page.html")).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("html"));
        QStringList missing;
        for (const QVariant &row : index.diagnostics())
            missing.append(row.toMap().value(QStringLiteral("targetId")).toString());
        QCOMPARE(missing, (QStringList{QStringLiteral("media/bg.png"),
                                       QStringLiteral("media/clip.webm")}));
        QCOMPARE(index.affectedBy(QStringLiteral("css/base.css")),
                 (QStringList{QStringLiteral("css/site.css"), QStringLiteral("page.html")}));
    }

    void relinkCandidatesRunOnAWorker()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid() && copyFixture(dir.path()));
        AssetDependencyIndex index;
        index.setProjectRoot(dir.path());
        QSignalSpy ready(&index, &AssetDependencyIndex::relinkCandidatesReady);
        const QString request = index.relinkCandidatesAsync(QStringLiteral("images/lost.png"));
        QCOMPARE(ready.size(), 0);  // nothing is walked on the calling thread
        QVERIFY(ready.wait(5000));
        QCOMPARE(ready.first().at(0).toString(), request);
        QCOMPARE(ready.first().at(1).toString(), QStringLiteral("images/lost.png"));
        QCOMPARE(ready.first().at(2).toStringList(), QStringList{QStringLiteral("archive/lost.png")});
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

    void documentResourcesBecomeEdges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray media("waveform bytes");
        const QString present = dir.filePath(QStringLiteral("voice.wav"));
        QVERIFY(writeFile(present, media));
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(media, QCryptographicHash::Sha256).toHex());
        PropertyDocument document;
        QVERIFY(document.defineProperty({QStringLiteral("uri"), QMetaType::fromType<QString>(),
                                         QString{}, {}, true, false}));
        const EntityId voice = EntityId::create(), music = EntityId::create();
        QVERIFY(document.addEntity(voice, {{QStringLiteral("uri"), present}}));
        QVERIFY(document.addEntity(music, {{QStringLiteral("uri"),
                                            dir.filePath(QStringLiteral("gone.wav"))}}));
        auto row = [&](const QString &id, const EntityId &entity) {
            return QVariantMap{{QStringLiteral("resourceId"), id},
                               {QStringLiteral("entityId"), entity.toString()},
                               {QStringLiteral("uriPropertyId"), QStringLiteral("uri")},
                               {QStringLiteral("expectedSha256"), hash}};
        };
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        ResourceResolver resolver;
        DocumentResourceLinks links;
        links.setDocument(&document);
        links.setUndoService(&undo);
        links.setResolver(&resolver);
        links.setManifest({row(QStringLiteral("res:voice"), voice),
                           row(QStringLiteral("res:music"), music)});

        AssetDependencyIndex index;
        QCOMPARE(index.indexDocumentResources(QStringLiteral("cut.sloom"), &links), 2);
        QCOMPARE(index.dependencies(QStringLiteral("cut.sloom")),
                 (QStringList{QStringLiteral("res:music"), QStringLiteral("res:voice")}));
        QCOMPARE(index.asset(QStringLiteral("res:voice")).value(QStringLiteral("available")), true);
        QCOMPARE(index.asset(QStringLiteral("res:music")).value(QStringLiteral("available")), false);
        QCOMPARE(referenceTo(index, QStringLiteral("cut.sloom"), QStringLiteral("res:voice"))
                     .value(QStringLiteral("reference")).toString(), present);
        QVERIFY(!index.rebuildBlockers(QStringLiteral("cut.sloom")).isEmpty());
        // Unlinking the missing resource drops its edge and its entry.
        links.setManifest({row(QStringLiteral("res:voice"), voice)});
        QCOMPARE(index.indexDocumentResources(QStringLiteral("cut.sloom"), &links), 1);
        QCOMPARE(index.assetCount(), 2);
        QVERIFY(index.rebuildBlockers(QStringLiteral("cut.sloom")).isEmpty());
        QCOMPARE(index.indexDocumentResources(QStringLiteral("cut.sloom"), nullptr), -1);
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
