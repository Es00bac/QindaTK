// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "media_decode_worker.h"
#include "motion_composition.h"
#include "motion_frame_renderer.h"
#include "node_graph_model.h"
#include "resource_resolver.h"

#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <optional>

namespace QindaTK {

// AGENT-CONTRACT (docs/media-playback.md "Frame feeds"): resolves the media
// behind MotionComposition layer sourceIds and NodeGraphModel "source" nodes
// and hands them decoded frames. prepare(ticks) evaluates the composition,
// decodes each visible media layer at its exact localTicks (and each graph
// source at `ticks`) on its own decode thread, then sets every source in one
// GUI-thread step and emits prepared(); the host renders after that signal.
// A newer prepare() cancels the older request, whose results never apply.
// Offline media removes the source (renderer.removeSource / graph
// clearSourceImage) so renders report it missing instead of stale pixels.
// A relinked resource re-prepares the last prepared time.
class MediaFrameFeed : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::ResourceResolver *resolver READ resolver WRITE setResolver NOTIFY bindingsChanged)
    Q_PROPERTY(QindaTK::MotionComposition *composition READ composition WRITE setComposition NOTIFY bindingsChanged)
    Q_PROPERTY(QindaTK::MotionFrameRenderer *renderer READ renderer WRITE setRenderer NOTIFY bindingsChanged)
    Q_PROPERTY(QindaTK::NodeGraphModel *graph READ graph WRITE setGraph NOTIFY bindingsChanged)
    Q_PROPERTY(QVariantMap sourceResources READ sourceResources WRITE setSourceResources NOTIFY bindingsChanged)
    Q_PROPERTY(QString ticksPerSecondNumerator READ ticksPerSecondNumerator WRITE setTicksPerSecondNumerator NOTIFY bindingsChanged)
    Q_PROPERTY(QString ticksPerSecondDenominator READ ticksPerSecondDenominator WRITE setTicksPerSecondDenominator NOTIFY bindingsChanged)
    Q_PROPERTY(int maxSourceDimension READ maxSourceDimension WRITE setMaxSourceDimension NOTIFY bindingsChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged)
    Q_PROPERTY(QString preparedTicks READ preparedTicks NOTIFY stateChanged)
    Q_PROPERTY(QStringList missingResourceIds READ missingResourceIds NOTIFY stateChanged)
    Q_PROPERTY(QVariantList fedSources READ fedSources NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
public:
    explicit MediaFrameFeed(QObject *parent = nullptr);
    ~MediaFrameFeed() override;

    ResourceResolver *resolver() const { return m_resolver; }
    void setResolver(ResourceResolver *resolver);
    MotionComposition *composition() const { return m_composition; }
    void setComposition(MotionComposition *composition);
    MotionFrameRenderer *renderer() const { return m_renderer; }
    void setRenderer(MotionFrameRenderer *renderer);
    NodeGraphModel *graph() const { return m_graph; }
    void setGraph(NodeGraphModel *graph);
    // sourceId -> resourceId. An unmapped sourceId that the resolver knows is
    // itself a resource ID; any other sourceId stays host-supplied pixels.
    QVariantMap sourceResources() const { return m_sourceResources; }
    void setSourceResources(const QVariantMap &map);
    QString ticksPerSecondNumerator() const { return QString::number(m_rate.numerator); }
    void setTicksPerSecondNumerator(const QString &value);
    QString ticksPerSecondDenominator() const { return QString::number(m_rate.denominator); }
    void setTicksPerSecondDenominator(const QString &value);
    int maxSourceDimension() const { return m_maxDimension; }
    void setMaxSourceDimension(int pixels);
    bool pending() const { return m_request.has_value(); }
    QString preparedTicks() const { return m_preparedTicks; }
    QStringList missingResourceIds() const { return m_missing; }
    QVariantList fedSources() const { return m_fed; }
    QString lastError() const { return m_error; }

    // Returns the request ID, or "" (lastError) for invalid ticks or a
    // composition that fails to evaluate. prepared() follows on a later turn.
    Q_INVOKABLE QString prepare(const QString &timeTicks);
    Q_INVOKABLE void cancel();

signals:
    void bindingsChanged();
    void stateChanged();
    void prepared(const QString &requestId, const QString &timeTicks);
    void failed(const QString &requestId, const QString &error);

private:
    struct Target {
        bool node = false; // false: composition layer
        QString sourceId;
        QString nodeId;
        QString resourceId;
        QString uri;
        qint64 ticks = 0;
        quint64 job = 0; // 0 when the resource is offline
    };
    struct Request {
        QString id;
        QString timeTicks;
        QVector<Target> targets;
        QHash<quint64, MediaWorkerResult> results;
        QHash<quint64, CancellationSource> jobs;
        int remaining = 0;
    };
    QString resourceFor(const QString &sourceId) const;
    QString onlineUri(const QString &resourceId) const;
    void onResult(MediaWorkerResult result);
    void apply();
    void onResourceChanged(const QString &resourceId);

    QPointer<ResourceResolver> m_resolver;
    QPointer<MotionComposition> m_composition;
    QPointer<MotionFrameRenderer> m_renderer;
    QPointer<NodeGraphModel> m_graph;
    QVariantMap m_sourceResources;
    TimelineRate m_rate{TimelineTime::kDefaultTicksPerSecond, 1};
    int m_maxDimension = 2048;
    std::optional<Request> m_request;
    QString m_preparedTicks;
    QStringList m_preparedResources;
    QStringList m_missing;
    QVariantList m_fed;
    QString m_error;
    quint64 m_serial = 0;
    // AGENT-GUARD: last member, destroyed first; its thread posts results here.
    std::unique_ptr<MediaDecodeWorker> m_worker;
};

} // namespace QindaTK
