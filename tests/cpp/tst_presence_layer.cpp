// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_property_sync.h"
#include "reference_document_codec.h"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

using namespace QindaTK;

namespace {
QQuickItem *findVisual(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name) return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = findVisual(child, name)) return found;
    return nullptr;
}
}

class TstPresenceLayer final : public QObject {
    Q_OBJECT
private slots:
    void twoClientsMapLeaveAndExpireWithoutEditingDocument()
    {
        PropertyDocument base;
        const auto snapshot = ReferenceDocumentCodec::capture(base);
        QVERIFY(snapshot);
        auto first = ReferenceDocumentCodec::decode(snapshot->payload);
        auto second = ReferenceDocumentCodec::decode(snapshot->payload);
        QVERIFY(first && second);
        LocalPropertySyncService service;
        service.setDocumentId(base.documentIdString());
        QVERIFY(service.open());
        LocalPropertySyncClient a, b;
        a.setDocument(first->document.get());
        a.setService(&service);
        a.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000a"));
        b.setDocument(second->document.get());
        b.setService(&service);
        b.setActorId(QStringLiteral("00000000-0000-0000-0000-00000000000b"));
        QVERIFY(a.connectToService());
        QVERIFY(b.connectToService());

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QindaTK as Tk
            Item {
                id: host
                width: 320; height: 200
                property real shift: 0
                Tk.PresenceLayer {
                    objectName: "presenceLayer"
                    anchors.fill: parent
                    mapper: function(anchor, kind, presence) {
                        if (anchor.objectId !== "scene/title") return null
                        return kind === "cursor"
                            ? {x: 30 + host.shift, y: 45, width: 0, height: 18}
                            : {x: 30 + host.shift, y: 45, width: 88, height: 18}
                    }
                }
            }
        )", QUrl(QStringLiteral("file:///presence-test.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(320, 200);
        auto *host = qobject_cast<QQuickItem *>(component.create());
        QVERIFY2(host != nullptr, qPrintable(component.errorString()));
        host->setParent(window.contentItem());
        host->setParentItem(window.contentItem());
        window.show();
        QTRY_VERIFY(window.isVisible());
        auto *layer = host->findChild<QQuickItem *>(QStringLiteral("presenceLayer"));
        QVERIFY(layer);
        layer->setProperty("client", QVariant::fromValue(static_cast<QObject *>(&b)));
        QCOMPARE(layer->property("peers").toList().size(), 0);

        a.setPresence({{QStringLiteral("label"), QStringLiteral("Alice")},
                       {QStringLiteral("activity"), QStringLiteral("Selecting title")},
                       {QStringLiteral("cursor"), QVariantMap{{QStringLiteral("objectId"),
                                                               QStringLiteral("scene/title")}}},
                       {QStringLiteral("selection"), QVariantMap{{QStringLiteral("objectId"),
                                                                  QStringLiteral("scene/title")}}}});
        QTRY_COMPARE(layer->property("peers").toList().size(), 1);
        const QString markerName = QStringLiteral("presencePeer_") + a.actorId();
        QTRY_VERIFY(findVisual(layer, markerName));
        auto *marker = findVisual(layer, markerName);
        auto *cursor = findVisual(marker, QStringLiteral("remoteCursor"));
        auto *selection = findVisual(marker, QStringLiteral("remoteSelection"));
        auto *activity = findVisual(marker, QStringLiteral("remoteActivity"));
        QVERIFY(cursor && selection && activity);
        QCOMPARE(cursor->x(), 30.0);
        QCOMPARE(selection->width(), 88.0);
        QVERIFY(activity->isVisible());

        host->setProperty("shift", 27.0);
        layer->setProperty("viewRevision", 1);
        QTRY_VERIFY(findVisual(layer, markerName));
        auto *movedCursor = findVisual(findVisual(layer, markerName),
                                       QStringLiteral("remoteCursor"));
        QVERIFY(movedCursor);
        QCOMPARE(movedCursor->x(), 57.0);
        QCOMPARE(first->document->revision().value, 0ULL);
        QCOMPARE(second->document->revision().value, 0ULL);
        QCOMPARE(service.operationCount(), 0);

        a.disconnectFromService();
        QTRY_COMPARE(layer->property("peers").toList().size(), 0);
        QTRY_VERIFY(!findVisual(layer, markerName));
        QVERIFY(a.connectToService());
        a.setPresence({{QStringLiteral("label"), QStringLiteral("Alice")},
                       {QStringLiteral("cursor"), QVariantMap{{QStringLiteral("objectId"),
                                                               QStringLiteral("scene/title")}}}});
        QTRY_COMPARE(layer->property("peers").toList().size(), 1);
        layer->setProperty("staleAfterMs", 1);
        QTRY_COMPARE(layer->property("peers").toList().size(), 0);
        QCOMPARE(service.operationCount(), 0);
    }
};

QTEST_MAIN(TstPresenceLayer)
#include "tst_presence_layer.moc"
