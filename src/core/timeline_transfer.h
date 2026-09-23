// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "clipboard_service.h"
#include "drag_drop_service.h"
#include "timeline_model.h"

#include <QHash>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: timeline clips cross the WP-016 transfer boundary as
// TransferPayloadCodec object items (internal MIME only) with schema
// "qindatk.timeline.clip/1": the clip record, its start offset from the
// earliest copied clip and its visible-track offset, in exact decimal ticks of
// the source timebase. Pasting or dropping is one InsertClips transaction with
// fresh clip IDs. A different timebase is refused rather than rounded, and
// clips from another document that name media resources are refused unless
// the host has confirmed those resources resolve (allowForeignResources).
namespace TimelineTransfer {
inline constexpr const char *ClipSchema = "qindatk.timeline.clip/1";
TransferPayload payloadFor(const TimelineModel &model, const QVector<QUuid> &clipIds,
                           OperationError *error);
std::optional<TimelineEditRequest> insertRequest(const TimelineModel &model,
                                                 const TransferPayload &payload,
                                                 const QUuid &trackId, bool allowForeignResources,
                                                 OperationError *error);
} // namespace TimelineTransfer

class TimelineClipboard : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY changed)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY changed)
    Q_PROPERTY(QString sourceDocumentId READ sourceDocumentId NOTIFY changed)
    Q_PROPERTY(bool allowForeignResources READ allowForeignResources WRITE setAllowForeignResources NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit TimelineClipboard(QObject *parent = nullptr);
    ~TimelineClipboard() override;
    // Tests and embedders inject an adapter; the default is the system clipboard.
    void setAdapter(ClipboardAdapter *adapter);
    bool canPaste() const { return m_clipCount > 0; }
    int clipCount() const { return m_clipCount; }
    QString sourceDocumentId() const { return m_sourceDocument; }
    bool allowForeignResources() const { return m_allowForeign; }
    void setAllowForeignResources(bool allow);
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool copyClips(QindaTK::TimelineModel *model, const QStringList &clipIds);
    Q_INVOKABLE QVariantMap paste(QindaTK::TimelineModel *model, const QString &trackId,
                                  const QString &atTicks);
    Q_INVOKABLE void refresh();

signals:
    void changed();
    void rejected(const QString &reason);

private:
    std::optional<TransferPayload> current(OperationError *error) const;
    bool fail(const QString &message);

    std::unique_ptr<ClipboardAdapter> m_ownedAdapter;
    QPointer<ClipboardAdapter> m_adapter;
    int m_clipCount = 0;
    QString m_sourceDocument;
    bool m_allowForeign = false;
    QString m_lastError;
};

// WP-016 DragDropService endpoint for one timeline view. Copy only: a preview
// plans the InsertClips edit without touching the model; commit re-checks the
// snapshot captured by the preview and applies one transaction.
class TimelineDropTarget final : public DropTarget {
public:
    TimelineDropTarget(TimelineModel *model, QString targetId, QString viewId,
                       QObject *parent = nullptr);
    QString stableTargetId() const override { return m_targetId; }
    QString viewId() const override { return m_viewId; }
    quint64 viewGeneration() const override { return m_viewGeneration; }
    void setViewGeneration(quint64 generation) { m_viewGeneration = generation; }
    void setAllowForeignResources(bool allow) { m_allowForeign = allow; }
    SnapshotHeader snapshot() const override;
    // location: {trackId: UUID string, ticks: decimal string}
    DropPreview previewDrop(const QString &sessionId, const TransferPayload &payload,
                            TransferOperation operation, const QVariantMap &location) override;
    OperationError commitDrop(const QString &sessionId, const QString &previewToken) override;
    void cancelDrop(const QString &sessionId, const QString &previewToken) override;

private:
    struct Plan {
        SnapshotHeader snapshot;
        TimelineEditRequest request;
        TimelineEditInput input;
    };
    QPointer<TimelineModel> m_model;
    QString m_targetId;
    QString m_viewId;
    quint64 m_viewGeneration = 0;
    bool m_allowForeign = false;
    QHash<QString, Plan> m_plans;
};

} // namespace QindaTK
