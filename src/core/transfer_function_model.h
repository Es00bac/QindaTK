// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_model.h"
#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class UndoService;
// Measured scalar value → color and opacity for image and volume views. Stops
// have stable IDs, strictly increasing values and endpoints pinned to the
// domain; RGB and opacity interpolate linearly between neighbouring stops.
// AGENT-CONTRACT (docs/volume-imaging.md#transfer-functions, D-148): every
// committed stop edit is one typed, undoable history step (before/after stop
// lists) in the model's UndoService; a host session may adopt that history.
// Between beginEdit() and commitEdit() edits are previews: views update but
// nothing is recorded, and cancelEdit() restores the state at beginEdit().
// configure() and restoreSettings() are loads: they reset the history.
class TransferFunctionModel : public QObject, public DocumentModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double domainMin READ domainMin NOTIFY changed)
    Q_PROPERTY(double domainMax READ domainMax NOTIFY changed)
    Q_PROPERTY(QString unit READ unit NOTIFY changed)
    Q_PROPERTY(QVariantList points READ points NOTIFY changed)
    Q_PROPERTY(quint64 revision READ revision NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY historyChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY historyChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService NOTIFY historyChanged)
public:
    explicit TransferFunctionModel(QObject *parent = nullptr);
    ~TransferFunctionModel() override;
    double domainMin() const { return m_min; }
    double domainMax() const { return m_max; }
    QString unit() const { return m_unit; }
    QVariantList points() const;
    quint64 revision() const { return m_revision; }
    QString lastError() const { return m_error; }
    Q_INVOKABLE bool configure(double minimum, double maximum, const QString &unit = {});
    Q_INVOKABLE bool replacePoints(const QVariantList &points);
    Q_INVOKABLE QString addPoint(double value, const QColor &color, double opacity);
    Q_INVOKABLE bool movePoint(const QString &id, double value);
    Q_INVOKABLE bool setPointColor(const QString &id, const QColor &color);
    Q_INVOKABLE bool setPointOpacity(const QString &id, double opacity);
    Q_INVOKABLE bool removePoint(const QString &id);
    Q_INVOKABLE QVariantMap point(const QString &id) const;
    Q_INVOKABLE QVariantList previewColors(int count = 64) const;
    Q_INVOKABLE QVariantMap saveSettings() const;
    Q_INVOKABLE bool restoreSettings(const QVariantMap &settings);
    Q_INVOKABLE QColor colorAt(double value) const;
    Q_INVOKABLE double opacityAt(double value) const;
    bool editing() const { return m_editing; }
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    UndoService *undoService() const;
    // One gesture (a drag, a slider press): edits until commitEdit() are
    // previews and become one history step labelled `label`.
    Q_INVOKABLE bool beginEdit(const QString &label);
    Q_INVOKABLE bool commitEdit();
    Q_INVOKABLE bool cancelEdit();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // DocumentModel: the standalone history can move into a DocumentSession.
    QObject *modelObject() const override { return const_cast<TransferFunctionModel *>(this); }
    DocumentId modelId() const override { return DocumentId::parse(m_documentId); }
    QString modelKind() const override { return QStringLiteral("transfer-function"); }
    SnapshotHeader modelHeader() const override;
    QMetaObject::Connection connectModelChanged(QObject *receiver, std::function<void()> slot) override;
    OperationError prepareCommand(const DocumentCommand &command, bool forward) const override;
    void publishCommand(const DocumentCommand &command, bool forward) override;
    DocumentCommandPtr composeCommands(const DocumentCommand &first,
                                       const DocumentCommand &second) const override;
signals:
    void changed();
    void errorChanged();
    void editingChanged();
    void historyChanged();
    // After every committed step, undo, redo or load (not after previews).
    void committed();
private:
    struct Point {
        QString id; double value; QColor color; double opacity;
        bool operator==(const Point &) const = default;
    };
    struct TransferCommand;
    struct Segment { const Point *left; const Point *right; double t; };
    Segment segmentAt(double value) const;
    void publish();
    void fail(const QString &message);
    int indexOf(const QString &id) const;
    // Records `after` as one step from the current points, or previews it
    // while a gesture is open. False (with lastError) when history refuses.
    bool apply(QVector<Point> after, const QString &label);
    bool parsePoints(const QVariantList &points, double min, double max, QVector<Point> *out);
    void resetHistory();
    UndoService *history() const;
    void historyAttached(UndoService *service) override;
    double m_min = 0, m_max = 255;
    QString m_unit;
    QVector<Point> m_points, m_editBefore;
    QString m_editLabel;
    bool m_editing = false;
    quint64 m_revision = 0;
    quint64 m_resetGeneration = 0;
    QString m_documentId;
    UndoService *m_localHistory = nullptr;
    QMetaObject::Connection m_historyConnection;
    QString m_error;
};
} // namespace QindaTK
