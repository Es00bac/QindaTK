// SPDX-License-Identifier: LGPL-3.0-or-later
// The native v6 per-object appearance block (D-106). The native writer puts
// it, length-prefixed, at the end of every object record; the default
// appearance is an empty block, so plain objects grow by four bytes only.
#include "canvas_appearance.h"

#include <QDataStream>
#include <QIODevice>

namespace QindaTK::CanvasAppearanceCodec {
namespace {
void configure(QDataStream &stream)
{
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::BigEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
}

OperationError malformed()
{
    return {ErrorCode::InvalidArgument, QStringLiteral("Malformed object appearance block"),
            QStringLiteral("objects.appearance"), QStringLiteral("canvas.format")};
}

void writeBytes(QDataStream &out, const QByteArray &bytes)
{
    out << quint32(bytes.size());
    out.writeRawData(bytes.constData(), bytes.size());
}

bool readBytes(QDataStream &in, QByteArray &out, qsizetype maximum)
{
    quint32 size = 0;
    in >> size;
    if (in.status() != QDataStream::Ok || qsizetype(size) > maximum
        || qint64(size) > in.device()->bytesAvailable())
        return false;
    out.resize(qsizetype(size));
    return in.readRawData(out.data(), qint64(size)) == qint64(size);
}
} // namespace

QByteArray encode(const CanvasAppearance &a)
{
    if (a.isDefault()) return {};
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    configure(out);
    out << quint8(a.strokeAlignment) << double(a.miterLimit) << double(a.opacity)
        << quint8(a.blend) << quint8(a.pattern.kind);
    if (a.pattern.kind != CanvasPatternKind::None)
        out << a.pattern.ink << double(a.pattern.spacing) << double(a.pattern.angle)
            << double(a.pattern.weight);
    out << quint32(a.effects.size());
    for (const CanvasEffect &effect : a.effects) {
        writeBytes(out, effect.id.toUtf8());
        writeBytes(out, effect.kind.toUtf8());
        out << quint8(effect.enabled);
        writeBytes(out, effect.parameters);
    }
    return bytes;
}

OperationError decode(const QByteArray &bytes, CanvasAppearance *appearance)
{
    *appearance = {};
    if (bytes.isEmpty()) return {};
    if (bytes.size() > MaximumBytes) return malformed();
    QDataStream in(bytes);
    configure(in);
    quint8 alignment = 0, blend = 0, pattern = 0;
    double miterLimit = 0, opacity = 0;
    in >> alignment >> miterLimit >> opacity >> blend >> pattern;
    CanvasAppearance a;
    a.strokeAlignment = CanvasStrokeAlignment(alignment);
    a.miterLimit = miterLimit;
    a.opacity = opacity;
    a.blend = CanvasBlendMode(blend);
    a.pattern.kind = CanvasPatternKind(pattern);
    if (a.pattern.kind != CanvasPatternKind::None) {
        double spacing = 0, angle = 0, weight = 0;
        in >> a.pattern.ink >> spacing >> angle >> weight;
        a.pattern.spacing = spacing;
        a.pattern.angle = angle;
        a.pattern.weight = weight;
    }
    quint32 count = 0;
    in >> count;
    if (in.status() != QDataStream::Ok
        || qsizetype(count) > CanvasAppearanceRules::MaximumEffects)
        return malformed();
    const qsizetype tokenBytes = CanvasAppearanceRules::MaximumTokenLength * 4;
    for (quint32 i = 0; i < count; ++i) {
        CanvasEffect effect;
        QByteArray id, kind;
        quint8 enabled = 0;
        if (!readBytes(in, id, tokenBytes) || !readBytes(in, kind, tokenBytes)) return malformed();
        in >> enabled;
        if (in.status() != QDataStream::Ok || enabled > 1
            || !readBytes(in, effect.parameters, CanvasAppearanceRules::MaximumParameterBytes))
            return malformed();
        effect.id = QString::fromUtf8(id);
        effect.kind = QString::fromUtf8(kind);
        effect.enabled = enabled == 1;
        a.effects.append(effect);
    }
    if (in.status() != QDataStream::Ok || !in.atEnd()) return malformed();
    *appearance = a;
    return {};
}

} // namespace QindaTK::CanvasAppearanceCodec
