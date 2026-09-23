// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/document_resource_links.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class TstDocumentResourceLinks final : public QObject {
    Q_OBJECT
private:
    static QString writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
        return path;
    }

    static QVariantMap manifestRow(const EntityId &id, const QByteArray &hash)
    {
        return {{QStringLiteral("resourceId"), QStringLiteral("asset:shared-audio")},
                {QStringLiteral("entityId"), id.toString()},
                {QStringLiteral("uriPropertyId"), QStringLiteral("uri")},
                {QStringLiteral("expectedSha256"), QString::fromLatin1(hash)}};
    }

    void defineSchema(PropertyDocument &document)
    {
        QVERIFY(document.defineProperty({QStringLiteral("uri"), QMetaType::fromType<QString>(),
                                         QString{}, {}, true, false}));
        QVERIFY(document.defineProperty({QStringLiteral("resourceId"), QMetaType::fromType<QString>(),
                                         QString{}, {}, false, false}));
        QVERIFY(document.defineProperty({QStringLiteral("startTick"), QMetaType::fromType<qint64>(),
                                         qint64(0), QStringLiteral("tick"), false, false}));
        QVERIFY(document.defineProperty({QStringLiteral("endTick"), QMetaType::fromType<qint64>(),
                                         qint64(0), QStringLiteral("tick"), false, false}));
    }

private slots:
    void sharedResourceRelinkUndoRedoSurvivesMissingOriginal()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray media("waveform bytes");
        const QByteArray hash = QCryptographicHash::hash(media, QCryptographicHash::Sha256).toHex();
        const QString original = writeFile(dir.filePath(QStringLiteral("original.wav")), media);
        const QString replacement = writeFile(dir.filePath(QStringLiteral("replacement.wav")), media);
        QVERIFY(!original.isEmpty());
        QVERIFY(!replacement.isEmpty());

        PropertyDocument document;
        defineSchema(document);
        const EntityId resource = EntityId::create();
        const EntityId clipOne = EntityId::create();
        const EntityId clipTwo = EntityId::create();
        QVERIFY(document.addEntity(resource, {{QStringLiteral("uri"), original}}));
        QVERIFY(document.addEntity(clipOne, {{QStringLiteral("resourceId"),
                                              QStringLiteral("asset:shared-audio")},
                                             {QStringLiteral("startTick"), qint64(240)},
                                             {QStringLiteral("endTick"), qint64(1440)}}));
        QVERIFY(document.addEntity(clipTwo, {{QStringLiteral("resourceId"),
                                              QStringLiteral("asset:shared-audio")},
                                             {QStringLiteral("startTick"), qint64(1920)},
                                             {QStringLiteral("endTick"), qint64(3120)}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        undo.setActiveDocument(&document);
        ResourceResolver resolver;
        DocumentResourceLinks links;
        links.setDocument(&document);
        links.setUndoService(&undo);
        links.setResolver(&resolver);
        links.setManifest({manifestRow(resource, hash)});
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("uri")).toString(), original);
        const qint64 oneStart = document.value(EntityId::parse(clipOne.toString()),
                                               QStringLiteral("startTick")).toLongLong();
        const qint64 oneEnd = document.value(clipOne, QStringLiteral("endTick")).toLongLong();
        const qint64 twoStart = document.value(clipTwo, QStringLiteral("startTick")).toLongLong();
        const qint64 twoEnd = document.value(clipTwo, QStringLiteral("endTick")).toLongLong();
        const QString initialRevision = document.revisionString();

        QVERIFY(links.relink(QStringLiteral("asset:shared-audio"), replacement,
                             initialRevision));
        QCOMPARE(document.committedValue(resource, QStringLiteral("uri")).toString(), replacement);
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("uri")).toString(), replacement);
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("expectedSha256")).toString(), QString::fromLatin1(hash));
        QCOMPARE(document.value(clipOne, QStringLiteral("startTick")).toLongLong(), oneStart);
        QCOMPARE(document.value(clipOne, QStringLiteral("endTick")).toLongLong(), oneEnd);
        QCOMPARE(document.value(clipTwo, QStringLiteral("startTick")).toLongLong(), twoStart);
        QCOMPARE(document.value(clipTwo, QStringLiteral("endTick")).toLongLong(), twoEnd);
        QVERIFY(undo.canUndo());
        QCOMPARE(undo.undoText(), QStringLiteral("Relink resource"));

        QVERIFY(QFile::remove(original));
        QVERIFY(undo.undo());
        QCOMPARE(document.committedValue(resource, QStringLiteral("uri")).toString(), original);
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("uri")).toString(), original);
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("availability")).toString(), QStringLiteral("missing"));
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("expectedSha256")).toString(), QString::fromLatin1(hash));
        QVERIFY(!undo.canUndo());
        QVERIFY(undo.canRedo());
        QVERIFY(undo.redo());
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("uri")).toString(), replacement);
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("availability")).toString(), QStringLiteral("ready"));
        QCOMPARE(document.value(clipOne, QStringLiteral("startTick")).toLongLong(), oneStart);
        QCOMPARE(document.value(clipOne, QStringLiteral("endTick")).toLongLong(), oneEnd);
        QCOMPARE(document.value(clipTwo, QStringLiteral("startTick")).toLongLong(), twoStart);
        QCOMPARE(document.value(clipTwo, QStringLiteral("endTick")).toLongLong(), twoEnd);
        QVERIFY(!undo.canRedo());
    }

    void rejectsStaleRevisionAndInvalidDestinationWithoutMutation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray media("audio");
        const QByteArray hash = QCryptographicHash::hash(media, QCryptographicHash::Sha256).toHex();
        const QString original = writeFile(dir.filePath(QStringLiteral("original.wav")), media);
        PropertyDocument document;
        defineSchema(document);
        const EntityId resource = EntityId::create();
        QVERIFY(document.addEntity(resource, {{QStringLiteral("uri"), original}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        undo.setActiveDocument(&document);
        ResourceResolver resolver;
        DocumentResourceLinks links;
        links.setDocument(&document);
        links.setUndoService(&undo);
        links.setResolver(&resolver);
        links.setManifest({manifestRow(resource, hash)});
        const QString revision = document.revisionString();
        QVERIFY(!links.relink(QStringLiteral("asset:shared-audio"),
                              dir.filePath(QStringLiteral("absent.wav")), revision));
        QCOMPARE(document.committedValue(resource, QStringLiteral("uri")).toString(), original);
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("uri")).toString(), original);
        QVERIFY(!links.relink(QStringLiteral("asset:missing-id"), original, revision));
        QVERIFY(!links.relink(QStringLiteral("asset:shared-audio"), original,
                              QStringLiteral("0")));
        QVERIFY(document.removeEntity(resource));
        QVERIFY(!links.relink(QStringLiteral("asset:shared-audio"), original,
                              document.revisionString()));
        QCOMPARE(resolver.lookup(QStringLiteral("asset:shared-audio"))
                     .value(QStringLiteral("uri")).toString(), original);
        QVERIFY(!undo.canUndo());
    }

    void documentSwitchAndCloseRetireOldMappings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray firstBytes("first document resource");
        const QByteArray secondBytes("second document resource");
        const QString firstPath = writeFile(dir.filePath(QStringLiteral("first.wav")), firstBytes);
        const QString secondPath = writeFile(dir.filePath(QStringLiteral("second.wav")), secondBytes);
        const QString resourceId = QStringLiteral("asset:per-document");
        PropertyDocument first;
        PropertyDocument second;
        defineSchema(first);
        defineSchema(second);
        const EntityId firstResource = EntityId::create();
        const EntityId secondResource = EntityId::create();
        QVERIFY(first.addEntity(firstResource, {{QStringLiteral("uri"), firstPath}}));
        QVERIFY(second.addEntity(secondResource, {{QStringLiteral("uri"), secondPath}}));
        ResourceResolver resolver;
        DocumentResourceLinks links;
        links.setResolver(&resolver);
        links.setDocument(&first);
        const QVariantMap firstRow{{QStringLiteral("resourceId"), resourceId},
                                   {QStringLiteral("entityId"), firstResource.toString()},
                                   {QStringLiteral("expectedSha256"),
                                    QString::fromLatin1(QCryptographicHash::hash(
                                        firstBytes, QCryptographicHash::Sha256).toHex())}};
        links.setManifest({firstRow});
        QCOMPARE(resolver.lookup(resourceId).value(QStringLiteral("uri")).toString(), firstPath);

        links.setDocument(&second);
        QVERIFY(resolver.lookup(resourceId).isEmpty());
        const QVariantMap secondRow{{QStringLiteral("resourceId"), resourceId},
                                    {QStringLiteral("entityId"), secondResource.toString()},
                                    {QStringLiteral("expectedSha256"),
                                     QString::fromLatin1(QCryptographicHash::hash(
                                         secondBytes, QCryptographicHash::Sha256).toHex())}};
        links.setManifest({secondRow});
        QCOMPARE(resolver.lookup(resourceId).value(QStringLiteral("uri")).toString(), secondPath);
        QCOMPARE(resolver.lookup(resourceId).value(QStringLiteral("expectedSha256")).toString(),
                 QString::fromLatin1(QCryptographicHash::hash(
                     secondBytes, QCryptographicHash::Sha256).toHex()));

        links.setDocument(nullptr);
        QVERIFY(resolver.lookup(resourceId).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstDocumentResourceLinks)
#include "tst_document_resource_links.moc"
