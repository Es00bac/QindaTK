// SPDX-License-Identifier: LGPL-3.0-or-later
#include "print_adapter_p.h"

#include <algorithm>

namespace QindaTK {
namespace {

QVector<Print::BackendPtr> &defaultBackends()
{
    static QVector<Print::BackendPtr> backends;
    return backends;
}

} // namespace

namespace PrintDetail {

QString errorCodeName(ErrorCode code)
{
    static const char *const names[] = {"", "invalidArgument", "unsupported", "unavailable", "cancelled",
                                        "staleRevision", "staleGeneration", "validation", "backendFailure", "conflict"};
    return QLatin1String(names[qBound(0, int(code), 9)]);
}

QVariantMap idleJob()
{
    return {{QStringLiteral("id"), QString()}, {QStringLiteral("status"), QStringLiteral("idle")},
            {QStringLiteral("progress"), 0.0}, {QStringLiteral("error"), QString()},
            {QStringLiteral("printerId"), QString()}, {QStringLiteral("outputPath"), QString()},
            {QStringLiteral("sheetCount"), 0}, {QStringLiteral("sheetsDone"), 0},
            {QStringLiteral("affectedStart"), 0}, {QStringLiteral("affectedEnd"), 0},
            {QStringLiteral("affectedPages"), QString()}, {QStringLiteral("failedPage"), 0}};
}

QString pagesText(const QVector<int> &pages)
{
    QVector<Print::PageRange> ranges;
    for (int page : pages) {
        if (!ranges.isEmpty() && ranges.last().last == page) ranges.last().last = page + 1;
        else ranges.append({page + 1, page + 1});
    }
    return Print::formatPageRanges(ranges);
}

} // namespace PrintDetail

PrintAdapter::PrintAdapter(QObject *parent)
    : QObject(parent), m_images(std::make_unique<RichTextImageRenderer>()), m_profile(Print::profile({}))
{
    m_backends = {std::make_shared<Print::PdfFileBackend>()};
    m_backends += defaultBackends();
    m_lastJob = PrintDetail::idleJob();
    refreshDestinations();
}

PrintAdapter::~PrintAdapter() = default; // a running job's device aborts in ~Job

void PrintAdapter::registerDefaultBackend(Print::BackendPtr backend)
{
    if (backend) defaultBackends().append(std::move(backend));
}

void PrintAdapter::clearDefaultBackends()
{
    defaultBackends().clear();
}

void PrintAdapter::setBackends(QVector<Print::BackendPtr> backends)
{
    m_backends = std::move(backends);
    refreshDestinations();
}

void PrintAdapter::refreshDestinations()
{
    m_destinations.clear();
    for (const Print::BackendPtr &backend : std::as_const(m_backends))
        for (const Print::Capabilities &destination : backend->destinations())
            m_destinations.append({destination, backend});
    emit destinationsChanged();
    emit settingsChanged();
    invalidateOutput();
}

QVariantList PrintAdapter::destinations() const
{
    QVariantList list;
    for (const auto &entry : m_destinations) list.append(Print::capabilitiesToMap(entry.first));
    return list;
}

Print::Capabilities PrintAdapter::destination() const
{
    for (const auto &entry : m_destinations)
        if (entry.first.printerId == m_ticket.printerId) return entry.first;
    // AGENT-NOTE: an unknown or vanished destination stays selected and is
    // reported unavailable, so a job never silently goes somewhere else.
    Print::Capabilities missing;
    missing.printerId = m_ticket.printerId;
    missing.label = m_ticket.printerId;
    missing.available = false;
    missing.status = tr("not found");
    return missing;
}

RichTextDocument *PrintAdapter::document() const
{
    return m_document;
}

void PrintAdapter::setDocument(RichTextDocument *document)
{
    if (m_document == document) return;
    disconnect(m_documentChanged);
    disconnect(m_documentDestroyed);
    m_layout.reset();
    m_document = document;
    if (document) {
        m_documentChanged = connect(document, &RichTextDocument::changed, this, &PrintAdapter::onDocumentChanged);
        m_documentDestroyed = connect(document, &QObject::destroyed, this, [this] {
            m_layout.reset();
            invalidateOutput();
            emit documentChanged();
        });
    }
    emit documentChanged();
    invalidateOutput();
}

RichTextLayout *PrintAdapter::layout()
{
    if (!m_document) return nullptr;
    if (!m_layout) {
        m_layout = std::make_unique<RichTextLayout>();
        m_layout->setMode(RichTextLayout::Mode::Paged);
        m_layout->setDocument(m_document);
    }
    // AGENT-GUARD: inside an open transaction the model has steps the layout
    // has not seen yet (contentChanged comes at commit); paginating then
    // would walk stale pages. The commit's changed() refreshes everything.
    if (!m_document->inTransaction()) m_layout->paginateAll();
    return m_layout.get();
}

void PrintAdapter::onDocumentChanged()
{
    // changed() also fires for refusals inside an open transaction; the
    // commit (or cancel) emits it again once the layout has the change.
    if (m_document && m_document->inTransaction()) return;
    invalidateOutput();
    emit settingsChanged(); // the whole-document page range follows the page count
}

void PrintAdapter::invalidateOutput()
{
    m_cache.clear();
    m_cacheBytes = 0;
    ++m_generation;
    m_preflightStale = true;
    emit outputChanged();
    emit previewChanged();
    emit previewInvalidated();
    emit issuesChanged();
}

QVariantMap PrintAdapter::settings()
{
    return Print::ticketToMap(m_ticket, pageCount());
}

int PrintAdapter::pageCount()
{
    RichTextLayout *pages = layout();
    return pages ? pages->pageCount() : 0;
}

QVector<Print::Sheet> PrintAdapter::plan()
{
    RichTextLayout *pages = layout();
    if (!pages) return {};
    return Print::planSheets(m_document->pageSetupValue(), pages->pageCount(), m_ticket,
                             Print::resolvePaper(destination(), m_ticket.paperId));
}

Print::Sheet PrintAdapter::sheetOf(int pageIndex)
{
    const RichText::PageSetup page = m_document ? m_document->pageSetupValue() : RichText::PageSetup{};
    return Print::sheetFor(page, pageIndex, m_ticket, Print::resolvePaper(destination(), m_ticket.paperId));
}

QVariantList PrintAdapter::sheets()
{
    QVariantList list;
    for (const Print::Sheet &sheet : plan()) list.append(Print::sheetToMap(sheet));
    return list;
}

QVariantMap PrintAdapter::sheetForPage(int pageIndex)
{
    const int count = pageCount();
    if (pageIndex < 0 || pageIndex >= count) return {};
    QVariantMap map = Print::sheetToMap(sheetOf(pageIndex));
    map.insert(QStringLiteral("selected"), Print::selectedPages(m_ticket.ranges, count).contains(pageIndex));
    return map;
}

bool PrintAdapter::applySettings(const QVariantMap &patch)
{
    clearError();
    OperationError error;
    const std::optional<Print::Ticket> next = Print::ticketFromMap(patch, m_ticket, &error);
    if (!next) {
        setError(error.code, error.message);
        return false;
    }
    m_ticket = *next;
    emit settingsChanged();
    invalidateOutput();
    return true;
}

QImage PrintAdapter::previewImage(int pageIndex, double zoom)
{
    RichTextLayout *pages = layout();
    if (!pages || pageIndex < 0 || pageIndex >= pages->pageCount() || !(zoom > 0) || zoom > 8) return {};
    const QString key = QStringLiteral("%1@%2").arg(pageIndex).arg(zoom, 0, 'g', 6);
    const auto found = m_cache.find(key);
    if (found != m_cache.end()) {
        found->lastUse = ++m_cacheClock;
        return found->image;
    }
    const QImage image = Print::renderSheet(*pages, sheetOf(pageIndex), zoom,
                                            m_ticket.colorMode == QLatin1String("grayscale"), m_images.get());
    ++m_renderCount;
    if (!image.isNull()) {
        m_cache.insert(key, {image, ++m_cacheClock});
        m_cacheBytes += image.sizeInBytes();
        trimCache();
    }
    emit previewChanged();
    return image;
}

void PrintAdapter::setPreviewCacheLimit(int pages)
{
    pages = qBound(1, pages, 256);
    if (pages == m_cacheLimit) return;
    m_cacheLimit = pages;
    trimCache();
    emit previewChanged();
}

void PrintAdapter::trimCache()
{
    // AGENT-GUARD: the cache is bounded by page count and bytes; the image
    // just rendered has the newest use stamp, so it is evicted last.
    while (m_cache.size() > m_cacheLimit || (m_cacheBytes > PreviewCacheByteLimit && m_cache.size() > 1)) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->lastUse < oldest->lastUse) oldest = it;
        m_cacheBytes -= oldest->image.sizeInBytes();
        m_cache.erase(oldest);
    }
}

QVariantList PrintAdapter::runPreflight()
{
    clearError();
    m_issues.clear();
    // A fresh decoder per run: an image file fixed on disk is read again.
    m_images = std::make_unique<RichTextImageRenderer>();
    if (RichTextLayout *pages = layout()) {
        Print::PreflightInput input;
        input.document = m_document;
        input.layout = pages;
        input.images = m_images.get();
        input.ticket = m_ticket;
        input.destination = destination();
        input.profile = m_profile;
        m_issues = Print::preflight(input);
    }
    m_preflightStale = false;
    emit issuesChanged();
    return issues();
}

QVariantList PrintAdapter::issues() const
{
    QVariantList list;
    for (const Print::Issue &issue : m_issues) list.append(issue.toMap());
    return list;
}

QVariantMap PrintAdapter::issue(const QString &issueId) const
{
    for (const Print::Issue &issue : m_issues)
        if (issue.id == issueId) return issue.toMap();
    return {};
}

int PrintAdapter::blockingIssueCount() const
{
    return int(std::count_if(m_issues.cbegin(), m_issues.cend(), Print::blocksOutput));
}

void PrintAdapter::setProfileId(const QString &id)
{
    if (id == m_profile.id) return;
    m_profile = Print::profile(id);
    m_preflightStale = true;
    emit issuesChanged();
}

QVariantList PrintAdapter::profileList() const
{
    QVariantList list;
    for (const Print::Profile &profile : Print::profiles())
        list.append(QVariantMap{{QStringLiteral("id"), profile.id}, {QStringLiteral("label"), profile.label},
                                {QStringLiteral("minimumDpi"), profile.minimumDpi}, {QStringLiteral("strict"), profile.strict}});
    return list;
}

void PrintAdapter::setError(ErrorCode code, const QString &message)
{
    m_lastError = message;
    m_lastErrorCode = PrintDetail::errorCodeName(code);
    emit errorChanged();
}

void PrintAdapter::clearError()
{
    if (m_lastError.isEmpty() && m_lastErrorCode.isEmpty()) return;
    m_lastError.clear();
    m_lastErrorCode.clear();
    emit errorChanged();
}

} // namespace QindaTK
