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

namespace QindaTK {

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
    // Scans every QML file under projectRoot on a worker thread. Returns a
    // request ID; projectScanFinished or projectScanDiscarded follows.
    Q_INVOKABLE QString scanProjectAsync();
    Q_INVOKABLE void cancelProjectScan();
    Q_INVOKABLE QVariantMap asset(const QString &id) const;
    Q_INVOKABLE QStringList dependencies(const QString &id) const;
    Q_INVOKABLE QVariantList references(const QString &id) const;
    Q_INVOKABLE QStringList dependents(const QString &id) const;
    Q_INVOKABLE QStringList affectedBy(const QString &id) const;
    Q_INVOKABLE QStringList relinkCandidates(const QString &id) const;
    Q_INVOKABLE QVariantList relinkPlan(const QString &missingId,
                                        const QString &replacementId) const;

    // Pure extraction used by scanQmlFile and the project worker.
    static Extraction extractQml(const QString &root, const QString &sourceId);
    static QString absolutePathIn(const QString &root, const QString &id);

signals:
    void changed();
    void scanRejected(const QString &sourceId, const QString &reason);
    void projectScanFinished(const QString &requestId, int sourceCount, int skippedCount);
    void projectScanDiscarded(const QString &requestId);

private:
    struct Asset {
        QString id;
        QString label;
        QString uri;
        QString kind;
        bool available = false;
        QHash<QString, Edge> dependencies;
        quint64 scanToken = 0;
    };
    struct SourceResult {
        QString sourceId;
        Extraction extraction;
    };
    struct ProjectScan {
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
    int m_edgeCount = 0;
    int m_quiet = 0;
};

} // namespace QindaTK
