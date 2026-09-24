// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "edit_transaction.h"
#include "gradient_evaluator.h"
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace QindaTK {

class GradientEditorModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString gradientId READ gradientId NOTIFY changed)
  Q_PROPERTY(QVariantList stops READ stops NOTIFY changed)
  Q_PROPERTY(QVariantList previewSamples READ previewSamples NOTIFY changed)
  Q_PROPERTY(QString selectedStopId READ selectedStopId NOTIFY changed)
  Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
  Q_PROPERTY(bool editing READ editing NOTIFY changed)
  Q_PROPERTY(QindaTK::UndoService *undoService READ undoService CONSTANT)
public:
  explicit GradientEditorModel(QObject *parent = nullptr);
  ~GradientEditorModel() override;
  QString gradientId() const { return m_definition.id; }
  QVariantList stops() const;
  QVariantList previewSamples() const;
  QString selectedStopId() const { return m_selected; }
  QString lastError() const { return m_error; }
  bool editing() const { return m_transaction.active(); }
  UndoService *undoService() { return &m_undo; }
  Q_INVOKABLE bool undo() { return m_undo.undo(); }
  Q_INVOKABLE bool redo() { return m_undo.redo(); }
  const GradientDefinition &definition() const { return m_definition; }
  Q_INVOKABLE bool selectStop(const QString &id);
  Q_INVOKABLE bool
  beginGesture(const QString &label = QStringLiteral("Edit gradient"));
  Q_INVOKABLE bool previewMove(const QString &id, double position);
  Q_INVOKABLE bool previewColor(const QString &id, const QString &hex);
  Q_INVOKABLE bool commitGesture();
  Q_INVOKABLE void cancelGesture();
  Q_INVOKABLE bool addStop(double position);
  Q_INVOKABLE bool deleteStop(const QString &id);
  Q_INVOKABLE bool setStopColor(const QString &id, const QString &hex);
  Q_INVOKABLE QByteArray encode() const;
  Q_INVOKABLE bool decode(const QByteArray &bytes);
signals:
  void changed();
  void editRejected(const QString &message);

private:
  bool apply(GradientDefinition candidate, const QString &label, bool commit);
  bool publishPreview(const GradientDefinition &candidate);
  bool parseHex(const QString &hex, ColorValue *value) const;
  void syncFromDocument();
  void rebuildSamples();
  GradientDefinition m_definition;
  QString m_selected;
  QString m_error;
  std::unique_ptr<PropertyDocument> m_document;
  UndoService m_undo;
  EditTransaction m_transaction;
  EntityId m_entity;
  ColorProfile m_target = ColorProfile::sRgb();
  ColorEngine m_engine;
  QVariantList m_samples;
};

class GradientDefinitionCodec {
public:
  static QByteArray encode(const GradientDefinition &definition,
                           OperationError *error = nullptr);
  static std::optional<GradientDefinition>
  decode(const QByteArray &bytes, OperationError *error = nullptr);
};

} // namespace QindaTK
