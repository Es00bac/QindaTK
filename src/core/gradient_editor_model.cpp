// SPDX-License-Identifier: LGPL-3.0-or-later
#include "gradient_editor_model.h"
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr auto gradientProperty = "gradient.definition";
constexpr qsizetype maximumCodecBytes = 64 * 1024 * 1024;
ColorValue colorFromHex(const QString &hex, const ColorProfile &profile,
                        bool *ok) {
  QColor c(hex);
  *ok = c.isValid();
  if (!*ok)
    return {};
  return {c.redF(),   c.greenF(),   c.blueF(),
          c.alphaF(), profile.id(), profile.transferId()};
}
QString colorToHex(const ColorValue &value) {
  QColor c;
  c.setRgbF(qBound(0.0, value.red, 1.0), qBound(0.0, value.green, 1.0),
            qBound(0.0, value.blue, 1.0), qBound(0.0, value.alpha, 1.0));
  if (c.alphaF() >= 1.0)
    return c.name(QColor::HexRgb);
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue())
      .arg(c.alphaF(), 0, 'g', 8);
}
} // namespace

QByteArray GradientDefinitionCodec::encode(const GradientDefinition &g,
                                           OperationError *error) {
  if (auto e = GradientEvaluator::validate(g); e) {
    if (error)
      *error = e;
    return {};
  }
  QJsonArray stops;
  for (const auto &s : g.stops) {
    QJsonObject c{{"r", s.color.red},
                  {"g", s.color.green},
                  {"b", s.color.blue},
                  {"a", s.color.alpha}};
    stops.append(QJsonObject{
        {"id", s.id},
        {"position", QString::number(s.position, 'g', 17)},
        {"profile", QString::fromLatin1(s.profile.iccBytes().toBase64())},
        {"color", c}});
  }
  if (error)
    *error = {};
  const QByteArray encoded =
      QJsonDocument(QJsonObject{{"format", "qindatk.gradient.v1"},
                                {"id", g.id},
                                {"stops", stops}})
          .toJson(QJsonDocument::Compact);
  if (encoded.size() > maximumCodecBytes) {
    if (error)
      *error = {ErrorCode::Validation,
                QStringLiteral("Gradient payload exceeds the 64 MiB limit"),
                {},
                QStringLiteral("gradient-codec")};
    return {};
  }
  return encoded;
}
std::optional<GradientDefinition>
GradientDefinitionCodec::decode(const QByteArray &bytes,
                                OperationError *error) {
  if (bytes.isEmpty() || bytes.size() > maximumCodecBytes) {
    if (error)
      *error = {ErrorCode::Validation,
                QStringLiteral(
                    "Gradient payload is empty or exceeds the 64 MiB limit"),
                {},
                QStringLiteral("gradient-codec")};
    return {};
  }
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(bytes, &parse);
  auto fail = [&](QString message) -> std::optional<GradientDefinition> {
    if (error)
      *error = {ErrorCode::Validation, message, {}, "gradient-codec"};
    return {};
  };
  if (parse.error != QJsonParseError::NoError || !doc.isObject())
    return fail("Invalid gradient JSON");
  const auto root = doc.object();
  if (root.value("format") != "qindatk.gradient.v1" ||
      !root.value("stops").isArray())
    return fail("Unsupported gradient format");
  GradientDefinition g;
  g.id = root.value("id").toString();
  for (const auto &item : root.value("stops").toArray()) {
    if (!item.isObject())
      return fail("Invalid stop record");
    const auto o = item.toObject();
    bool ok = false;
    const auto profileBytes =
        QByteArray::fromBase64(o.value("profile").toString().toLatin1());
    auto profile = ColorProfile::fromIcc(profileBytes);
    const auto co = o.value("color").toObject();
    bool posOk = false;
    const double pos = o.value("position").toString().toDouble(&posOk);
    if (!profile.isValid() || !posOk || !co.value("r").isDouble() ||
        !co.value("g").isDouble() || !co.value("b").isDouble() ||
        !co.value("a").isDouble())
      return fail("Invalid stop encoding");
    ColorValue cv{co.value("r").toDouble(),
                  co.value("g").toDouble(),
                  co.value("b").toDouble(),
                  co.value("a").toDouble(),
                  profile.id(),
                  profile.transferId()};
    Q_UNUSED(ok);
    g.stops.append({o.value("id").toString(), pos, cv, profile});
  }
  if (auto e = GradientEvaluator::validate(g); e) {
    if (error)
      *error = e;
    return {};
  }
  if (error)
    *error = {};
  return g;
}

GradientEditorModel::GradientEditorModel(QObject *parent)
    : QObject(parent), m_document(std::make_unique<PropertyDocument>()),
      m_entity(EntityId::create()), m_engine(32) {
  m_definition.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  for (int i = 0; i < 2; ++i) {
    const double p = i;
    ColorValue c{p, p, p, 1, m_target.id(), m_target.transferId()};
    m_definition.stops.append(
        {QUuid::createUuid().toString(QUuid::WithoutBraces), p, c, m_target});
  }
  const QByteArray payload = GradientDefinitionCodec::encode(m_definition);
  m_document->defineProperty({QString::fromLatin1(gradientProperty),
                              QMetaType::fromType<QByteArray>(),
                              payload,
                              {},
                              true,
                              false});
  m_document->addEntity(m_entity,
                        {{QString::fromLatin1(gradientProperty), payload}});
  m_undo.addDocument(m_document.get());
  m_undo.setActiveDocument(m_document.get());
  m_transaction.setDocument(m_document.get());
  m_transaction.setUndoService(&m_undo);
  m_transaction.setTargetIds({m_entity.toString()});
  m_transaction.setPropertyId(QString::fromLatin1(gradientProperty));
  connect(m_document.get(), &PropertyDocument::changed, this,
          &GradientEditorModel::syncFromDocument);
  syncFromDocument();
}
GradientEditorModel::~GradientEditorModel() {
  disconnect(m_document.get(), nullptr, this, nullptr);
  m_transaction.cancel();
}

QVariantList GradientEditorModel::stops() const {
  QVariantList out;
  for (const auto &s : m_definition.stops)
    out << QVariantMap{
        {"id", s.id}, {"position", s.position}, {"color", colorToHex(s.color)}};
  return out;
}
QVariantList GradientEditorModel::previewSamples() const { return m_samples; }
void GradientEditorModel::rebuildSamples() {
  m_samples.clear();
  constexpr int n = 64;
  for (int i = 0; i < n; ++i) {
    auto r = GradientEvaluator::evaluate(m_definition, double(i) / (n - 1),
                                         m_target, m_engine);
    m_samples << (r.color ? colorToHex(*r.color) : QStringLiteral("#ff00ff"));
  }
}
void GradientEditorModel::syncFromDocument() {
  const auto b =
      m_document->value(m_entity, QString::fromLatin1(gradientProperty))
          .toByteArray();
  auto decoded = GradientDefinitionCodec::decode(b);
  if (decoded)
    m_definition = *decoded;
  rebuildSamples();
  emit changed();
}
bool GradientEditorModel::selectStop(const QString &id) {
  for (const auto &s : m_definition.stops)
    if (s.id == id) {
      m_selected = id;
      emit changed();
      return true;
    }
  return false;
}
bool GradientEditorModel::beginGesture(const QString &label) {
  m_transaction.setLabel(label);
  if (!m_transaction.begin()) {
    m_error = m_transaction.errorMessage();
    emit editRejected(m_error);
    emit changed();
    return false;
  }
  return true;
}
bool GradientEditorModel::publishPreview(const GradientDefinition &candidate) {
  OperationError e;
  const auto bytes = GradientDefinitionCodec::encode(candidate, &e);
  if (e) {
    m_error = e.message;
    emit editRejected(m_error);
    return false;
  }
  if (!m_transaction.active() && !beginGesture())
    return false;
  if (const auto issue = m_transaction.updateValue(bytes); issue) {
    m_error = issue.message;
    emit editRejected(m_error);
    return false;
  }
  return true;
}
bool GradientEditorModel::previewMove(const QString &id, double p) {
  if (!std::isfinite(p) || p < 0 || p > 1)
    return false;
  auto c = m_definition;
  auto it = std::find_if(c.stops.begin(), c.stops.end(),
                         [&](const auto &s) { return s.id == id; });
  if (it == c.stops.end())
    return false;
  it->position = p;
  std::stable_sort(
      c.stops.begin(), c.stops.end(),
      [](const auto &a, const auto &b) { return a.position < b.position; });
  return publishPreview(c);
}
bool GradientEditorModel::parseHex(const QString &hex, ColorValue *out) const {
  bool ok = false;
  *out = colorFromHex(hex, m_target, &ok);
  return ok;
}
bool GradientEditorModel::previewColor(const QString &id, const QString &hex) {
  ColorValue v;
  if (!parseHex(hex, &v))
    return false;
  auto c = m_definition;
  auto it = std::find_if(c.stops.begin(), c.stops.end(),
                         [&](const auto &s) { return s.id == id; });
  if (it == c.stops.end())
    return false;
  it->color = v;
  return publishPreview(c);
}
bool GradientEditorModel::commitGesture() {
  if (!m_transaction.active())
    return false;
  const bool ok = !m_transaction.commitEdit();
  m_error = m_transaction.errorMessage();
  emit changed();
  return ok;
}
void GradientEditorModel::cancelGesture() {
  m_transaction.cancel();
  syncFromDocument();
}
bool GradientEditorModel::apply(GradientDefinition c, const QString &label,
                                bool commit) {
  if (auto e = GradientEvaluator::validate(c); e) {
    m_error = e.message;
    emit editRejected(m_error);
    return false;
  }
  if (!beginGesture(label) || !publishPreview(c))
    return false;
  return commit ? commitGesture() : true;
}
bool GradientEditorModel::addStop(double p) {
  if (!std::isfinite(p) || p < 0 || p > 1 ||
      m_definition.stops.size() >= GradientEvaluator::MaxStops)
    return false;
  auto c = m_definition;
  auto sample = GradientEvaluator::evaluate(c, p, m_target, m_engine);
  if (!sample.color)
    return false;
  c.stops.append({QUuid::createUuid().toString(QUuid::WithoutBraces), p,
                  *sample.color, m_target});
  std::stable_sort(
      c.stops.begin(), c.stops.end(),
      [](const auto &a, const auto &b) { return a.position < b.position; });
  return apply(c, "Add gradient stop", true);
}
bool GradientEditorModel::deleteStop(const QString &id) {
  if (m_definition.stops.size() <= 1)
    return false;
  auto c = m_definition;
  auto it = std::find_if(c.stops.begin(), c.stops.end(),
                         [&](const auto &s) { return s.id == id; });
  if (it == c.stops.end())
    return false;
  c.stops.erase(it);
  if (m_selected == id)
    m_selected.clear();
  return apply(c, "Delete gradient stop", true);
}
bool GradientEditorModel::setStopColor(const QString &id, const QString &hex) {
  ColorValue v;
  if (!parseHex(hex, &v))
    return false;
  auto c = m_definition;
  auto it = std::find_if(c.stops.begin(), c.stops.end(),
                         [&](const auto &s) { return s.id == id; });
  if (it == c.stops.end())
    return false;
  it->color = v;
  return apply(c, "Change gradient stop color", true);
}
QByteArray GradientEditorModel::encode() const {
  return GradientDefinitionCodec::encode(m_definition);
}
bool GradientEditorModel::decode(const QByteArray &bytes) {
  OperationError e;
  auto g = GradientDefinitionCodec::decode(bytes, &e);
  if (!g) {
    m_error = e.message;
    emit editRejected(m_error);
    return false;
  }
  return apply(*g, "Load gradient", true);
}
} // namespace QindaTK
