// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <atomic>
#include <memory>

Q_MOC_INCLUDE("document_resource_links.h")

namespace QindaTK {

class DocumentResourceLinks;

// Stable-ID asset graph. Sources own their outgoing edges; a rescan replaces
// only that source's edges. Analysis tokens stop older workers overwriting a
// newer result. The host owns rebuilds and URI relinks.
//
// AGENT-CONTRACT (WP-087): an edge records where it was written (`kind`,
// `line`, `reference`). relinkPlan() turns that into the text edits a host
// applies; the index never rewrites source files. Both sides:
// asset_dependency_scan.cpp (plan) and the host's document transaction.
class AssetDependencyIndex : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString projectRoot READ projectRoot WRITE setProjectRoot NOTIFY changed)
    Q_PROPERTY(QVariantList assets READ assets NOTIFY changed)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(int assetCount READ assetCount NOTIFY changed)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
public:
    // One reference as extracted from a source file.
    struct Edge {
        QString kind;       // "import", "reference" or "host"
        int line = 0;       // 1-based; 0 when the host supplied the edge
        QString reference;  // the text as written in the source
    };
    struct ExtractedEdge {
        QString targetId;
        Edge edge;
        bool exists = false;
    };
    struct Extraction {
        QString error;
        bool changedDuringRead = false;
        QList<ExtractedEdge> edges;
    };

    explicit AssetDependencyIndex(QObject *parent = nullptr) : QObject(parent) {}
    ~AssetDependencyIndex() override;
    QString projectRoot() const { return m_projectRoot; }
    void setProjectRoot(const QString &path);
    QVariantList assets() const;
    QVariantList diagnostics() const;
    int assetCount() const { return m_assets.size(); }
    int edgeCount() const { return m_edgeCount; }
    QString lastError() const { return m_error; }
    bool scanning() const { return m_scanCancel != nullptr; }

    Q_INVOKABLE bool upsertAsset(const QString &id, const QString &label,
                                const QString &uri, bool available,
                                const QString &kind = QStringLiteral("asset"));
    Q_INVOKABLE bool setAvailable(const QString &id, bool available);
    Q_INVOKABLE QString beginScan(const QString &sourceId);
    Q_INVOKABLE bool commitScan(const QString &sourceId, const QString &token,
                                const QStringList &dependencies);
    // edges: [{targetId, kind, line, reference}]; kind defaults to "host".
    Q_INVOKABLE bool commitScanEdges(const QString &sourceId, const QString &token,
                                     const QVariantList &edges);
    Q_INVOKABLE bool scanQmlFile(const QString &sourceId);
    // Any scannable source: QML, or SVG/HTML/CSS markup (extractMarkup in
    // asset_dependency_qml.cpp).
    Q_INVOKABLE bool scanFile(const QString &sourceId);
    // Render queue rows (RenderQueueModel::jobs): each output depends on its
    // source and on the outputs of the jobs it waits for. Returns the number
    // of outputs indexed, or -1 with lastError set.
    Q_INVOKABLE int indexRenderJobs(const QVariantList &jobs);
    // A document's linked resources (DocumentResourceLinks manifest, resolved
    // through its ResourceResolver): the document depends on each logical
    // resource ID, which is missing when the resolver says so. Returns the
    // number of resources indexed, or -1 with lastError set.
    Q_INVOKABLE int indexDocumentResources(const QString &documentId,
                                           QindaTK::DocumentResourceLinks *links);
    // Forgets an asset and its outgoing edges. An asset something still
    // uses stays as a missing placeholder so its dependents stay truthful.
    Q_INVOKABLE bool removeAsset(const QString &id);
    // Removes every missing asset nothing uses; returns how many went.
    Q_INVOKABLE int pruneMissing();
    // Scans every scannable source under projectRoot on a worker thread.
    // Returns a request ID; projectScanFinished or projectScanDiscarded follows.
    Q_INVOKABLE QString scanProjectAsync();
    Q_INVOKABLE void cancelProjectScan();
    Q_INVOKABLE QVariantMap asset(const QString &id) const;
    Q_INVOKABLE QStringList dependencies(const QString &id) const;
    Q_INVOKABLE QVariantList references(const QString &id) const;
    Q_INVOKABLE QStringList dependents(const QString &id) const;
    Q_INVOKABLE QStringList affectedBy(const QString &id) const;
    // Diagnostics that make rebuilding `id` invalid: a missing or cyclic
    // asset anywhere in its dependency closure, including itself.
    Q_INVOKABLE QVariantList rebuildBlockers(const QString &id) const;
    // For a replaced input: {valid: [ids, inputs before outputs], blocked:
    // [{id, reason}]} over everything affectedBy(changedId).
    Q_INVOKABLE QVariantMap rebuildPlan(const QString &changedId) const;
    Q_INVOKABLE QStringList relinkCandidates(const QString &id) const;
    // Same walk on a worker; relinkCandidatesReady follows.
    Q_INVOKABLE QString relinkCandidatesAsync(const QString &id);
    Q_INVOKABLE QVariantList relinkPlan(const QString &missingId,
                                        const QString &replacementId) const;

    // Pure extraction used by scanQmlFile and the project worker.
    static Extraction extractQml(const QString &root, const QString &sourceId);
    static Extraction extractMarkup(const QString &root, const QString &sourceId);
    static Extraction extractSource(const QString &root, const QString &sourceId);
    // "qml", "svg", "html" or "css" for a scannable source, else "".
    static QString sourceKind(const QString &id);
    static QStringList findRelinkCandidates(const QString &root, const QString &id);
    static QString absolutePathIn(const QString &root, const QString &id);

signals:
    void changed();
    void scanRejected(const QString &sourceId, const QString &reason);
    void projectScanFinished(const QString &requestId, int sourceCount, int skippedCount);
    void projectScanDiscarded(const QString &requestId);
    void relinkCandidatesReady(const QString &requestId, const QString &assetId,
                               const QStringList &candidates);

private:
    struct Asset {
        QString id;
        QString label;
        QString uri;
        QString kind;
        bool available = false;
        // Created by a scan (a source, a target or a placeholder) and never
        // registered by the host; only these are pruned automatically.
        bool discovered = false;
        QHash<QString, Edge> dependencies;
        quint64 scanToken = 0;
    };
    struct SourceResult {
        QString sourceId;
        Extraction extraction;
    };
    struct ProjectScan {
        QString root;
        QString error;
        bool cancelled = false;
        QList<SourceResult> sources;
    };
    static bool validId(const QString &id);
    static ProjectScan runProjectScan(const QString &root,
                                      std::shared_ptr<std::atomic_bool> cancel);
    void applyProjectScan(const ProjectScan &scan, const QHash<QString, quint64> &tokens,
                          const QString &requestId);
    bool applyExtraction(const QString &sourceId, const Extraction &extraction);
    bool upsert(const QString &id, const QString &label, const QString &uri,
                bool available, const QString &kind, bool discovered);
    void pruneOrphans(const QSet<QString> &candidates);
    bool commitEdges(const QString &sourceId, const QString &token,
                     QHash<QString, Edge> next);
    void notify();
    bool fail(const QString &message);
    QVariantMap assetMap(const Asset &entry) const;
    QHash<QString, Asset> m_assets;
    QString m_projectRoot;
    QString m_error;
    std::shared_ptr<std::atomic_bool> m_scanCancel;
    quint64 m_projectGeneration = 0;
    quint64 m_relinkSerial = 0;
    QSet<QString> m_pruneCandidates;  // collected while m_quiet > 0
    int m_edgeCount = 0;
    int m_quiet = 0;
};

} // namespace QindaTK
