// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "color_grade_processor.h"
#include "edit_transaction.h"
#include "video_scopes_model.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// One clip/frame grade gesture adapter. The host supplies a tagged source image
// and immutable color context; previews stay transient in PropertyDocument.
class ColorGradeEditSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString clipId READ clipId WRITE setClipId NOTIFY changed)
    Q_PROPERTY(QString frameId READ frameId WRITE setFrameId NOTIFY changed)
    Q_PROPERTY(QImage sourceImage READ sourceImage WRITE setSourceImage NOTIFY changed)
    Q_PROPERTY(QImage processedImage READ processedImage NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY changed)
    Q_PROPERTY(QString beforeImageUrl READ beforeImageUrl NOTIFY changed)
    Q_PROPERTY(QString afterImageUrl READ afterImageUrl NOTIFY changed)
    Q_PROPERTY(QVariantMap beforeScopes READ beforeScopes NOTIFY changed)
    Q_PROPERTY(QVariantMap afterScopes READ afterScopes NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    // ColorWheels descriptors built from the current parameters
    // ({id, label, x, y, minimumX, maximumX, minimumY, maximumY, stepX, stepY,
    // enabled}), the current exposure, and the declared grade space.
    Q_PROPERTY(QVariantList wheels READ wheels NOTIFY changed)
    Q_PROPERTY(double exposure READ exposure NOTIFY changed)
    Q_PROPERTY(QString gradeSpace READ gradeSpace CONSTANT)
public:
    explicit ColorGradeEditSession(QObject *parent = nullptr);
    QString clipId() const { return m_clipId; }
    void setClipId(const QString &id);
    QString frameId() const { return m_frameId; }
    void setFrameId(const QString &id);
    QImage sourceImage() const { return m_sourceImage; }
    QImage processedImage() const { return m_processedImage; }
    void setSourceImage(const QImage &image);
    QVariantMap parameters() const;
    QString beforeImageUrl() const { return m_beforeUrl; }
    QString afterImageUrl() const { return m_afterUrl; }
    QVariantMap beforeScopes() const { return m_beforeScopes; }
    QVariantMap afterScopes() const { return m_afterScopes; }
    QString errorMessage() const { return m_error.message; }
    bool editing() const { return m_transaction.active(); }
    bool canUndo() const;
    bool canRedo() const;
    QVariantList wheels() const;
    double exposure() const;
    QString gradeSpace() const;
    // The last committed grade, without an open gesture's preview; exports
    // read this so a drag in progress never reaches a file.
    QVariantMap committedParameters() const;

    // Host-only profile/context configuration; a generation change cancels the
    // current gesture before the new context can render.
    void setFrame(const QString &clipId, const QString &frameId, const QImage &taggedImage,
                  const ColorContextSnapshot &context);
    void setColorContext(const ColorContextSnapshot &context);

    Q_INVOKABLE bool beginGesture(const QString &gestureId);
    Q_INVOKABLE bool preview(const QString &gestureId, const QVariantMap &candidate);
    Q_INVOKABLE bool finishGesture(const QString &gestureId);
    Q_INVOKABLE bool cancelGesture(const QString &gestureId);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    // AGENT-CONTRACT (docs/color.md "Grade gestures"; ColorWheels): wheel
    // drags and numeric entry build the same parameter map through
    // withWheel(), so equal x/y give equal parameters and pixels. A drag is
    // previewWheel()* then finishWheel() under one gesture ID (one undo
    // step); setWheelValue/setExposure/resetWheel are one-step numeric edits.
    Q_INVOKABLE bool previewWheel(const QString &gestureId, const QString &wheelId, double x, double y);
    Q_INVOKABLE bool finishWheel(const QString &gestureId, const QString &wheelId, double x, double y);
    Q_INVOKABLE bool setWheelValue(const QString &wheelId, double x, double y);
    Q_INVOKABLE bool setExposure(double exposureEv);
    Q_INVOKABLE bool resetWheel(const QString &wheelId);
signals:
    void changed();
    void gestureRejected(const QString &gestureId, const QString &message);
private:
    static QVariantMap toMap(const ColorGradeParameters &parameters);
    static QString pngDataUrl(const QImage &image);
    bool render(const QVariantMap &candidate);
    void refresh();
    void resetForTarget();
    bool reject(const QString &gestureId, const QString &message);
    // parameters() with one wheel (lift, gamma, gain) or "exposure" replaced.
    std::optional<QVariantMap> withWheel(const QString &wheelId, double x, double y) const;
    bool applyNumeric(const QString &label, const QVariantMap &candidate);

    PropertyDocument m_document;
    UndoService m_undo;
    EditTransaction m_transaction;
    EntityId m_entity = EntityId::create();
    ColorGradeProcessor m_processor;
    VideoScopesModel m_scopeModel;
    ColorContextSnapshot m_context;
    QImage m_sourceImage;
    QImage m_processedImage;
    QString m_clipId = QStringLiteral("clip");
    QString m_frameId = QStringLiteral("frame");
    QString m_gestureId;
    QString m_beforeUrl;
    QString m_afterUrl;
    QVariantMap m_beforeScopes;
    QVariantMap m_afterScopes;
    OperationError m_error;
    int m_serial = 0;
};

} // namespace QindaTK
