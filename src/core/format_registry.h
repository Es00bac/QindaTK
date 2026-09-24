// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_manager.h"
#include "job_model.h"
#include "reference_document_codec.h"
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {

struct FormatPlan {
    QVariantMap options;
    QVariantList warnings;
    QVariantList losses;
    OperationError error;
    QVariantMap preview;
};
struct FormatPayload {
    QByteArray bytes;
    QVariantList warnings;
    QVariantList losses;
    OperationError error;
};

// Providers operate only on immutable bytes/snapshots. A provider advertises
// each supported operation separately; render-only is never inferred as edit.
class FormatProvider {
public:
    virtual ~FormatProvider() = default;
    virtual QVariantMap metadata() const = 0;
    virtual bool recognizes(const QByteArray &prefix, const QUrl &url) const = 0;
    virtual FormatPlan preflightImport(const QByteArray &bytes,
                                       const QVariantMap &options) const = 0;
    virtual FormatPayload importDocument(const QByteArray &bytes,
                                         const QVariantMap &options,
                                         const CancellationToken &cancel,
                                         const JobProgress &progress) const = 0;
    virtual FormatPlan preflightExport(const SnapshotPtr &snapshot,
                                       const QVariantMap &options) const = 0;
    virtual FormatPayload exportDocument(const SnapshotPtr &snapshot,
                                         const QVariantMap &options,
                                         const CancellationToken &cancel,
                                         const JobProgress &progress) const = 0;
};
using FormatProviderPtr = std::shared_ptr<FormatProvider>;

class NativeReferenceFormat final : public FormatProvider {
public:
    QVariantMap metadata() const override;
    bool recognizes(const QByteArray &prefix, const QUrl &url) const override;
    FormatPlan preflightImport(const QByteArray &bytes, const QVariantMap &options) const override;
    FormatPayload importDocument(const QByteArray &bytes, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
    FormatPlan preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const override;
    FormatPayload exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
};
class PlainTextFormat final : public FormatProvider {
public:
    QVariantMap metadata() const override;
    bool recognizes(const QByteArray &prefix, const QUrl &url) const override;
    FormatPlan preflightImport(const QByteArray &bytes, const QVariantMap &options) const override;
    FormatPayload importDocument(const QByteArray &bytes, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
    FormatPlan preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const override;
    FormatPayload exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
};

// The canvas native file (CanvasDocumentFormat): validated import, export and
// roundtrip of `qindatk.canvas-document.v1` sessions only. Opt-in: an
// application with canvas documents registers it, so reference-document
// dialogs never list a format that cannot apply to their documents.
class CanvasNativeFormat final : public FormatProvider {
public:
    QVariantMap metadata() const override;
    bool recognizes(const QByteArray &prefix, const QUrl &url) const override;
    FormatPlan preflightImport(const QByteArray &bytes, const QVariantMap &options) const override;
    FormatPayload importDocument(const QByteArray &bytes, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
    FormatPlan preflightExport(const SnapshotPtr &snapshot, const QVariantMap &options) const override;
    FormatPayload exportDocument(const SnapshotPtr &snapshot, const QVariantMap &options,
                                 const CancellationToken &cancel,
                                 const JobProgress &progress) const override;
};

// GUI-thread broker for operation-specific providers and cancellable jobs.
// Import jobs yield native bytes of a registered document schema; adoptImport
// creates the live DocumentSession only after a successful job and explicit
// caller decision. A provider's `schemas` metadata lists the document
// schemas it can export; any other snapshot is refused before it runs.
class FormatRegistry : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList formats READ formats NOTIFY formatsChanged)
    Q_PROPERTY(QindaTK::JobModel *jobs READ jobs CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit FormatRegistry(QObject *parent = nullptr);
    JobModel *jobs() const { return m_jobs; }
    QString lastError() const { return m_lastError; }
    QVariantList formats() const;
    bool registerProvider(FormatProviderPtr provider);
    Q_INVOKABLE QVariantList formatsForOperation(const QString &operation) const;
    Q_INVOKABLE QString detectImport(const QUrl &source) const;
    Q_INVOKABLE QVariantMap preflightImport(const QUrl &source,
                                            const QString &formatId = {},
                                            const QVariantMap &options = {}) const;
    Q_INVOKABLE QVariantMap preflightExport(PropertyDocument *document,
                                            const QUrl &destination,
                                            const QString &formatId,
                                            const QVariantMap &options = {}) const;
    Q_INVOKABLE QVariantMap preflightExportSession(DocumentSession *session,
                                                   const QUrl &destination,
                                                   const QString &formatId,
                                                   const QVariantMap &options = {}) const;
    Q_INVOKABLE QString startImport(const QUrl &source, const QString &formatId = {},
                                    const QVariantMap &options = {});
    Q_INVOKABLE QString startExport(PropertyDocument *document, const QUrl &destination,
                                    const QString &formatId, const QVariantMap &options = {});
    Q_INVOKABLE QString startExportSession(DocumentSession *session, const QUrl &destination,
                                           const QString &formatId, const QVariantMap &options = {});
    Q_INVOKABLE DocumentSession *adoptImport(DocumentManager *manager, const QString &jobId);
    // Export of any snapshot (docs/format-registry.md#snapshot-sources): a QObject
    // that is a SnapshotSource (such as Tk.Workbook) from QML, or a snapshot.
    Q_INVOKABLE QVariantMap preflightExportSource(QObject *source, const QUrl &destination,
                                                  const QString &formatId,
                                                  const QVariantMap &options = {}) const;
    Q_INVOKABLE QString startExportSource(QObject *source, const QUrl &destination,
                                          const QString &formatId, const QVariantMap &options = {});
    QVariantMap preflightSnapshot(const SnapshotPtr &snapshot, const QUrl &destination,
                                  const QString &formatId, const QVariantMap &options) const;
    QString startExportSnapshot(const SnapshotPtr &snapshot, const QUrl &destination,
                                const QString &formatId, const QVariantMap &options);
signals:
    void formatsChanged();
    void changed();
private:
    FormatProviderPtr provider(const QString &id) const;
    bool fail(const QString &message);
    QVector<FormatProviderPtr> m_providers;
    JobModel *m_jobs = nullptr;
    QString m_lastError;
};
} // namespace QindaTK
