// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/asset_provider.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QMutex>
#include <QMutexLocker>
#include <QtTest>
#include <chrono>
#include <condition_variable>
#include <mutex>

using namespace QindaTK;

namespace {

class Gate final {
public:
    void wait()
    {
        std::unique_lock lock(m_mutex);
        ++m_entered;
        m_changed.notify_all();
        m_changed.wait(lock, [this] { return m_released; });
    }

    bool waitForEntered(int count)
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::seconds(5),
                                  [this, count] { return m_entered >= count; });
    }

    int entered() const
    {
        std::lock_guard lock(m_mutex);
        return m_entered;
    }

    void release()
    {
        std::lock_guard lock(m_mutex);
        m_released = true;
        m_changed.notify_all();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    int m_entered = 0;
    bool m_released = false;
};

class FakeAssetBackend final : public AssetBackend {
public:
    AssetResult fetch(const AssetRequest &request, const CancellationToken &cancel,
                      const JobProgress &) override
    {
        m_calls.fetch_add(1);
        {
            QMutexLocker lock(&m_mutex);
            m_uris.append(request.uri);
        }
        if (m_gate)
            m_gate->wait();
        if (cancel.isCancelled())
            m_cancelObserved.store(true);
        if (m_fail)
            return {{}, {}, {}, {}, {ErrorCode::BackendFailure,
                QStringLiteral("fixture asset failure"), {}, QStringLiteral("fixture")}};
        return {{}, QByteArray(m_byteCount, 'x'), QStringLiteral("application/octet-stream"),
                {}, {}};
    }

    int calls() const { return m_calls.load(); }
    void setGate(Gate *gate) { m_gate = gate; }
    void setFail(bool fail) { m_fail = fail; }
    void setByteCount(qsizetype count) { m_byteCount = count; }
    bool cancellationObserved() const { return m_cancelObserved.load(); }
    QStringList uris() const
    {
        QMutexLocker lock(&m_mutex);
        return m_uris;
    }

private:
    mutable QMutex m_mutex;
    QStringList m_uris;
    std::atomic_int m_calls = 0;
    std::atomic_bool m_cancelObserved = false;
    Gate *m_gate = nullptr;
    bool m_fail = false;
    qsizetype m_byteCount = 512;
};

AssetRequest requestFor(const QString &resourceId, const QString &uri,
                        const QString &kind = QStringLiteral("thumbnail"))
{
    AssetRequest request;
    request.resourceId = resourceId;
    request.uri = uri;
    request.kind = kind;
    request.size = QSize(80, 60);
    return request;
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return {};
    file.close();
    return path;
}

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

} // namespace

class TstAssetProvider final : public QObject {
    Q_OBJECT
private:
    static QString state(const AssetProvider &provider, const QString &id)
    {
        return provider.asset(id).value(QStringLiteral("state")).toString();
    }

private slots:
    void duplicateRequestsShareOneBackendFlight()
    {
        AssetProvider provider;
        auto backend = std::make_shared<FakeAssetBackend>();
        Gate gate;
        backend->setGate(&gate);
        provider.setBackend(backend);
        const AssetRequest request = requestFor(QStringLiteral("asset:duplicate"),
                                                QStringLiteral("file:///duplicate.bin"));
        const QString first = provider.request(request);
        QVERIFY(!first.isEmpty());
        QVERIFY(gate.waitForEntered(1));
        const QString second = provider.request(request);
        QVERIFY(!second.isEmpty());
        QCOMPARE(provider.inFlightCount(), 1);
        QCOMPARE(provider.subscriptionCount(), 2);
        QCOMPARE(backend->calls(), 1);
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, first), QStringLiteral("ready"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, second), QStringLiteral("ready"), 5000);
    }

    void cacheHitAndLeastRecentlyUsedEvictionRespectBudget()
    {
        AssetProvider provider;
        auto backend = std::make_shared<FakeAssetBackend>();
        backend->setByteCount(512); // Each cached result costs 512 bytes plus 1024 overhead.
        provider.setBackend(backend);
        provider.setCacheBudgetBytes(3072);

        const AssetRequest a = requestFor(QStringLiteral("asset:a"), QStringLiteral("a.bin"));
        const AssetRequest b = requestFor(QStringLiteral("asset:b"), QStringLiteral("b.bin"));
        const AssetRequest c = requestFor(QStringLiteral("asset:c"), QStringLiteral("c.bin"));
        const QString aFirst = provider.request(a);
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, aFirst), QStringLiteral("ready"), 5000);
        const QString aHit = provider.request(a);
        QCOMPARE(state(provider, aHit), QStringLiteral("ready"));
        QCOMPARE(backend->calls(), 1);

        const QString bFirst = provider.request(b);
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, bFirst), QStringLiteral("ready"), 5000);
        QCOMPARE(provider.cacheBytes(), 3072);
        const QString aRecent = provider.request(a);
        QCOMPARE(state(provider, aRecent), QStringLiteral("ready"));
        QCOMPARE(backend->calls(), 2);

        const QString cFirst = provider.request(c);
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, cFirst), QStringLiteral("ready"), 5000);
        QCOMPARE(provider.cacheBytes(), 3072);
        const QString bReloaded = provider.request(b);
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, bReloaded), QStringLiteral("ready"), 5000);
        QCOMPARE(backend->calls(), 4);
        QCOMPARE(provider.cacheBytes(), 3072);
    }

    void rapidViewInvalidationRejectsOldResultAndAllowsFreshRequest()
    {
        AssetProvider provider;
        auto backend = std::make_shared<FakeAssetBackend>();
        Gate gate;
        backend->setGate(&gate);
        provider.setBackend(backend);
        const AssetRequest request = requestFor(QStringLiteral("asset:scroll"),
                                                QStringLiteral("scroll.bin"));
        const QString oldId = provider.request(request, nullptr, QStringLiteral("view:filmstrip"));
        QVERIFY(!oldId.isEmpty());
        QVERIFY(gate.waitForEntered(1));
        provider.invalidateView(QStringLiteral("view:filmstrip"));
        QCOMPARE(state(provider, oldId), QStringLiteral("stale"));

        const QString freshId = provider.request(request, nullptr, QStringLiteral("view:filmstrip"));
        QVERIFY(!freshId.isEmpty());
        QVERIFY(gate.waitForEntered(2));
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, freshId), QStringLiteral("ready"), 5000);
        QCOMPARE(state(provider, oldId), QStringLiteral("stale"));
        QCOMPARE(backend->calls(), 2);
    }

    void documentRevisionAndDestructionRejectInFlightResults()
    {
        {
            AssetProvider provider;
            auto backend = std::make_shared<FakeAssetBackend>();
            Gate gate;
            backend->setGate(&gate);
            provider.setBackend(backend);
            PropertyDocument document;
            const QString id = provider.request(requestFor(QStringLiteral("asset:revision"),
                                                           QStringLiteral("revision.bin")),
                                                &document, QStringLiteral("view:revision"));
            QVERIFY(!id.isEmpty());
            QVERIFY(gate.waitForEntered(1));
            QVERIFY(document.resetModel({}));
            QCOMPARE(state(provider, id), QStringLiteral("stale"));
            gate.release();
            QTRY_COMPARE_WITH_TIMEOUT(state(provider, id), QStringLiteral("stale"), 5000);
        }
        {
            AssetProvider provider;
            auto backend = std::make_shared<FakeAssetBackend>();
            Gate gate;
            backend->setGate(&gate);
            provider.setBackend(backend);
            auto document = std::make_unique<PropertyDocument>();
            const QString id = provider.request(requestFor(QStringLiteral("asset:closed"),
                                                           QStringLiteral("closed.bin")),
                                                document.get(), QStringLiteral("view:closed"));
            QVERIFY(!id.isEmpty());
            QVERIFY(gate.waitForEntered(1));
            document.reset();
            QCOMPARE(state(provider, id), QStringLiteral("stale"));
            gate.release();
            QTRY_COMPARE_WITH_TIMEOUT(state(provider, id), QStringLiteral("stale"), 5000);
        }
    }

    void releasingLastSubscriptionCancelsBackendWork()
    {
        AssetProvider provider;
        auto backend = std::make_shared<FakeAssetBackend>();
        Gate gate;
        backend->setGate(&gate);
        provider.setBackend(backend);
        const QString id = provider.request(requestFor(QStringLiteral("asset:cancel"),
                                                       QStringLiteral("cancel.bin")));
        QVERIFY(!id.isEmpty());
        QVERIFY(gate.waitForEntered(1));
        QVERIFY(provider.release(id));
        QCOMPARE(provider.subscriptionCount(), 0);
        QCOMPARE(provider.inFlightCount(), 0);
        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(backend->cancellationObserved(), 5000);
    }

    void resolverRelinkInvalidatesCachedAssetsAndChangesResolvedUri()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray initial("first asset bytes");
        const QByteArray replacement("replacement asset bytes");
        const QString originalPath = writeFile(dir.filePath(QStringLiteral("old.bin")), initial);
        const QString replacementPath = writeFile(dir.filePath(QStringLiteral("new.bin")), replacement);
        QVERIFY(!originalPath.isEmpty());
        QVERIFY(!replacementPath.isEmpty());

        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:relink"), originalPath,
                                          sha256(initial)));
        AssetProvider provider;
        auto backend = std::make_shared<FakeAssetBackend>();
        provider.setBackend(backend);
        provider.setResolver(&resolver);
        const QString first = provider.requestAsset(QStringLiteral("asset:relink"),
                                                    QStringLiteral("thumbnail"));
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, first), QStringLiteral("ready"), 5000);
        QCOMPARE(provider.cacheBytes(), 1536);

        QVERIFY(resolver.relink(QStringLiteral("asset:relink"), replacementPath));
        QCOMPARE(provider.cacheBytes(), 0);
        const QString second = provider.requestAsset(QStringLiteral("asset:relink"),
                                                     QStringLiteral("thumbnail"));
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, second), QStringLiteral("ready"), 5000);
        QCOMPARE(backend->calls(), 2);
        QCOMPARE(backend->uris(), QStringList({originalPath, replacementPath}));
    }

    void backendFailureAndMissingResourceHaveDistinctStates()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray bytes("present");
        const QString path = writeFile(dir.filePath(QStringLiteral("present.bin")), bytes);
        const QString missing = dir.filePath(QStringLiteral("missing.bin"));
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("asset:present"), path, sha256(bytes)));
        QVERIFY(resolver.registerResource(QStringLiteral("asset:missing"), missing,
                                          sha256(QByteArrayLiteral("later"))));

        AssetProvider provider;
        auto backend = std::make_shared<FakeAssetBackend>();
        backend->setFail(true);
        provider.setBackend(backend);
        provider.setResolver(&resolver);
        const QString failed = provider.requestAsset(QStringLiteral("asset:present"),
                                                     QStringLiteral("waveform"));
        QTRY_COMPARE_WITH_TIMEOUT(state(provider, failed), QStringLiteral("failed"), 5000);
        QVERIFY(provider.asset(failed).value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("fixture asset failure")));

        const int beforeMissing = backend->calls();
        const QString absent = provider.requestAsset(QStringLiteral("asset:missing"),
                                                     QStringLiteral("thumbnail"));
        QVERIFY(!absent.isEmpty());
        QCOMPARE(state(provider, absent), QStringLiteral("missing"));
        QCOMPARE(backend->calls(), beforeMissing);
    }
};

QTEST_GUILESS_MAIN(TstAssetProvider)
#include "tst_asset_provider.moc"
