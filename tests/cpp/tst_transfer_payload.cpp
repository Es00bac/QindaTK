// SPDX-License-Identifier: LGPL-3.0-or-later
#include "transfer_payload.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QBuffer>
#include <QImage>
#include <QRandomGenerator>
#include <QtTest>

using namespace QindaTK;

namespace {
DocumentId sourceId()
{
    return DocumentId::parse(QStringLiteral("11111111-1111-4111-8111-111111111111"));
}
EntityId entityId(const char *text)
{
    return EntityId::parse(QString::fromLatin1(text));
}
TransferPayload sample(TransferItemKind kind = TransferItemKind::Object,
                       QJsonObject data = {{QStringLiteral("name"), QStringLiteral("clip")}})
{
    return {sourceId(), RevisionId{9007199254740993ULL}, TransferOperation::Move,
            {{entityId("22222222-2222-4222-8222-222222222222"), kind, data}}};
}
QByteArray pngBytes()
{
    return QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/"
        "lS8AAAAASUVORK5CYII=");
}
QByteArray largePngBytes()
{
    QImage image(1024, 1024, QImage::Format_RGB32);
    QRandomGenerator random(12345);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = qRgb(int(random.bounded(256)), int(random.bounded(256)),
                           int(random.bounded(256)));
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return bytes;
}
QByteArray mutateRoot(const QByteArray &bytes, const QString &key,
                      const QJsonValue &value)
{
    QJsonObject root = QJsonDocument::fromJson(bytes).object();
    root.insert(key, value);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
} // namespace

class TstTransferPayload final : public QObject {
    Q_OBJECT
private slots:
    void roundTripsExactRevisionAndOperation()
    {
        OperationError error;
        const auto original = sample(TransferItemKind::Range,
            {{QStringLiteral("start"), QStringLiteral("9007199254740993")},
             {QStringLiteral("end"), QStringLiteral("9007199254741999")},
             {QStringLiteral("unit"), QStringLiteral("samples")}});
        const QByteArray bytes = TransferPayloadCodec::encode(original, &error);
        QVERIFY2(!error, qPrintable(error.message));
        const auto decoded = TransferPayloadCodec::decode(bytes, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.message));
        QCOMPARE(decoded->sourceDocumentId, original.sourceDocumentId);
        QCOMPARE(decoded->sourceRevision.value, original.sourceRevision.value);
        QCOMPARE(decoded->operation, TransferOperation::Move);
        QCOMPARE(decoded->items.front().entityId, original.items.front().entityId);
        QCOMPARE(decoded->items.front().kind, TransferItemKind::Range);
        QCOMPARE(decoded->items.front().payload, original.items.front().payload);
    }

    void advertisesOnlySupportedExternalAlternatives()
    {
        OperationError error;
        auto text = sample(TransferItemKind::Text,
                           {{QStringLiteral("text"), QStringLiteral("hello 🌱")}});
        const auto textMimes = TransferPayloadCodec::mimeAlternatives(text, &error);
        QVERIFY2(!error, qPrintable(error.message));
        QCOMPARE(textMimes.size(), 2);
        QCOMPARE(textMimes.value(QStringLiteral("text/plain")),
                 QString::fromUtf8("hello 🌱").toUtf8());
        QVERIFY(textMimes.contains(QString::fromLatin1(TransferPayloadCodec::InternalMime)));

        auto image = sample(TransferItemKind::Image,
            {{QStringLiteral("pngBase64"), QString::fromLatin1(pngBytes().toBase64())}});
        const auto imageMimes = TransferPayloadCodec::mimeAlternatives(image, &error);
        QVERIFY2(!error, qPrintable(error.message));
        QCOMPARE(imageMimes.value(QStringLiteral("image/png")), pngBytes());

        const auto rangeMimes = TransferPayloadCodec::mimeAlternatives(sample(TransferItemKind::Range), &error);
        QVERIFY2(!error, qPrintable(error.message));
        QCOMPARE(rangeMimes.size(), 1);
        QVERIFY(rangeMimes.contains(QString::fromLatin1(TransferPayloadCodec::InternalMime)));
        const auto multi = TransferPayloadCodec::mimeAlternatives(
            {sourceId(), RevisionId{1}, TransferOperation::Copy,
             {{entityId("22222222-2222-4222-8222-222222222222"), TransferItemKind::Text,
               {{QStringLiteral("text"), QStringLiteral("one")}}},
              {entityId("33333333-3333-4333-8333-333333333333"), TransferItemKind::Text,
               {{QStringLiteral("text"), QStringLiteral("two")}}}}}, &error);
        QVERIFY2(!error, qPrintable(error.message));
        QCOMPARE(multi.size(), 1);
    }

    void rejectsMalformedAndUnsupportedInput()
    {
        OperationError error;
        const QByteArray bytes = TransferPayloadCodec::encode(sample(), &error);
        QVERIFY(!bytes.isEmpty());
        QVERIFY(!TransferPayloadCodec::decode(QByteArray("{"), &error));
        QCOMPARE(error.code, ErrorCode::Validation);
        QVERIFY(!TransferPayloadCodec::decode(mutateRoot(bytes, QStringLiteral("version"), 99), &error));
        QCOMPARE(error.code, ErrorCode::Unsupported);
        QVERIFY(!TransferPayloadCodec::decode(mutateRoot(bytes, QStringLiteral("future"), true), &error));
        QCOMPARE(error.code, ErrorCode::Validation);
        QVERIFY(!TransferPayloadCodec::decode(QByteArray(TransferPayloadCodec::MaximumBytes + 1, 'x'), &error));
        QCOMPARE(error.code, ErrorCode::Validation);
    }

    void rejectsInvalidIdsDuplicatesAndUnknownItemKinds()
    {
        OperationError error;
        const QByteArray bytes = TransferPayloadCodec::encode(sample(), &error);
        QJsonObject root = QJsonDocument::fromJson(bytes).object();
        QJsonArray items = root.value(QStringLiteral("items")).toArray();
        QJsonObject first = items[0].toObject();
        first.insert(QStringLiteral("entityId"), QStringLiteral("not-a-uuid"));
        items[0] = first;
        root.insert(QStringLiteral("items"), items);
        QVERIFY(!TransferPayloadCodec::decode(QJsonDocument(root).toJson(), &error));
        QCOMPARE(error.code, ErrorCode::Validation);

        first.insert(QStringLiteral("entityId"), QStringLiteral("22222222-2222-4222-8222-222222222222"));
        first.insert(QStringLiteral("kind"), QStringLiteral("plugin-private-kind"));
        items[0] = first;
        root.insert(QStringLiteral("items"), items);
        QVERIFY(!TransferPayloadCodec::decode(QJsonDocument(root).toJson(), &error));
        QCOMPARE(error.code, ErrorCode::Unsupported);

        auto duplicate = sample();
        duplicate.items.append(duplicate.items.front());
        QVERIFY(TransferPayloadCodec::encode(duplicate, &error).isEmpty());
        QCOMPARE(error.code, ErrorCode::Validation);
    }

    void boundsTextAndValidatesImage()
    {
        OperationError error;
        auto hugeText = sample(TransferItemKind::Text,
            {{QStringLiteral("text"), QString(TransferPayloadCodec::MaximumTextBytes + 1, QLatin1Char('x'))}});
        QVERIFY(TransferPayloadCodec::encode(hugeText, &error).isEmpty());
        QCOMPARE(error.code, ErrorCode::Validation);
        auto fakeImage = sample(TransferItemKind::Image,
            {{QStringLiteral("pngBase64"), QStringLiteral("bm90IGEgcG5n")}});
        QVERIFY(TransferPayloadCodec::encode(fakeImage, &error).isEmpty());
        QCOMPARE(error.code, ErrorCode::Validation);
        auto validImage = sample(TransferItemKind::Image,
            {{QStringLiteral("pngBase64"), QString::fromLatin1(pngBytes().toBase64())}});
        QVERIFY(!TransferPayloadCodec::encode(validImage, &error).isEmpty());
        QVERIFY(!error);

        auto mediumText = sample(TransferItemKind::Text,
            {{QStringLiteral("text"), QString(1024 * 1024 + 8, QLatin1Char('y'))}});
        QVERIFY2(!TransferPayloadCodec::encode(mediumText, &error).isEmpty(),
                 qPrintable(error.message));

        const QByteArray largePng = largePngBytes();
        QVERIFY(largePng.size() > 1024 * 1024);
        QVERIFY(largePng.size() < TransferPayloadCodec::MaximumImageBytes);
        auto mediumImage = sample(TransferItemKind::Image,
            {{QStringLiteral("pngBase64"), QString::fromLatin1(largePng.toBase64())}});
        QVERIFY2(!TransferPayloadCodec::encode(mediumImage, &error).isEmpty(),
                 qPrintable(error.message));
        const auto mediumMimes = TransferPayloadCodec::mimeAlternatives(mediumImage, &error);
        QVERIFY2(!error, qPrintable(error.message));
        QCOMPARE(mediumMimes.value(QStringLiteral("image/png")), largePng);
    }
};

QTEST_GUILESS_MAIN(TstTransferPayload)
#include "tst_transfer_payload.moc"
