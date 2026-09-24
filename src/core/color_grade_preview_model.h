// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "color_grade_processor.h"
#include "video_scopes_model.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A small QML adapter for the deterministic pixel fixture. Production hosts
// pass decoded frames to ColorGradeProcessor directly.
class ColorGradePreviewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString beforeImageUrl READ beforeImageUrl NOTIFY changed)
    Q_PROPERTY(QString afterImageUrl READ afterImageUrl NOTIFY changed)
    Q_PROPERTY(QVariantMap beforeScopes READ beforeScopes NOTIFY changed)
    Q_PROPERTY(QVariantMap afterScopes READ afterScopes NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    explicit ColorGradePreviewModel(QObject *parent = nullptr);
    QString beforeImageUrl() const { return m_beforeImageUrl; }
    QString afterImageUrl() const { return m_afterImageUrl; }
    QVariantMap beforeScopes() const { return m_beforeScopes; }
    QVariantMap afterScopes() const { return m_afterScopes; }
    QString errorMessage() const { return m_errorMessage; }
    Q_INVOKABLE QVariantMap render(const QVariantMap &parameters);
signals:
    void changed();
private:
    static QImage fixtureFrame();
    static QString pngDataUrl(const QImage &image);
    ColorGradeProcessor m_processor;
    VideoScopesModel m_scopeModel;
    ColorContextSnapshot m_context;
    QImage m_beforeImage;
    QString m_beforeImageUrl;
    QString m_afterImageUrl;
    QVariantMap m_beforeScopes;
    QVariantMap m_afterScopes;
    QString m_errorMessage;
    quint64 m_revision = 0;
};

} // namespace QindaTK
