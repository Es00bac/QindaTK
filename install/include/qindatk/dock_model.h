// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: the arrangement authority for dockable panels. It owns
// where every panel is; DockHost (QML) only draws that and reports
// gestures. The model is zone-based, as Sloom Studio's dockablePanel
// contract and QindaStudio's PanelLayoutController are: a panel is docked,
// floating, collapsed or hidden; a docked panel sits in one zone (left,
// right, top, bottom, center, overlay), in one lane of that zone (lanes are
// columns for side zones, rows for the others), at an order inside the
// lane, optionally sharing a tab group with neighbours. Arrangements are
// per workspace, persist across runs (`storageKey`), and can be saved by
// name. Every operation is idempotent on invalid ids.
//
// Compatibility: method names follow PanelLayoutController so QindaStudio
// can adopt this model with a find-and-replace; dockBeside/lanes/shares
// are the additions that make drag-to-dock and lane splitting possible.
class DockModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged)
    Q_PROPERTY(QVariantList panels READ panels NOTIFY layoutChanged)
    Q_PROPERTY(QStringList savedLayouts READ savedLayouts NOTIFY savedLayoutsChanged)
    Q_PROPERTY(QStringList presets READ presets NOTIFY layoutChanged)
    // QSettings group for persist()/restore(); empty disables persistence.
    Q_PROPERTY(QString storageKey READ storageKey WRITE setStorageKey NOTIFY storageKeyChanged)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY layoutChanged)

public:
    enum Mode { Docked, Floating, Collapsed, Hidden };
    Q_ENUM(Mode)
    enum Zone { Left, Right, Top, Bottom, Center, Overlay };
    Q_ENUM(Zone)

    explicit DockModel(QObject *parent = nullptr);
    ~DockModel() override;

    Q_INVOKABLE static QString modeName(int mode);
    Q_INVOKABLE static QString zoneName(int zone);
    Q_INVOKABLE static QStringList zoneNames();

    // ---- registry ----
    // definition: {title, mode, zone, lane, order, extent, share,
    //   floatingRect {x,y,width,height}, minWidth, minHeight, fixedSize,
    //   closable, chrome, allowedZones [names], group, groupActive}.
    // Idempotent: a panel that already carries a user arrangement keeps it.
    Q_INVOKABLE void registerPanel(const QString &workspace, const QString &panelId,
                                   const QVariantMap &definition);
    Q_INVOKABLE void unregisterPanel(const QString &workspace, const QString &panelId);
    Q_INVOKABLE [[nodiscard]] bool hasPanel(const QString &panelId) const;
    Q_INVOKABLE [[nodiscard]] QStringList panelIds() const;

    [[nodiscard]] QString workspace() const { return m_workspace; }
    void setWorkspace(const QString &workspace);
    [[nodiscard]] QVariantList panels() const;
    Q_INVOKABLE [[nodiscard]] QVariantMap panel(const QString &panelId) const;
    // Docked and collapsed panels of a zone, sorted by lane then order.
    Q_INVOKABLE [[nodiscard]] QVariantList panelsInZone(const QString &zone) const;
    // [{lane, extent, slots: [{panelIds, activeId, share, collapsed}]}]
    Q_INVOKABLE [[nodiscard]] QVariantList lanes(const QString &zone) const;
    Q_INVOKABLE [[nodiscard]] qreal zoneExtent(const QString &zone) const;
    Q_INVOKABLE [[nodiscard]] QVariantList floatingPanels() const;
    Q_INVOKABLE [[nodiscard]] bool acceptsZone(const QString &panelId, const QString &zone) const;

    // ---- presentation ----
    Q_INVOKABLE void setMode(const QString &panelId, const QString &mode);
    Q_INVOKABLE void dock(const QString &panelId, const QString &zone, int lane = -1);
    // placement: "before" | "after" (same lane), "tab" (join the target's
    // group), "lane-before" | "lane-after" (a new lane next to the target's).
    Q_INVOKABLE void dockBeside(const QString &panelId, const QString &targetPanelId,
                                const QString &placement);
    Q_INVOKABLE void floatPanel(const QString &panelId, double x = -1, double y = -1);
    Q_INVOKABLE void hide(const QString &panelId);
    Q_INVOKABLE void show(const QString &panelId);
    Q_INVOKABLE void collapse(const QString &panelId, bool collapsed);
    Q_INVOKABLE void toggle(const QString &panelId);

    // ---- geometry ----
    Q_INVOKABLE void moveFloating(const QString &panelId, double x, double y);
    Q_INVOKABLE void resizeFloating(const QString &panelId, double width, double height);
    Q_INVOKABLE void setFloatingRect(const QString &panelId, double x, double y, double width, double height);
    // Extent of a docked panel's lane (width for side zones, height else).
    Q_INVOKABLE void resizeDocked(const QString &panelId, double extent);
    Q_INVOKABLE void setLaneExtent(const QString &zone, int lane, double extent);
    // Relative weight of a slot inside its lane.
    Q_INVOKABLE void setShare(const QString &panelId, double share);
    Q_INVOKABLE void bringToFront(const QString &panelId);

    // ---- tab groups ----
    Q_INVOKABLE void groupWith(const QString &panelId, const QString &targetPanelId);
    Q_INVOKABLE void ungroup(const QString &panelId);
    Q_INVOKABLE void activateInGroup(const QString &panelId);
    Q_INVOKABLE void moveInGroup(const QString &panelId, int index);

    // ---- presets and saved layouts ----
    Q_INVOKABLE void reset();
    Q_INVOKABLE void registerPreset(const QString &workspace, const QString &presetId,
                                    const QVariantMap &statesByPanelId);
    Q_INVOKABLE void applyPreset(const QString &presetId);
    [[nodiscard]] QStringList presets() const;
    [[nodiscard]] QStringList savedLayouts() const;
    Q_INVOKABLE void saveLayout(const QString &name);
    Q_INVOKABLE void applyLayout(const QString &name);
    Q_INVOKABLE void deleteLayout(const QString &name);

    // ---- persistence ----
    [[nodiscard]] QString storageKey() const { return m_storageKey; }
    void setStorageKey(const QString &key);
    Q_INVOKABLE void persist() const;
    Q_INVOKABLE void restore();
    Q_INVOKABLE [[nodiscard]] QString serialize() const;
    Q_INVOKABLE bool deserialize(const QString &json);
    [[nodiscard]] qulonglong generation() const { return m_generation; }

signals:
    void workspaceChanged();
    void layoutChanged();
    void savedLayoutsChanged();
    void storageKeyChanged();
    // A hidden or collapsed panel was brought back; hosts focus it.
    void panelShown(const QString &panelId);

private:
    struct PanelState final {
        QString title;
        Mode mode = Docked;
        Mode defaultMode = Docked;
        QString zone = QStringLiteral("right");
        QString defaultZone = QStringLiteral("right");
        int lane = 0;
        int defaultLane = 0;
        int order = 0;
        int defaultOrder = 0;
        qreal extent = 280;
        qreal defaultExtent = 280;
        qreal share = 1;
        qreal defaultShare = 1;
        QRectF floatingRect{80, 80, 320, 400};
        QRectF defaultFloatingRect{80, 80, 320, 400};
        QSizeF minimumSize{160, 100};
        bool fixedSize = false;
        bool closable = true;
        QString chrome = QStringLiteral("default");
        QStringList allowedZones;
        bool zonesDeclared = false;
        int zOrder = 0;
        QString group;
        QString defaultGroup;
        bool groupActive = true;
        bool defaultGroupActive = true;
    };
    using Workspace = QHash<QString, PanelState>;

    [[nodiscard]] Workspace *activeWorkspace();
    [[nodiscard]] const Workspace *activeWorkspace() const;
    [[nodiscard]] PanelState *state(const QString &panelId);
    [[nodiscard]] const PanelState *state(const QString &panelId) const;
    [[nodiscard]] QVariantMap toMap(const QString &panelId, const PanelState &panel) const;
    [[nodiscard]] QVariantMap arrangementOf(const PanelState &panel) const;
    void applyArrangement(PanelState &panel, const QVariantMap &values);
    void applyDefaults(PanelState &panel);
    // Panel ids of a zone/lane in order, or of a whole zone when lane < 0.
    [[nodiscard]] QStringList laneOrder(const QString &zone, int lane) const;
    void renumberLane(const QString &zone, int lane);
    [[nodiscard]] int nextZOrder();
    void touched();

    QHash<QString, Workspace> m_workspaces;
    QHash<QString, QHash<QString, QVariantMap>> m_presets;
    QHash<QString, QHash<QString, QVariantMap>> m_savedLayouts;
    // Arrangements restored before their panels registered.
    QHash<QString, QHash<QString, QVariantMap>> m_pending;
    QString m_workspace = QStringLiteral("default");
    QString m_storageKey;
    int m_zCounter = 1;
    qulonglong m_generation = 0;
    bool m_restoring = false;
};

} // namespace QindaTK
