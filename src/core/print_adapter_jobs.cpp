// SPDX-License-Identifier: LGPL-3.0-or-later
// Print jobs (D-131): a snapshot of the document, one sheet per event-loop
// turn, cancellation between sheets, and failures that name the pages left
// unprinted while the ticket stays as it was.
#include "print_adapter_p.h"

#include <QTimer>
#include <algorithm>

namespace QindaTK {

bool PrintAdapter::busy() const
{
    return m_job != nullptr;
}

QVariantMap PrintAdapter::job() const
{
    if (!m_job) return m_lastJob;
    QVariantMap map = PrintDetail::idleJob();
    const int count = int(m_job->sheets.size());
    map.insert(QStringLiteral("id"), m_job->id);
    map.insert(QStringLiteral("status"), m_job->status);
    map.insert(QStringLiteral("printerId"), m_job->printerId);
    map.insert(QStringLiteral("outputPath"), m_job->outputPath);
    map.insert(QStringLiteral("sheetCount"), count);
    map.insert(QStringLiteral("sheetsDone"), m_job->next);
    map.insert(QStringLiteral("progress"), count > 0 ? double(m_job->next) / count : 0.0);
    return map;
}

QString PrintAdapter::print(const QVariantMap &patch)
{
    clearError();
    if (m_job) {
        setError(ErrorCode::Conflict, tr("A print job is already running"));
        return {};
    }
    if (!patch.isEmpty() && !applySettings(patch)) return {};
    RichTextLayout *live = layout();
    if (!live) {
        setError(ErrorCode::InvalidArgument, tr("There is no document to print"));
        return {};
    }
    const Print::Capabilities target = destination();
    // Options the device cannot honour are refused before anything is sent;
    // document issues (fonts, images) are preflight's to report, not to block.
    const QVector<ValidationIssue> problems = Print::validateTicket(m_ticket, target, live->pageCount());
    if (!problems.isEmpty()) {
        setError(problems.first().code, problems.first().message);
        return {};
    }
    Print::BackendPtr backend;
    for (const auto &entry : std::as_const(m_destinations))
        if (entry.first.printerId == m_ticket.printerId) backend = entry.second;
    if (!backend) {
        setError(ErrorCode::Unavailable, tr("%1 is not available").arg(m_ticket.printerId));
        return {};
    }

    auto job = std::make_unique<Job>();
    job->snapshot = std::make_unique<RichTextDocument>();
    if (!job->snapshot->loadJson(m_document->toJson())) {
        setError(ErrorCode::BackendFailure, tr("Cannot copy the document for printing: %1").arg(job->snapshot->lastError()));
        return {};
    }
    job->layout = std::make_unique<RichTextLayout>();
    job->layout->setMode(RichTextLayout::Mode::Paged);
    job->layout->setDocument(job->snapshot.get());
    job->layout->paginateAll();
    job->sheets = Print::planSheets(job->snapshot->pageSetupValue(), job->layout->pageCount(), m_ticket,
                                    Print::resolvePaper(target, m_ticket.paperId));
    if (job->sheets.isEmpty()) {
        setError(ErrorCode::InvalidArgument, tr("No pages are selected"));
        return {};
    }
    job->id = QStringLiteral("print-%1").arg(++m_jobCounter);
    job->status = QStringLiteral("pending");
    job->ticket = m_ticket;
    job->printerId = m_ticket.printerId;
    job->outputPath = Print::localPath(m_ticket.outputPath);
    job->images = std::make_unique<RichTextImageRenderer>();
    job->backend = backend;
    QString error;
    job->device = backend->open(m_ticket, target, &error);
    m_job = std::move(job);
    const QString id = m_job->id;
    if (!m_job->device) {
        finishJob(QStringLiteral("failed"), error.isEmpty() ? tr("Cannot open %1").arg(target.label) : error, 0);
        return id;
    }
    emit jobChanged();
    QTimer::singleShot(0, this, &PrintAdapter::stepJob);
    return id;
}

bool PrintAdapter::cancel(const QString &jobId)
{
    clearError();
    if (!m_job || m_job->id != jobId) {
        setError(ErrorCode::InvalidArgument, tr("No running print job %1").arg(jobId));
        return false;
    }
    if (!m_job->cancelRequested) {
        m_job->cancelRequested = true;
        m_job->status = QStringLiteral("cancelling");
        emit jobChanged();
    }
    return true;
}

void PrintAdapter::stepJob()
{
    if (!m_job || !m_job->device) return;
    Job &job = *m_job;
    if (job.cancelRequested) {
        job.device->abort();
        finishJob(QStringLiteral("cancelled"), tr("Print job cancelled"), -1);
        return;
    }
    QString error;
    if (job.next >= job.sheets.size()) {
        if (!job.device->finish(&error)) {
            job.device->abort();
            finishJob(QStringLiteral("failed"), error, -1);
        } else {
            finishJob(QStringLiteral("completed"), {}, -1);
        }
        return;
    }
    job.status = QStringLiteral("running");
    const Print::Sheet &sheet = job.sheets.at(job.next);
    if (!job.device->beginSheet(sheet, job.next, &error)) {
        job.device->abort();
        finishJob(QStringLiteral("failed"), error, job.next);
        return;
    }
    Print::paintSheet(job.device->painter(), *job.layout, sheet, job.images.get(), false);
    ++job.next;
    emit jobChanged();
    QTimer::singleShot(0, this, &PrintAdapter::stepJob);
}

void PrintAdapter::finishJob(const QString &status, const QString &error, int failedSheet)
{
    Job &current = *m_job;
    QVariantMap map = PrintAdapter::job();
    map.insert(QStringLiteral("status"), status);
    map.insert(QStringLiteral("error"), error);
    if (status != QLatin1String("completed")) {
        // What did not reach paper or disk: every sheet after those the device
        // delivered irrevocably (none for devices that commit at finish).
        const int delivered = current.device ? qBound(0, current.device->deliveredSheets(), int(current.sheets.size())) : 0;
        QVector<int> pages;
        for (int index = delivered; index < current.sheets.size(); ++index) pages.append(current.sheets.at(index).sourcePage);
        if (!pages.isEmpty()) {
            map.insert(QStringLiteral("affectedStart"), *std::min_element(pages.cbegin(), pages.cend()) + 1);
            map.insert(QStringLiteral("affectedEnd"), *std::max_element(pages.cbegin(), pages.cend()) + 1);
        }
        map.insert(QStringLiteral("affectedPages"), PrintDetail::pagesText(pages));
        if (failedSheet >= 0 && failedSheet < current.sheets.size())
            map.insert(QStringLiteral("failedPage"), current.sheets.at(failedSheet).sourcePage + 1);
    }
    if (status == QLatin1String("failed")) setError(ErrorCode::BackendFailure, error);
    m_lastJob = map;
    const QString id = current.id;
    m_job.reset(); // releases the device, the snapshot and its layout; the ticket stays
    emit jobChanged();
    emit jobFinished(id, status);
}

} // namespace QindaTK
