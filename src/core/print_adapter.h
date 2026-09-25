// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_preflight.h"
#include "rich_text_document.h" // complete type for the QML document property
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {
class RichTextImageRenderer;
class RichTextLayout;
namespace Print { class PreviewRenderer; }

// Printing and publishing preflight for a RichTextDocument (WP-053,
// docs/printing.md): destinations and their capabilities, one print ticket,
// the output sheets, a bounded preview cache, print jobs and preflight.
// AGENT-CONTRACT (D-130/D-131): preview images, jobs and exportPdf share
// Print::sheetFor/paintSheet; PrintPreview, PrintSheetView and PreflightPanel
// only draw these properties and send requests back to this object.
class PrintAdapter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::RichTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QVariantList destinations READ destinations NOTIFY destinationsChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY settingsChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY outputChanged)
    Q_PROPERTY(int sheetCount READ sheetCount NOTIFY outputChanged)
    Q_PROPERTY(QVariantList sheets READ sheets NOTIFY outputChanged)
    Q_PROPERTY(QVariantMap job READ job NOTIFY jobChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobChanged)
    Q_PROPERTY(QVariantList issues READ issues NOTIFY issuesChanged)
    Q_PROPERTY(bool preflightStale READ preflightStale NOTIFY issuesChanged)
    Q_PROPERTY(int blockingIssueCount READ blockingIssueCount NOTIFY issuesChanged)
    Q_PROPERTY(QString profileId READ profileId WRITE setProfileId NOTIFY issuesChanged)
    Q_PROPERTY(QVariantList profiles READ profileList CONSTANT)
    Q_PROPERTY(int previewCacheLimit READ previewCacheLimit WRITE setPreviewCacheLimit NOTIFY previewChanged)
    Q_PROPERTY(int previewCacheCount READ previewCacheCount NOTIFY previewChanged)
    Q_PROPERTY(qint64 previewCacheBytes READ previewCacheBytes NOTIFY previewChanged)
    Q_PROPERTY(int previewRenderCount READ previewRenderCount NOTIFY previewChanged)
    Q_PROPERTY(int previewPendingCount READ previewPendingCount NOTIFY previewChanged)
    Q_PROPERTY(int previewGeneration READ previewGeneration NOTIFY previewInvalidated)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QString lastErrorCode READ lastErrorCode NOTIFY errorChanged)
public:
    static constexpr qint64 PreviewCacheByteLimit = 64ll * 1024 * 1024;
    // AGENT-GUARD: one preview image is at most a quarter of the cache, so
    // the byte limit holds even for the newest image; larger requests are
    // rendered at a lower zoom (Print::previewZoom) and scaled by the view.
    static constexpr qint64 PreviewImageByteLimit = PreviewCacheByteLimit / 4;

    explicit PrintAdapter(QObject *parent = nullptr);
    ~PrintAdapter() override;

    // Backends every new adapter starts with, after the built-in PDF writer
    // (for example NativePrintBackend, registered once in main()).
    static void registerDefaultBackend(Print::BackendPtr backend);
    static void clearDefaultBackends();
    // Replaces this adapter's backends (the PDF writer is not added).
    void setBackends(QVector<Print::BackendPtr> backends);

    RichTextDocument *document() const;
    void setDocument(RichTextDocument *document);
    QVariantList destinations() const;
    QVariantMap settings();
    QVariantMap capabilities() const { return Print::capabilitiesToMap(destination()); }
    int pageCount();
    int sheetCount() { return int(plan().size()); }
    QVariantList sheets();
    QVariantMap job() const;
    bool busy() const;
    QVariantList issues() const;
    bool preflightStale() const { return m_preflightStale; }
    int blockingIssueCount() const;
    QString profileId() const { return m_profile.id; }
    void setProfileId(const QString &id);
    QVariantList profileList() const;
    int previewCacheLimit() const { return m_cacheLimit; }
    void setPreviewCacheLimit(int pages);
    int previewCacheCount() const { return int(m_cache.size()); }
    qint64 previewCacheBytes() const { return m_cacheBytes; }
    int previewRenderCount() const { return m_renderCount; }
    int previewPendingCount() const { return int(m_pending.size()); }
    int previewGeneration() const { return m_generation; }
    QString lastError() const { return m_lastError; }
    QString lastErrorCode() const { return m_lastErrorCode; }

    Print::Ticket ticket() const { return m_ticket; }
    Print::Capabilities destination() const;
    QVector<Print::Sheet> plan();
    // The sheet a layout page is output on with the current ticket.
    Print::Sheet sheetOf(int pageIndex);
    // The zoom a preview of `pageIndex` is rendered at: `zoom`, lowered until
    // the image fits PreviewImageByteLimit.
    double previewZoomFor(int pageIndex, double zoom);
    // Preview of `pageIndex` at `zoom` (1: 96 px per inch) from the cache,
    // rendered on the calling thread when missing. Blocking: for tests and
    // tools; views use requestPreview/cachedPreview.
    QImage previewImage(int pageIndex, double zoom);
    // The cached preview, or a null image; never renders.
    QImage cachedPreview(int pageIndex, double zoom);
    // True when the preview is cached. Otherwise queues it on the preview
    // thread and returns false; previewReady(pageIndex) follows.
    Q_INVOKABLE bool requestPreview(int pageIndex, double zoom);

    Q_INVOKABLE void refreshDestinations();
    // Merges `patch` into the ticket; malformed values change nothing. Options
    // the destination lacks are accepted here and reported by preflight.
    Q_INVOKABLE bool applySettings(const QVariantMap &patch);
    Q_INVOKABLE QVariantMap sheetForPage(int pageIndex);
    // Starts a job for the current ticket (after applying `patch`); returns its
    // ID, or "" when the ticket is refused (lastError names the field).
    Q_INVOKABLE QString print(const QVariantMap &patch = {});
    Q_INVOKABLE bool cancel(const QString &jobId);
    Q_INVOKABLE QVariantList runPreflight();
    Q_INVOKABLE QVariantMap issue(const QString &issueId) const;
    // Applies a repair and re-runs preflight; a repair that leaves the issue
    // in place is undone and returns false (D-132).
    Q_INVOKABLE bool repair(const QString &issueId, const QString &actionId, const QVariant &argument = {});

signals:
    void documentChanged();
    void destinationsChanged();
    void settingsChanged();
    void outputChanged();
    void jobChanged();
    void jobFinished(const QString &jobId, const QString &status);
    void issuesChanged();
    void previewChanged();          // cache statistics
    void previewInvalidated();      // pages must be fetched again (document or ticket changed)
    void previewReady(int pageIndex); // a requested preview is in the cache
    void errorChanged();

private:
    struct Job;
    struct CachedImage { QImage image; quint64 lastUse = 0; };
    RichTextLayout *layout();
    void onDocumentChanged();
    void invalidateOutput();
    void setError(ErrorCode code, const QString &message);
    void clearError();
    void stepJob();
    void finishJob(const QString &status, const QString &error, int failedSheet);
    void trimCache();
    QString previewKey(int pageIndex, double zoom) const;
    void cachePreview(const QString &key, const QImage &image);
    void onPreviewRendered(int generation, const QString &key, const QImage &image);
    bool applyRepair(const Print::Issue &issue, const QString &actionId, const QVariant &argument);
    bool replaceFont(const QString &family);
    bool relinkImage(const QString &objectId, const QVariant &argument);

    QPointer<RichTextDocument> m_document;
    QMetaObject::Connection m_documentChanged;
    QMetaObject::Connection m_documentDestroyed;
    std::unique_ptr<RichTextLayout> m_layout;
    std::unique_ptr<RichTextImageRenderer> m_images;
    QVector<Print::BackendPtr> m_backends;
    QVector<QPair<Print::Capabilities, Print::BackendPtr>> m_destinations;
    Print::Ticket m_ticket;
    Print::Profile m_profile;
    QVector<Print::Issue> m_issues;
    bool m_preflightStale = true;
    QHash<QString, CachedImage> m_cache;
    qint64 m_cacheBytes = 0;
    int m_cacheLimit = 8;
    quint64 m_cacheClock = 0;
    int m_renderCount = 0;
    QHash<QString, int> m_pending;   // preview key -> page, queued on the preview thread
    std::unique_ptr<Print::PreviewRenderer> m_renderer;
    int m_generation = 0;
    std::unique_ptr<Job> m_job;
    QVariantMap m_lastJob;
    int m_jobCounter = 0;
    QString m_lastError;
    QString m_lastErrorCode;
};

} // namespace QindaTK
