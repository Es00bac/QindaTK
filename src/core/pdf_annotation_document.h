// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

namespace QindaTK {

// A local PDF annotation writer. Coordinates are unrotated page points with
// the origin at the top left, matching pdf-text word rectangles.
class PdfAnnotationDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY changed)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)
    Q_PROPERTY(QVariantList annotations READ annotations NOTIFY changed)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
public:
    explicit PdfAnnotationDocument(QObject *parent = nullptr);
    ~PdfAnnotationDocument() override;

    QString sourcePath() const;
    int pageCount() const;
    QVariantList annotations() const;
    QVariantMap capabilities() const;
    bool dirty() const;
    QString errorString() const;

    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE QString addAnnotation(int pageIndex, const QString &kind,
                                      const QVariantMap &boundsPoints,
                                      const QString &text, const QString &author = {});
    Q_INVOKABLE bool removeAnnotation(const QString &id);
    Q_INVOKABLE bool saveAs(const QString &path);
signals:
    void changed();
private:
    struct Private;
    std::unique_ptr<Private> d;
    bool fail(const QString &message);
    void refresh();
};

} // namespace QindaTK
