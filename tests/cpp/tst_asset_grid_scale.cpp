// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_provider.h"
#include "density.h"
#include "resource_resolver.h"
#include "selection_model.h"
#include "theme.h"
#include "virtual_collection_adapter.h"
#include <QAccessible>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace QindaTK;
namespace {
QString assetId(int row)
{ return QStringLiteral("00000000-0000-4000-8000-%1").arg(row,12,16,QLatin1Char('0')); }
QString resourceId(int row) { return QStringLiteral("asset:%1").arg(row); }
class GeneratedAssets final : public QAbstractListModel {
public:
    enum Role { IdRole=Qt::UserRole+1, LabelRole, SecondaryRole, ResourceRole, CaptionRole };
    explicit GeneratedAssets(int count,QObject *parent=nullptr):QAbstractListModel(parent),m_count(count) {}
    int rowCount(const QModelIndex &parent={}) const override { return parent.isValid() ? 0 : m_count; }
    QVariant data(const QModelIndex &index,int role) const override
    {
        if (!index.isValid() || index.row()<0 || index.row()>=m_count) return {};
        const int row=index.row();
        switch (role) {
        case IdRole: return assetId(row);
        case LabelRole: return QStringLiteral("Asset %1").arg(row,6,10,QLatin1Char('0'));
        case SecondaryRole: return QStringLiteral("Row %1").arg(row);
        case ResourceRole: return resourceId(row);
        case CaptionRole: return QStringLiteral("Preview %1").arg(row);
        default: return {};
        }
    }
    QHash<int,QByteArray> roleNames() const override
    { return {{IdRole,"id"},{LabelRole,"label"},{SecondaryRole,"secondary"},
              {ResourceRole,"resourceId"},{CaptionRole,"caption"}}; }
private:
    int m_count;
};
QList<QObject *> delegates(QObject *view,const QString &name)
{
    QList<QObject *> result;
    if (!view) return result;
    auto *content=qobject_cast<QQuickItem *>(view->property("contentItem").value<QObject *>());
    if (!content) return result;
    for (QQuickItem *item:content->childItems())
        if (item->objectName()==name) result.append(item);
    return result;
}
QObject *delegateAt(QObject *view,const QString &name,int index)
{
    for (QObject *item:delegates(view,name))
        if (item->property("index").toInt()==index) return item;
    return nullptr;
}
QStringList activeSubscriptions(QObject *gridView)
{
    QStringList ids;
    for (QObject *tile:delegates(gridView,QStringLiteral("assetGridDelegate"))) {
        QObject *thumbnail=tile->findChild<QObject *>(QStringLiteral("assetGridThumbnail_")
            +tile->property("stableId").toString());
        if (!thumbnail) continue;
        const QString id=thumbnail->property("subscriptionId").toString();
        if (!id.isEmpty()) ids.append(id);
    }
    return ids;
}
bool pressAccessible(QObject *item)
{
    auto *access=QAccessible::queryAccessibleInterface(item);
    if (!access || !access->actionInterface()
        || !access->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()))
        return false;
    access->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    return true;
}
}

class AssetGridScaleTest : public QObject {
    Q_OBJECT
private slots:
    void hundredThousandAssetsUseBoundedRealProviderAndAccessibleRows();
};
void AssetGridScaleTest::hundredThousandAssetsUseBoundedRealProviderAndAccessibleRows()
{
    constexpr int count=100000;
    QTemporaryDir mediaDir; QVERIFY(mediaDir.isValid());
    QImage source({64,48},QImage::Format_ARGB32_Premultiplied);
    source.fill(QColor(15,120,160));
    const QString imagePath=mediaDir.filePath(QStringLiteral("fixture.png"));
    QVERIFY(source.save(imagePath));
    GeneratedAssets model(count); SelectionModel selection;
    ResourceResolver resolver;
    // Real file-backed thumbnails are needed only for rows this viewport can
    // materialize. The 100k source stays generated in C++, not copied to JS.
    for (int row=0;row<240;++row)
        QVERIFY(resolver.registerResource(resourceId(row),imagePath));
    for (int row=count-240;row<count;++row)
        QVERIFY(resolver.registerResource(resourceId(row),imagePath));
    AssetProvider provider; provider.setResolver(&resolver);
    provider.setCacheBudgetBytes(64*1024);
    int maxRunning=0,maxQueued=0;
    connect(provider.jobs(),&JobModel::changed,this,[&] {
        maxRunning=qMax(maxRunning,provider.jobs()->runningCount());
        maxQueued=qMax(maxQueued,provider.jobs()->queuedCount());
    });

    QQmlEngine engine; engine.addImportPath(QString::fromUtf8(QINDATK_BUILD_QML_DIR));
    engine.rootContext()->setContextProperty(QStringLiteral("assetSource"),&model);
    engine.rootContext()->setContextProperty(QStringLiteral("assetSelection"),&selection);
    engine.rootContext()->setContextProperty(QStringLiteral("realAssetProvider"),&provider);
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QindaTK as Tk
        Rectangle {
            width: 960; height: 560; color: Tk.Theme.color.bg
            Tk.Flex {
                anchors.fill: parent; direction: Tk.Flex.Column
                padding: Tk.Theme.space.md; gap: Tk.Theme.space.sm
                Tk.Label { text: "100,000 indexed assets"; font.pixelSize: Tk.Theme.font.title }
                Tk.Flex {
                    direction: Tk.Flex.Row; gap: Tk.Theme.space.md; Tk.Flex.flex: 1
                    Tk.VirtualList {
                        objectName: "scaleList"; width: 190
                        model: assetSource; selectionModel: assetSelection; tooltip: "Asset list"
                    }
                    Tk.VirtualTree {
                        objectName: "scaleTree"; width: 190; flatProjection: true
                        model: assetSource; selectionModel: assetSelection; tooltip: "Asset tree"
                    }
                    Tk.AssetGrid {
                        objectName: "scaleGrid"; Tk.Flex.flex: 1
                        model: assetSource; selectionModel: assetSelection
                        provider: realAssetProvider; viewId: "wp027-scale"
                        tooltip: "Asset thumbnails"
                    }
                }
            }
        }
    )",QUrl(QStringLiteral("inline:/AssetGridScale.qml")));
    QTRY_VERIFY_WITH_TIMEOUT(component.isReady(),10000);
    QElapsedTimer timer; timer.start();
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root,qPrintable(component.errorString()));
    auto *rootItem=qobject_cast<QQuickItem *>(root.get()); QVERIFY(rootItem);
    QQuickView window; window.resize(960,560); rootItem->setParentItem(window.contentItem());
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(),5000);
    QObject *list=root->findChild<QObject *>(QStringLiteral("scaleList"));
    QObject *tree=root->findChild<QObject *>(QStringLiteral("scaleTree"));
    QObject *grid=root->findChild<QObject *>(QStringLiteral("scaleGrid"));
    QVERIFY(list && tree && grid);
    auto *listView=list->property("listView").value<QObject *>();
    auto *treeView=tree->property("treeView").value<QObject *>();
    auto *gridView=grid->property("gridView").value<QObject *>();
    QVERIFY(listView && treeView && gridView);
    auto *adapter=qobject_cast<VirtualCollectionAdapter *>(grid->property("collectionAdapter").value<QObject *>());
    QVERIFY(adapter); QTRY_COMPARE_WITH_TIMEOUT(adapter->count(),count,10000);
    auto *gridItem=qobject_cast<QQuickItem *>(grid); QVERIFY(gridItem);
    QTRY_VERIFY_WITH_TIMEOUT(gridItem->width()>400 && gridItem->height()>400,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!delegates(gridView,QStringLiteral("assetGridDelegate")).isEmpty(),10000);
    const qint64 mountedMs=timer.elapsed();
    qInfo() << "fixture geometry root/grid/viewport" << rootItem->size()
            << gridItem->size() << qobject_cast<QQuickItem *>(gridView)->size();
    QTRY_VERIFY_WITH_TIMEOUT(provider.cacheBytes()>0,10000);
    const int listCount=delegates(listView,QStringLiteral("virtualListDelegate")).size();
    const int treeCount=delegates(treeView,QStringLiteral("virtualTreeDelegate")).size();
    const int gridCount=delegates(gridView,QStringLiteral("assetGridDelegate")).size();
    QVERIFY(listCount>0 && listCount<128);
    QVERIFY(treeCount>0 && treeCount<128);
    QVERIFY(gridCount>0 && gridCount<128);
    QVERIFY(provider.subscriptionCount()>0 && provider.subscriptionCount()<=gridCount);
    QVERIFY(provider.cacheBytes()<=provider.cacheBudgetBytes());
    QVERIFY(maxRunning<=provider.jobs()->maxConcurrent());
    QVERIFY(maxQueued<=provider.jobs()->maxQueued());
    const QStringList firstSubscriptions=activeSubscriptions(gridView);
    QVERIFY(!firstSubscriptions.isEmpty());
    qInfo() << "100k asset mount ms" << mountedMs << "delegates list/tree/grid"
            << listCount << treeCount << gridCount << "subscriptions"
            << provider.subscriptionCount() << "cache bytes" << provider.cacheBytes()
            << "max decoder jobs" << maxRunning << "queued" << maxQueued;

    QAccessible::setActive(true);
    QObject *firstList=delegateAt(listView,QStringLiteral("virtualListDelegate"),0);
    QObject *firstTree=delegateAt(treeView,QStringLiteral("virtualTreeDelegate"),0);
    QObject *firstGrid=delegateAt(gridView,QStringLiteral("assetGridDelegate"),0);
    QVERIFY(firstList && firstTree && firstGrid);
    QSignalSpy listActivated(list,SIGNAL(activated(QString)));
    QSignalSpy treeActivated(tree,SIGNAL(activated(QString)));
    QSignalSpy gridActivated(grid,SIGNAL(activated(QString)));
    QVERIFY(pressAccessible(firstList)); QCOMPARE(listActivated.size(),1);
    QVERIFY(pressAccessible(firstTree)); QCOMPARE(treeActivated.size(),1);
    QVERIFY(pressAccessible(firstGrid)); QCOMPARE(gridActivated.size(),1);
    QCOMPARE(selection.currentId(),assetId(0));
    QCOMPARE(firstGrid->property("stableId").toString(),assetId(0));
    QCOMPARE(firstGrid->property("labelText").toString(),QStringLiteral("Asset 000000"));
    QObject *firstThumbnail=firstGrid->findChild<QObject *>(QStringLiteral("assetGridThumbnail_")+assetId(0));
    QVERIFY(firstThumbnail);
    QTRY_COMPARE_WITH_TIMEOUT(firstThumbnail->property("assetState").toString(),QStringLiteral("ready"),10000);
    QTRY_COMPARE_WITH_TIMEOUT(firstThumbnail->property("loadState").toString(),QStringLiteral("ready"),10000);

    const QByteArray captureDir=qgetenv("QINDATK_WP027_CAPTURE_DIR");
    if (!captureDir.isEmpty()) {
        const QString output=QDir(QString::fromUtf8(captureDir)).filePath(QStringLiteral("asset-scale-compact.png"));
        QVERIFY(window.grabWindow().save(output));
    }
    gridItem->forceActiveFocus(); QVERIFY(gridItem->hasActiveFocus());
    timer.restart(); QTest::keyClick(&window,Qt::Key_End);
    QTRY_COMPARE_WITH_TIMEOUT(selection.currentId(),assetId(count-1),10000);
    const qint64 endNavigationMs=timer.elapsed();
    QTRY_VERIFY_WITH_TIMEOUT(delegateAt(gridView,QStringLiteral("assetGridDelegate"),count-1),10000);
    for (const QString &oldId:firstSubscriptions)
        QVERIFY2(provider.asset(oldId).isEmpty(),qPrintable(oldId));
    const int endDelegateCount=delegates(gridView,QStringLiteral("assetGridDelegate")).size();
    QVERIFY(endDelegateCount>0 && endDelegateCount<128);
    QVERIFY(provider.subscriptionCount()<=endDelegateCount);
    QVERIFY(provider.cacheBytes()<=provider.cacheBudgetBytes());
    for (QObject *tile:delegates(gridView,QStringLiteral("assetGridDelegate"))) {
        const int row=tile->property("index").toInt();
        if (row<0 || row>=count) continue;
        QCOMPARE(tile->property("stableId").toString(),assetId(row));
        QCOMPARE(tile->property("resourceId").toString(),resourceId(row));
    }
    QTest::keyClick(&window,Qt::Key_Left);
    QCOMPARE(selection.currentId(),assetId(count-2));
    QTest::keyClick(&window,Qt::Key_Return);
    QCOMPARE(gridActivated.size(),2);
    QCOMPARE(gridActivated.last().first().toString(),assetId(count-2));
    qInfo() << "100k End navigation ms" << endNavigationMs
            << "end delegates" << endDelegateCount << "cache bytes" << provider.cacheBytes();

    Density::instance()->setMode(Density::Comfortable);
    Theme::instance()->setPreset(QStringLiteral("sloom-light"));
    QTest::qWait(50);
    if (!captureDir.isEmpty()) {
        const QString output=QDir(QString::fromUtf8(captureDir)).filePath(QStringLiteral("asset-scale-comfortable.png"));
        QVERIFY(window.grabWindow().save(output));
    }
    QCOMPARE(gridItem->property("objectName").toString(),QStringLiteral("scaleGrid"));
    rootItem->setParentItem(nullptr); root.reset();
    QTRY_COMPARE(provider.subscriptionCount(),0);
    Theme::instance()->setPreset(QStringLiteral("sloom-dark"));
    Density::instance()->setMode(Density::Compact);
}
QTEST_MAIN(AssetGridScaleTest)
#include "tst_asset_grid_scale.moc"
