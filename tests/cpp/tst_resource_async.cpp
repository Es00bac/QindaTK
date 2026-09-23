// SPDX-License-Identifier: LGPL-3.0-or-later
#include "job_model.h"
#include "resource_resolver.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <chrono>
#include <condition_variable>
#include <mutex>

using namespace QindaTK;

namespace {

class Gate final {
public:
    ~Gate() { release(); }

    void wait()
    {
        std::unique_lock lock(m_mutex);
        m_entered = true;
        m_changed.notify_all();
        m_changed.wait(lock, [this] { return m_released; });
    }

    bool waitForEntered()
    {
        std::unique_lock lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::seconds(5),
                                  [this] { return m_entered; });
    }

    void release()
    {
        std::lock_guard lock(m_mutex);
        m_released = true;
        m_changed.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_changed;
    bool m_entered = false;
    bool m_released = false;
};

QString writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size())
        return {};
    file.close();
    return QFileInfo(path).absoluteFilePath();
}

QByteArray sha256(const QByteArray &contents)
{
    return QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
}

QString queuedBlocker(JobModel &jobs, Gate &gate)
{
    JobSpec spec;
    spec.id = QStringLiteral("async-test-blocker");
    spec.label = QStringLiteral("Hold the only worker");
    spec.work = [&gate](const CancellationToken &, const JobProgress &) {
        gate.wait();
        return JobResult{};
    };
    return jobs.enqueue(std::move(spec));
}

QString stateOf(const JobModel &jobs, const QString &id)
{
    return jobs.job(id).value(QStringLiteral("state")).toString();
}

} // namespace

class TstResourceAsync final : public QObject {
    Q_OBJECT
private slots:
    void asynchronousRefreshDetectsModificationAndAdvancesRevision()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray original("original resource bytes");
        const QString path = writeFile(directory.filePath(QStringLiteral("media.bin")), original);
        QVERIFY(!path.isEmpty());

        ResourceResolver resolver;
        const QString resourceId = QStringLiteral("asset:async-refresh");
        QVERIFY(resolver.registerResource(resourceId, path, sha256(original)));
        const QVariantMap before = resolver.lookup(resourceId);
        QVERIFY(!writeFile(path, QByteArrayLiteral("updated resource bytes")).isEmpty());

        JobModel jobs;
        QSignalSpy finished(&jobs, &JobModel::jobFinished);
        const QString jobId = resolver.refreshAsync(&jobs, resourceId);
        QVERIFY(!jobId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(jobs, jobId), QStringLiteral("succeeded"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);

        const QVariantMap after = resolver.lookup(resourceId);
        QCOMPARE(after.value(QStringLiteral("availability")).toString(), QStringLiteral("modified"));
        QCOMPARE(after.value(QStringLiteral("sha256")).toByteArray(),
                 sha256(QByteArrayLiteral("updated resource bytes")));
        QCOMPARE(after.value(QStringLiteral("expectedSha256")).toByteArray(), sha256(original));
        QVERIFY(after.value(QStringLiteral("revision")).toString().toULongLong()
                > before.value(QStringLiteral("revision")).toString().toULongLong());
    }

    void refreshResultForOldUriAndRevisionIsIgnoredAfterRelink()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray oldBytes("old location bytes");
        const QByteArray newBytes("new location bytes");
        const QString oldPath = writeFile(directory.filePath(QStringLiteral("old.bin")), oldBytes);
        const QString newPath = writeFile(directory.filePath(QStringLiteral("new.bin")), newBytes);
        QVERIFY(!oldPath.isEmpty());
        QVERIFY(!newPath.isEmpty());

        ResourceResolver resolver;
        const QString resourceId = QStringLiteral("asset:relinked");
        QVERIFY(resolver.registerResource(resourceId, oldPath, sha256(oldBytes)));
        JobModel jobs;
        jobs.setMaxConcurrent(1);
        Gate gate;
        const QString blocker = queuedBlocker(jobs, gate);
        QVERIFY(!blocker.isEmpty());
        QVERIFY(gate.waitForEntered());

        QSignalSpy resourceChanged(&resolver, &ResourceResolver::resourceChanged);
        const QString refreshId = resolver.refreshAsync(&jobs, resourceId);
        QVERIFY(!refreshId.isEmpty());
        QCOMPARE(stateOf(jobs, refreshId), QStringLiteral("queued"));
        QVERIFY(resolver.relink(resourceId, newPath));
        const QVariantMap afterRelink = resolver.lookup(resourceId);
        QCOMPARE(afterRelink.value(QStringLiteral("uri")).toString(), newPath);
        const QString relinkRevision = afterRelink.value(QStringLiteral("revision")).toString();
        QCOMPARE(resourceChanged.size(), 1);

        QSignalSpy finished(&jobs, &JobModel::jobFinished);
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(jobs, refreshId), QStringLiteral("succeeded"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(finished.size() >= 2, 5000);
        const QVariantMap afterRefresh = resolver.lookup(resourceId);
        QCOMPARE(afterRefresh.value(QStringLiteral("uri")).toString(), newPath);
        QCOMPARE(afterRefresh.value(QStringLiteral("revision")).toString(), relinkRevision);
        QCOMPARE(afterRefresh.value(QStringLiteral("sha256")).toByteArray(), sha256(newBytes));
        QCOMPARE(resourceChanged.size(), 1); // The stale completion emitted no change.
    }

    void asynchronousRelinkProposalsAreExactAndDoNotMutateMappings()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray payload("matching media payload");
        const QString missingPath = directory.filePath(QStringLiteral("missing.wav"));
        const QString matchPath = writeFile(directory.filePath(QStringLiteral("match.wav")), payload);
        const QString wrongPath = writeFile(directory.filePath(QStringLiteral("wrong.wav")),
                                            QByteArrayLiteral("different payload"));
        QVERIFY(!matchPath.isEmpty());
        QVERIFY(!wrongPath.isEmpty());

        ResourceResolver resolver;
        const QString resourceId = QStringLiteral("asset:missing");
        QVERIFY(resolver.registerResource(resourceId, missingPath, sha256(payload)));
        const QVariantMap before = resolver.lookup(resourceId);

        JobModel jobs;
        QSignalSpy finished(&jobs, &JobModel::jobFinished);
        const QString jobId = resolver.proposeRelinksAsync(
            &jobs, {resourceId}, {wrongPath, matchPath, matchPath});
        QVERIFY(!jobId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(stateOf(jobs, jobId), QStringLiteral("succeeded"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);

        const QVariantList proposals = jobs.job(jobId).value(QStringLiteral("result"))
                                           .toMap().value(QStringLiteral("proposals")).toList();
        QCOMPARE(proposals.size(), 1);
        const QVariantMap proposal = proposals.constFirst().toMap();
        QCOMPARE(proposal.value(QStringLiteral("resourceId")).toString(), resourceId);
        QCOMPARE(proposal.value(QStringLiteral("currentUri")).toString(), missingPath);
        QCOMPARE(proposal.value(QStringLiteral("proposedUri")).toString(), matchPath);
        QCOMPARE(proposal.value(QStringLiteral("sha256")).toString(), QString::fromLatin1(sha256(payload)));
        const QVariantMap after = resolver.lookup(resourceId);
        QCOMPARE(after.value(QStringLiteral("uri")), before.value(QStringLiteral("uri")));
        QCOMPARE(after.value(QStringLiteral("revision")), before.value(QStringLiteral("revision")));
        QCOMPARE(after.value(QStringLiteral("availability")), before.value(QStringLiteral("availability")));
    }

    void cancellingQueuedAsyncRefreshDoesNotApplyHash()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray original("baseline bytes");
        const QString path = writeFile(directory.filePath(QStringLiteral("queued.bin")), original);
        QVERIFY(!path.isEmpty());

        ResourceResolver resolver;
        const QString resourceId = QStringLiteral("asset:queued-cancel");
        QVERIFY(resolver.registerResource(resourceId, path, sha256(original)));
        const QVariantMap before = resolver.lookup(resourceId);
        QVERIFY(!writeFile(path, QByteArrayLiteral("changed while queued")).isEmpty());

        JobModel jobs;
        jobs.setMaxConcurrent(1);
        Gate gate;
        const QString blocker = queuedBlocker(jobs, gate);
        QVERIFY(!blocker.isEmpty());
        QVERIFY(gate.waitForEntered());
        QSignalSpy finished(&jobs, &JobModel::jobFinished);
        const QString refreshId = resolver.refreshAsync(&jobs, resourceId);
        QVERIFY(!refreshId.isEmpty());
        QCOMPARE(stateOf(jobs, refreshId), QStringLiteral("queued"));
        QVERIFY(jobs.cancelJob(refreshId));
        QCOMPARE(stateOf(jobs, refreshId), QStringLiteral("cancelled"));

        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(finished.size() >= 2, 5000);
        const QVariantMap after = resolver.lookup(resourceId);
        QCOMPARE(after.value(QStringLiteral("sha256")), before.value(QStringLiteral("sha256")));
        QCOMPARE(after.value(QStringLiteral("revision")), before.value(QStringLiteral("revision")));
        QCOMPARE(after.value(QStringLiteral("availability")), before.value(QStringLiteral("availability")));
    }
};

QTEST_GUILESS_MAIN(TstResourceAsync)
#include "tst_resource_async.moc"
