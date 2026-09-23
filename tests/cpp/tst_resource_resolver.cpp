// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/resource_resolver.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

class TstResourceResolver final : public QObject {
    Q_OBJECT
private:
    static QString writeFile(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        if (file.write(contents) != contents.size())
            return {};
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }

    static QByteArray hash(const QByteArray &contents)
    {
        return QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
    }

private slots:
    void missingFileIsExplicitAndCanRecover()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("missing.wav"));
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:voice"), path,
                                          hash(QByteArrayLiteral("voice"))));
        QCOMPARE(resolver.resolve(QStringLiteral("asset:voice")).value(
                     QStringLiteral("availability")).toString(), QStringLiteral("missing"));
        QVERIFY(resolver.lastError().isEmpty());
        QVERIFY(!writeFile(path, QByteArrayLiteral("voice")).isEmpty());
        QVERIFY(resolver.refresh(QStringLiteral("asset:voice")));
        QCOMPARE(resolver.resolve(QStringLiteral("asset:voice")).value(
                     QStringLiteral("availability")).toString(), QStringLiteral("ready"));
    }

    void changedContentsReportModifiedAndAdvanceRevision()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir.filePath(QStringLiteral("clip.wav")),
                                       QByteArrayLiteral("original"));
        QVERIFY(!path.isEmpty());
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:clip"), path,
                                          hash(QByteArrayLiteral("original"))));
        const QString before = resolver.resolve(QStringLiteral("asset:clip"))
                                  .value(QStringLiteral("revision")).toString();
        QVERIFY(!writeFile(path, QByteArrayLiteral("replaced" )).isEmpty());
        const QVariantMap modified = resolver.resolve(QStringLiteral("asset:clip"));
        QCOMPARE(modified.value(QStringLiteral("availability")).toString(),
                 QStringLiteral("modified"));
        QVERIFY(modified.value(QStringLiteral("revision")).toString().toULongLong()
                > before.toULongLong());
        QCOMPARE(modified.value(QStringLiteral("expectedSha256")).toByteArray(),
                 hash(QByteArrayLiteral("original")));
    }

    void lookupUsesCachedStateUntilExplicitRefresh()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = writeFile(dir.filePath(QStringLiteral("cached.bin")),
                                       QByteArrayLiteral("before"));
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:cached"), path,
                                          hash(QByteArrayLiteral("before"))));
        const QVariantMap before = resolver.lookup(QStringLiteral("asset:cached"));
        QVERIFY(!writeFile(path, QByteArrayLiteral("after" )).isEmpty());
        const QVariantMap cached = resolver.lookup(QStringLiteral("asset:cached"));
        QCOMPARE(cached.value(QStringLiteral("sha256")), before.value(QStringLiteral("sha256")));
        QCOMPARE(cached.value(QStringLiteral("revision")), before.value(QStringLiteral("revision")));
        QVERIFY(resolver.refresh(QStringLiteral("asset:cached")));
        QCOMPARE(resolver.lookup(QStringLiteral("asset:cached"))
                     .value(QStringLiteral("availability")).toString(),
                 QStringLiteral("modified"));
    }

    void relinkChangesOneLogicalMappingUsedByRepeatedReferences()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray payload("same media bytes");
        const QString oldPath = writeFile(dir.filePath(QStringLiteral("old.bin")), payload);
        const QString newPath = writeFile(dir.filePath(QStringLiteral("new.bin")), payload);
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:shared"), oldPath, hash(payload)));
        // References store the logical ID; each subsequent resolution follows its one mapping.
        const QStringList documentReferences{QStringLiteral("asset:shared"),
                                             QStringLiteral("asset:shared")};
        QVERIFY(resolver.relink(QStringLiteral("asset:shared"), newPath));
        for (const QString &id : documentReferences)
            QCOMPARE(resolver.resolve(id).value(QStringLiteral("uri")).toString(), newPath);
    }

    void invalidRelinkLeavesMappingUntouched()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldPath = writeFile(dir.filePath(QStringLiteral("old.bin")),
                                          QByteArrayLiteral("media"));
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:media"), oldPath,
                                          hash(QByteArrayLiteral("media"))));
        const QVariantMap before = resolver.resolve(QStringLiteral("asset:media"));
        QVERIFY(!resolver.relink(QStringLiteral("asset:media"),
                                 dir.filePath(QStringLiteral("absent.bin"))));
        QVERIFY(!resolver.lastError().isEmpty());
        const QVariantMap after = resolver.resolve(QStringLiteral("asset:media"));
        QCOMPARE(after.value(QStringLiteral("uri")), before.value(QStringLiteral("uri")));
        QCOMPARE(after.value(QStringLiteral("revision")), before.value(QStringLiteral("revision")));
    }

    void batchSearchOffersExactHashProposalsWithoutMutation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray payload("searchable media");
        const QString missing = dir.filePath(QStringLiteral("removed.bin"));
        const QString match = writeFile(dir.filePath(QStringLiteral("candidate.bin")), payload);
        const QString wrong = writeFile(dir.filePath(QStringLiteral("wrong.bin")),
                                        QByteArrayLiteral("different"));
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:lost"), missing, hash(payload)));
        const QVariantList proposals = resolver.proposeRelinks(
            {QStringLiteral("asset:lost")}, {wrong, match});
        QCOMPARE(proposals.size(), 1);
        QCOMPARE(proposals.first().toMap().value(QStringLiteral("proposedUri")).toString(), match);
        QCOMPARE(resolver.resolve(QStringLiteral("asset:lost"))
                     .value(QStringLiteral("uri")).toString(), missing);
    }
};

QTEST_GUILESS_MAIN(TstResourceResolver)
#include "tst_resource_resolver.moc"
