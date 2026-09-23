// SPDX-License-Identifier: LGPL-3.0-or-later
#include "job_model.h"
#include "property_document.h"

#include <QSignalSpy>
#include <QAccessible>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopedPointer>
#include <QMutex>
#include <QMutexLocker>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

using namespace QindaTK;

namespace {

class Gate final {
public:
    ~Gate() { release(); }

    void enterAndWait()
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

JobSpec quickJob(const QString &id, const QString &label, int priority,
                 JobWork work)
{
    JobSpec spec;
    spec.id = id;
    spec.label = label;
    spec.priority = priority;
    spec.work = std::move(work);
    return spec;
}

QString stateOf(const JobModel &model, const QString &id)
{
    return model.job(id).value(QStringLiteral("state")).toString();
}

QStringList *activeAnnouncements = nullptr;
void captureAnnouncement(QAccessibleEvent *event)
{
    if (activeAnnouncements && event->type() == QAccessible::Announcement)
        activeAnnouncements->append(
            static_cast<QAccessibleAnnouncementEvent *>(event)->message());
}
class AnnouncementCapture final {
public:
    explicit AnnouncementCapture(QStringList &messages)
        : m_wasActive(QAccessible::isActive()),
          m_previous(QAccessible::installUpdateHandler(&captureAnnouncement))
    {
        activeAnnouncements = &messages;
        QAccessible::setActive(true);
    }
    ~AnnouncementCapture()
    {
        activeAnnouncements = nullptr;
        QAccessible::installUpdateHandler(m_previous);
        QAccessible::setActive(m_wasActive);
    }
private:
    bool m_wasActive;
    QAccessible::UpdateHandler m_previous;
};

} // namespace

class TstJobModel final : public QObject {
    Q_OBJECT
private slots:
    void limitsBoundWorkerConcurrency()
    {
        JobModel model;
        model.setMaxConcurrent(2);
        Gate gate;
        std::atomic_int active = 0;
        std::atomic_int maximum = 0;
        QSignalSpy finished(&model, &JobModel::jobFinished);

        for (int i = 0; i < 5; ++i) {
            const QString id = QStringLiteral("bounded-%1").arg(i);
            QVERIFY(!model.enqueue(quickJob(id, id, 0,
                [&gate, &active, &maximum](const CancellationToken &, const JobProgress &) {
                    const int now = ++active;
                    int observed = maximum.load();
                    while (observed < now && !maximum.compare_exchange_weak(observed, now)) {}
                    gate.enterAndWait();
                    --active;
                    return JobResult{};
                })).isEmpty());
        }

        const bool twoStarted = gate.waitForEntered(2);
        const int enteredBeforeRelease = gate.entered();
        gate.release();
        QVERIFY(twoStarted);
        QCOMPARE(enteredBeforeRelease, 2);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 5, 5000);
        QCOMPARE(maximum.load(), 2);
        QCOMPARE(model.runningCount(), 0);
    }

    void queuedJobsDispatchByPriorityAndKeepFifoTies()
    {
        JobModel model;
        model.setMaxConcurrent(1);
        Gate gate;
        QMutex orderMutex;
        QStringList order;
        const auto record = [&orderMutex, &order](const QString &id) {
            QMutexLocker lock(&orderMutex);
            order.append(id);
        };
        QVERIFY(!model.enqueue(quickJob(QStringLiteral("blocker"), QStringLiteral("Blocker"), 0,
            [&gate, &record](const CancellationToken &, const JobProgress &) {
                gate.enterAndWait();
                record(QStringLiteral("blocker"));
                return JobResult{};
            })).isEmpty());
        QVERIFY(gate.waitForEntered(1));

        const auto add = [&model, &record](const QString &id, int priority) {
            return model.enqueue(quickJob(id, id, priority,
                [&record, id](const CancellationToken &, const JobProgress &) {
                    record(id);
                    return JobResult{};
                }));
        };
        QVERIFY(!add(QStringLiteral("low"), 1).isEmpty());
        QVERIFY(!add(QStringLiteral("high-first"), 9).isEmpty());
        QVERIFY(!add(QStringLiteral("middle"), 4).isEmpty());
        QVERIFY(!add(QStringLiteral("high-second"), 9).isEmpty());

        QSignalSpy finished(&model, &JobModel::jobFinished);
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 5, 5000);
        QCOMPARE(order, QStringList({QStringLiteral("blocker"), QStringLiteral("high-first"),
                                     QStringLiteral("high-second"), QStringLiteral("middle"),
                                     QStringLiteral("low")}));
    }

    void queuedAndRunningCancellationHaveTerminalState()
    {
        JobModel model;
        model.setMaxConcurrent(1);
        Gate gate;
        std::atomic_bool observedCancellation = false;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        const QString running = model.enqueue(quickJob(QStringLiteral("running"),
            QStringLiteral("Running"), 0,
            [&gate, &observedCancellation](const CancellationToken &token, const JobProgress &) {
                gate.enterAndWait();
                observedCancellation = token.isCancelled();
                return JobResult{};
            }));
        QVERIFY(!running.isEmpty());
        QVERIFY(gate.waitForEntered(1));
        const QString queued = model.enqueue(quickJob(QStringLiteral("queued"),
            QStringLiteral("Queued"), 0,
            [](const CancellationToken &, const JobProgress &) { return JobResult{}; }));
        QVERIFY(!queued.isEmpty());
        QVERIFY(model.cancelJob(queued));
        QCOMPARE(stateOf(model, queued), QStringLiteral("cancelled"));

        QVERIFY(model.cancelJob(running));
        QCOMPARE(stateOf(model, running), QStringLiteral("cancelling"));
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QCOMPARE(stateOf(model, running), QStringLiteral("cancelled"));
        QVERIFY(observedCancellation.load());
        QVERIFY(!model.cancelJob(running));
    }

    void cancellationAfterDurableCommitReportsSuccess()
    {
        JobModel model;
        Gate committedGate;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        const QString id = model.enqueue(quickJob(QStringLiteral("committed-export"),
            QStringLiteral("Export"), 0,
            [&committedGate](const CancellationToken &, const JobProgress &) {
                // The worker has crossed its irreversible output commit.
                JobResult result;
                result.output = {{QStringLiteral("uri"), QStringLiteral("file:///saved")}};
                result.committed = true;
                committedGate.enterAndWait();
                return result;
            }));
        QVERIFY(!id.isEmpty());
        QVERIFY(committedGate.waitForEntered(1));
        QVERIFY(model.cancelJob(id));
        QCOMPARE(stateOf(model, id), QStringLiteral("cancelling"));
        committedGate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(stateOf(model, id), QStringLiteral("succeeded"));
        QCOMPARE(model.job(id).value(QStringLiteral("result")).toMap()
                     .value(QStringLiteral("uri")).toString(), QStringLiteral("file:///saved"));
        QCOMPARE(model.job(id).value(QStringLiteral("result")).toMap()
                     .value(QStringLiteral("committed")).toBool(), true);
    }

    void failureRetainsStructuredDiagnostics()
    {
        JobModel model;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        const QString id = model.enqueue(quickJob(QStringLiteral("failed-export"),
            QStringLiteral("Export"), 0,
            [](const CancellationToken &, const JobProgress &) {
                JobResult result;
                result.error = {ErrorCode::BackendFailure, QStringLiteral("Cannot write"),
                                QStringLiteral("/destination"), QStringLiteral("file")};
                result.output = {{QStringLiteral("error"), QVariantMap{
                    {QStringLiteral("code"), QStringLiteral("BackendFailure")},
                    {QStringLiteral("fieldPath"), QStringLiteral("/destination")}}},
                    {QStringLiteral("committed"), true}};
                return result;
            }));
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(stateOf(model, id), QStringLiteral("failed"));
        const auto diagnostic = model.job(id).value(QStringLiteral("result")).toMap()
            .value(QStringLiteral("error")).toMap();
        QCOMPARE(diagnostic.value(QStringLiteral("fieldPath")).toString(),
                 QStringLiteral("/destination"));
        QVERIFY(!model.job(id).value(QStringLiteral("result")).toMap()
                     .contains(QStringLiteral("committed")));
    }

    void progressIsObservableAndBackendFailuresAreReported()
    {
        JobModel model;
        Gate gate;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        const QString id = model.enqueue(quickJob(QStringLiteral("progress"),
            QStringLiteral("Progress"), 0,
            [&gate](const CancellationToken &, const JobProgress &progress) {
                progress(0.37);
                gate.enterAndWait();
                progress(0.82);
                return JobResult{{}, {ErrorCode::BackendFailure,
                    QStringLiteral("fixture backend failed"), {}, QStringLiteral("fixture")}};
            }));
        QVERIFY(!id.isEmpty());
        QVERIFY(gate.waitForEntered(1));
        QTRY_COMPARE_WITH_TIMEOUT(model.job(id).value(QStringLiteral("progress")).toInt(), 37, 5000);
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(stateOf(model, id), QStringLiteral("failed"));
        QCOMPARE(model.job(id).value(QStringLiteral("progress")).toInt(), 82);
        QCOMPARE(model.job(id).value(QStringLiteral("error")).toString(),
                 QStringLiteral("fixture backend failed"));
        QCOMPARE(finished.constFirst().at(1).value<JobModel::JobState>(), JobModel::Failed);
    }

    void changedDocumentRevisionMakesResultStale()
    {
        PropertyDocument document;
        const EntityId entity = EntityId::create();
        QVERIFY(document.defineProperty({QStringLiteral("gain"), QMetaType::fromType<int>(),
                                         1, {}, true, false}));
        QVERIFY(document.addEntity(entity, {{QStringLiteral("gain"), 1}}));
        const SnapshotHeader expected = document.header();
        JobModel model;
        Gate gate;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        JobSpec spec = quickJob(QStringLiteral("stale-revision"), QStringLiteral("Stale"), 0,
            [&gate](const CancellationToken &, const JobProgress &) {
                gate.enterAndWait();
                return JobResult{{{QStringLiteral("value"), 7}}, {}};
            });
        spec.expected = expected;
        spec.document = &document;
        QVERIFY(!model.enqueue(std::move(spec)).isEmpty());
        QVERIFY(gate.waitForEntered(1));
        QVERIFY(document.setTargetWritable(entity, QStringLiteral("gain"), false));
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(stateOf(model, QStringLiteral("stale-revision")), QStringLiteral("stale"));
        QVERIFY(model.job(QStringLiteral("stale-revision")).value(QStringLiteral("result")).toMap().isEmpty());
    }

    void destroyedDocumentMakesResultStale()
    {
        auto *document = new PropertyDocument;
        const SnapshotHeader expected = document->header();
        JobModel model;
        Gate gate;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        JobSpec spec = quickJob(QStringLiteral("closed-document"), QStringLiteral("Closed"), 0,
            [&gate](const CancellationToken &, const JobProgress &) {
                gate.enterAndWait();
                return JobResult{};
            });
        spec.expected = expected;
        spec.document = document;
        QVERIFY(!model.enqueue(std::move(spec)).isEmpty());
        QVERIFY(gate.waitForEntered(1));
        delete document;
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(stateOf(model, QStringLiteral("closed-document")), QStringLiteral("stale"));
    }

    void invalidatedViewMakesRunningWorkStaleBeforeItsResultCanApply()
    {
        JobModel model;
        Gate gate;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        JobSpec spec = quickJob(QStringLiteral("recycled-view"), QStringLiteral("View work"), 0,
            [&gate](const CancellationToken &, const JobProgress &) {
                gate.enterAndWait();
                return JobResult{{{QStringLiteral("unsafe"), true}}, {}};
            });
        spec.viewId = QStringLiteral("view-a");
        QVERIFY(!model.enqueue(std::move(spec)).isEmpty());
        QVERIFY(gate.waitForEntered(1));
        model.invalidateView(QStringLiteral("view-a"));
        QCOMPARE(stateOf(model, QStringLiteral("recycled-view")), QStringLiteral("cancelling"));
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(stateOf(model, QStringLiteral("recycled-view")), QStringLiteral("stale"));
        QVERIFY(model.job(QStringLiteral("recycled-view")).value(QStringLiteral("result")).toMap().isEmpty());
    }

    void invalidatingQueuedViewMakesQueuedWorkStale()
    {
        JobModel model;
        model.setMaxConcurrent(1);
        Gate gate;
        QSignalSpy finished(&model, &JobModel::jobFinished);
        const QString blocker = model.enqueue(quickJob(QStringLiteral("view-blocker"),
            QStringLiteral("Blocker"), 0,
            [&gate](const CancellationToken &, const JobProgress &) {
                gate.enterAndWait();
                return JobResult{};
            }));
        QVERIFY(!blocker.isEmpty());
        QVERIFY(gate.waitForEntered(1));
        JobSpec queued = quickJob(QStringLiteral("queued-view"), QStringLiteral("Queued view"), 0,
            [](const CancellationToken &, const JobProgress &) { return JobResult{}; });
        queued.viewId = QStringLiteral("view-b");
        QVERIFY(!model.enqueue(std::move(queued)).isEmpty());
        model.invalidateView(QStringLiteral("view-b"));
        QCOMPARE(stateOf(model, QStringLiteral("queued-view")), QStringLiteral("stale"));
        gate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QCOMPARE(stateOf(model, QStringLiteral("queued-view")), QStringLiteral("stale"));
    }

    void finishedRowsArePrunedToRetentionLimit()
    {
        JobModel model;
        int serial = 0;
        for (int batch = 0; batch < 8; ++batch) {
            QSignalSpy finished(&model, &JobModel::jobFinished);
            for (int i = 0; i < 40; ++i) {
                const QString id = QStringLiteral("prune-%1").arg(serial++);
                QVERIFY(!model.enqueue(quickJob(id, id, 0,
                    [](const CancellationToken &, const JobProgress &) { return JobResult{}; })).isEmpty());
            }
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 40, 5000);
            QCOMPARE(model.runningCount(), 0);
            QCOMPARE(model.queuedCount(), 0);
        }
        QCOMPARE(model.rowCount(), 256);
        QVERIFY(model.job(QStringLiteral("prune-0")).isEmpty());
        QVERIFY(!model.job(QStringLiteral("prune-319")).isEmpty());
    }

    void toastAnnouncesActionThroughAccessibility()
    {
        QStringList messages;
        AnnouncementCapture capture(messages);
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QindaTK as Tk
Item {
    width: 420; height: 120
    Tk.NotificationModel { id: notices }
    Tk.Toast { id: toast; objectName: "liveToast"; notificationModel: notices; duration: 100000 }
    function post() {
        toast.announce("Export ready", "Saved to disk", "success", "export",
                       [{id: "open.result", label: "Open result"}])
    }
})", QUrl());
        QScopedPointer<QObject> fixture(component.create());
        QVERIFY2(fixture, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(fixture.data());
        QVERIFY(item);
        QQuickWindow window;
        item->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(20);
        QVERIFY(QMetaObject::invokeMethod(fixture.data(), "post"));
        QTRY_VERIFY_WITH_TIMEOUT(messages.contains(QStringLiteral(
            "Export ready. Saved to disk. Action available: Open result")), 1000);
        auto *toast = fixture->findChild<QObject *>(QStringLiteral("liveToast"));
        QVERIFY(toast);
        QVERIFY(toast->property("visible").toBool());
        item->setParentItem(nullptr);
    }

    void concurrentTaskCenterLifecycleUsesRealJobs()
    {
        JobModel model;
        model.setMaxConcurrent(2);
        Gate cancelledGate, survivorGate;
        std::atomic_bool sawCancellation = false;
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("liveJobs"), &model);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QindaTK as Tk
Tk.TaskCenter {
    objectName: "liveTaskCenter"
    width: 620; height: 320
    jobModel: liveJobs
})", QUrl());
        QScopedPointer<QObject> panel(component.create());
        QVERIFY2(panel, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(panel.data());
        QVERIFY(item);
        QQuickWindow window;
        item->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(20);
        QSignalSpy requested(panel.data(), SIGNAL(cancelRequested(QString)));
        QSignalSpy finished(&model, &JobModel::jobFinished);
        QVERIFY(!model.enqueue(quickJob(QStringLiteral("cancel-me"), QStringLiteral("Cancel me"), 0,
            [&cancelledGate, &sawCancellation](const CancellationToken &token, const JobProgress &) {
                cancelledGate.enterAndWait();
                sawCancellation = token.isCancelled();
                return JobResult{};
            })).isEmpty());
        QVERIFY(!model.enqueue(quickJob(QStringLiteral("survivor"), QStringLiteral("Keep running"), 0,
            [&survivorGate](const CancellationToken &, const JobProgress &) {
                survivorGate.enterAndWait();
                JobResult result;
                result.output = {{QStringLiteral("uri"), QStringLiteral("file:///saved")}};
                result.committed = true;
                return result;
            })).isEmpty());
        QVERIFY(cancelledGate.waitForEntered(1));
        QVERIFY(survivorGate.waitForEntered(1));
        auto *list = panel->findChild<QObject *>(QStringLiteral("taskCenterList"));
        QVERIFY(list);
        QTRY_COMPARE_WITH_TIMEOUT(list->property("count").toInt(), 2, 1000);
        QCOMPARE(stateOf(model, QStringLiteral("survivor")), QStringLiteral("running"));
        QQmlExpression cancel(engine.rootContext(), panel.data(),
                              QStringLiteral("cancelTask('cancel-me')"));
        QVERIFY(cancel.evaluate().toBool());
        QVERIFY2(!cancel.hasError(), qPrintable(cancel.error().toString()));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.constFirst().at(0).toString(), QStringLiteral("cancel-me"));
        QCOMPARE(stateOf(model, QStringLiteral("cancel-me")), QStringLiteral("cancelling"));
        QCOMPARE(stateOf(model, QStringLiteral("survivor")), QStringLiteral("running"));
        item->setParentItem(nullptr);
        panel.reset(); // Closing the view must not cancel either worker.
        QCOMPARE(stateOf(model, QStringLiteral("survivor")), QStringLiteral("running"));
        cancelledGate.release(); survivorGate.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QCOMPARE(stateOf(model, QStringLiteral("cancel-me")), QStringLiteral("cancelled"));
        QCOMPARE(stateOf(model, QStringLiteral("survivor")), QStringLiteral("succeeded"));
        QVERIFY(sawCancellation.load());
        const QVariantMap receipt = model.job(QStringLiteral("survivor"))
            .value(QStringLiteral("result")).toMap();
        QCOMPARE(receipt.value(QStringLiteral("committed")).toBool(), true);
        QCOMPARE(receipt.value(QStringLiteral("uri")).toString(), QStringLiteral("file:///saved"));
        QVERIFY(model.job(QStringLiteral("cancel-me")).value(QStringLiteral("result")).toMap().isEmpty());
        QScopedPointer<QObject> reopened(component.create());
        QVERIFY2(reopened, qPrintable(component.errorString()));
        auto *reopenedItem = qobject_cast<QQuickItem *>(reopened.data());
        QVERIFY(reopenedItem);
        reopenedItem->setParentItem(window.contentItem());
        auto *reopenedList = reopened->findChild<QObject *>(QStringLiteral("taskCenterList"));
        QVERIFY(reopenedList);
        QTRY_COMPARE_WITH_TIMEOUT(reopenedList->property("count").toInt(), 2, 1000);
        reopenedItem->setParentItem(nullptr);
    }
};

QTEST_MAIN(TstJobModel)
#include "tst_job_model.moc"
