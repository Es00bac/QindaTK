// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QColor>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <vector>

namespace QindaTK {

enum class CanvasDevice : quint8 { Mouse, Touch, Pen, Eraser };

// One pointer sample. Both positions are supplied by the view so a tool
// never needs the view transform; `zoom` converts pixel thresholds.
struct CanvasToolInput {
    QPointF viewPoint;
    QPointF documentPoint;
    qreal pressure = 1.0;
    qreal zoom = 1.0;
    Qt::KeyboardModifiers modifiers;
    CanvasDevice device = CanvasDevice::Mouse;
};

// View hooks a navigation tool may call. Implemented by CanvasSurface.
class CanvasNavigator {
public:
    virtual ~CanvasNavigator() = default;
    virtual void navigatePan(QPointF viewDelta) = 0;
    virtual void navigateZoom(QPointF viewAnchor, qreal factor) = 0;
};

// AGENT-CONTRACT (docs/canvas.md#transient-preview): what a tool shows while
// a gesture runs, in DOCUMENT coordinates. It is never document state; WP-041
// overlays read the same structure.
struct CanvasToolPreview {
    enum Kind : quint8 { None, Marquee, Rect, Ellipse, Polyline, Move, Lasso };
    Kind kind = None;
    QRectF rect;
    QPolygonF points;
    QPointF offset;
    QVector<CanvasObjectId> ids;
};

struct CanvasToolContext {
    CanvasDocumentAdapter *document = nullptr;
    CanvasNavigator *navigator = nullptr;
    QVector<CanvasObjectId> *selection = nullptr;
    qreal hitTolerancePixels = 4.0;
    QColor fillColor;
    QColor strokeColor;
    qreal strokeWidth = 1.0;
};

// AGENT-CONTRACT (docs/canvas.md#tool-state-machine): a tool sees exactly
// begin -> update* -> (finish | cancel). finish returns the ONE edit to
// commit (empty for navigation, clicks and selection-only gestures);
// cancel must drop all transient state and never touch the document, which
// may already be gone (context.document == nullptr).
class CanvasTool {
public:
    virtual ~CanvasTool() = default;
    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual QString label() const = 0;
    [[nodiscard]] virtual QString icon() const = 0;
    [[nodiscard]] virtual Qt::CursorShape cursor() const { return Qt::ArrowCursor; }
    [[nodiscard]] virtual bool editsDocument() const = 0;
    virtual bool begin(CanvasToolContext &context, const CanvasToolInput &input, QString *declined) = 0;
    virtual void update(CanvasToolContext &context, const CanvasToolInput &input) = 0;
    virtual CanvasEdit finish(CanvasToolContext &context, const CanvasToolInput &input) = 0;
    virtual void cancel(CanvasToolContext &context) = 0;
    [[nodiscard]] virtual CanvasToolPreview preview() const { return {}; }
};

// select, pan, rectangle, ellipse, pen -- see canvas_tools.cpp.
std::vector<std::unique_ptr<CanvasTool>> createStandardCanvasTools();

// AGENT-NOTE: ToolController is the per-canvas gesture arbiter. It does not
// claim through GestureRouter because the router's Edit intent is bound to a
// PropertyDocument EditTransaction (WP-012); bridging spatial edits into it
// belongs to the WP-033 session seam (docs/canvas.md#open-seams).
class ToolController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString activeTool READ activeTool WRITE setActiveTool NOTIFY toolChanged)
    Q_PROPERTY(QString effectiveTool READ effectiveTool NOTIFY toolChanged)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY configurationChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool gestureActive READ gestureActive NOTIFY stateChanged)
    Q_PROPERTY(QString gestureOwner READ gestureOwner NOTIFY stateChanged)
    Q_PROPERTY(QString lastOutcome READ lastOutcome NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap preview READ previewMap NOTIFY previewChanged)
    Q_PROPERTY(QVariantList selection READ selectionList WRITE setSelectionList NOTIFY selectionChanged)
    Q_PROPERTY(QColor fillColor MEMBER m_fillColor NOTIFY configurationChanged)
    Q_PROPERTY(QColor strokeColor MEMBER m_strokeColor NOTIFY configurationChanged)
    Q_PROPERTY(qreal strokeWidth MEMBER m_strokeWidth NOTIFY configurationChanged)
    Q_PROPERTY(qreal hitTolerance MEMBER m_hitTolerance NOTIFY configurationChanged)

public:
    enum Device { Mouse, Touch, Pen, Eraser };
    Q_ENUM(Device)

    explicit ToolController(QObject *parent = nullptr);
    ~ToolController() override;

    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    QString activeTool() const { return m_activeTool; }
    void setActiveTool(const QString &id);
    QString effectiveTool() const;
    QVariantList tools() const;
    QString state() const { return m_tool ? QStringLiteral("active") : QStringLiteral("idle"); }
    bool gestureActive() const { return m_tool != nullptr; }
    QString gestureOwner() const { return m_owner; }
    QString lastOutcome() const { return m_lastOutcome; }
    CanvasToolPreview preview() const;
    QVariantMap previewMap() const;
    const QVector<CanvasObjectId> &selection() const { return m_selection; }
    void setSelection(const QVector<CanvasObjectId> &ids);
    QVariantList selectionList() const;
    void setSelectionList(const QVariantList &ids);
    Qt::CursorShape cursor() const;

    bool registerTool(std::unique_ptr<CanvasTool> tool);
    [[nodiscard]] bool hasTool(const QString &id) const;

    // Gesture API used by views. Only the view named in begin() may update
    // or end the gesture; a second begin while one runs is rejected.
    bool beginGesture(const QString &viewId, const CanvasToolInput &input,
                      CanvasNavigator *navigator = nullptr, QObject *navigatorOwner = nullptr);
    bool updateGesture(const QString &viewId, const CanvasToolInput &input);
    bool endGesture(const QString &viewId, const CanvasToolInput &input);

    // QML/test entry points in DOCUMENT coordinates (no navigator).
    Q_INVOKABLE bool begin(const QString &viewId, qreal x, qreal y,
                           QindaTK::ToolController::Device device = Mouse,
                           int modifiers = 0, qreal pressure = 1.0);
    Q_INVOKABLE bool update(const QString &viewId, qreal x, qreal y, qreal pressure = 1.0);
    Q_INVOKABLE bool end(const QString &viewId, qreal x, qreal y);
    Q_INVOKABLE void cancel(const QString &reason = QStringLiteral("cancel"));
    Q_INVOKABLE void escape() { cancel(QStringLiteral("escape")); }
    Q_INVOKABLE void lostGrab(const QString &viewId);
    Q_INVOKABLE void viewDetached(const QString &viewId);
    Q_INVOKABLE bool pushTemporaryTool(const QString &id);
    Q_INVOKABLE bool popTemporaryTool();

signals:
    void documentChanged();
    void configurationChanged();
    void toolChanged();
    void stateChanged();
    void previewChanged();
    void selectionChanged();
    void gestureCommitted(const QString &toolId, qulonglong revision);
    void gestureCanceled(const QString &toolId, const QString &reason);

private:
    CanvasTool *findTool(const QString &id) const;
    CanvasToolContext context();
    void finishGesture(const QString &outcome);
    void onDocumentEdited(quint64 revision);
    void pruneSelection();

    std::vector<std::unique_ptr<CanvasTool>> m_tools;
    QPointer<CanvasDocumentAdapter> m_document;
    QPointer<QObject> m_navigatorOwner;
    CanvasNavigator *m_navigator = nullptr;
    CanvasTool *m_tool = nullptr; // non-null while a gesture runs
    QString m_activeTool = QStringLiteral("select");
    QStringList m_temporary;
    QString m_owner;
    QString m_lastOutcome = QStringLiteral("idle");
    QVector<CanvasObjectId> m_selection;
    quint64 m_baseRevision = 0;
    bool m_committing = false;
    QColor m_fillColor{QStringLiteral("#4fa3d9")};
    QColor m_strokeColor{QStringLiteral("#1b2a36")};
    qreal m_strokeWidth = 2.0;
    qreal m_hitTolerance = 4.0;
};

} // namespace QindaTK
