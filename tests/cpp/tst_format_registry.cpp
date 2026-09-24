// SPDX-License-Identifier: LGPL-3.0-or-later
#include "format_registry.h"
#include "format_io.h"
#include "rich_text_codec.h"
#include "rich_text_native_format.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <thread>

using namespace QindaTK;
namespace {
bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
std::unique_ptr<PropertyDocument> textDocument(const QString &text, bool extra = false)
{
    auto document = std::make_unique<PropertyDocument>();
    if (!document->defineProperty({QStringLiteral("text"), QMetaType::fromType<QString>(),
                                   QString(), {}, true, false})) return {};
    if (extra && !document->defineProperty({QStringLiteral("rating"), QMetaType::fromType<int>(),
                                            0, {}, true, false})) return {};
    QVariantMap values{{QStringLiteral("text"), text}};
    if (extra) values.insert(QStringLiteral("rating"), 7);
    if (!document->addEntity(EntityId::create(), values)) return {};
    return document;
}
QString textFromNative(const QByteArray &bytes)
{
    const QJsonArray entities = QJsonDocument::fromJson(bytes).object().value(QStringLiteral("entities")).toArray();
    if (entities.isEmpty()) return {};
    return entities.first().toObject().value(QStringLiteral("values")).toObject()
        .value(QStringLiteral("text")).toObject().value(QStringLiteral("value")).toString();
}
class SlowExport final : public FormatProvider {
public:
    mutable std::atomic_bool entered = false;
    QVariantMap metadata() const override
    {
        return {{"formatId", QStringLiteral("test.slow")},
                {"extensions", QStringList{QStringLiteral("slow")}},
                {"operations", QStringList{QStringLiteral("export")}},
                {"editableImport", false}, {"renderOnly", false}, {"roundtrip", false}};
    }
    bool recognizes(const QByteArray &, const QUrl &) const override { return false; }
    FormatPlan preflightImport(const QByteArray &, const QVariantMap &) const override
    { return {{}, {}, {}, {ErrorCode::Unsupported, QStringLiteral("No import"), {}, {}}, {}}; }
    FormatPayload importDocument(const QByteArray &, const QVariantMap &,
                                 const CancellationToken &, const JobProgress &) const override
    { return {{}, {}, {}, {ErrorCode::Unsupported, QStringLiteral("No import"), {}, {}}}; }
    FormatPlan preflightExport(const SnapshotPtr &, const QVariantMap &options) const override
    { return {options, {}, {}, {}, {}}; }
    FormatPayload exportDocument(const SnapshotPtr &, const QVariantMap &options,
                                 const CancellationToken &cancel, const JobProgress &) const override
    {
        entered = true;
        if (options.value(QStringLiteral("failNow")).toBool())
            return {{}, {}, {}, {ErrorCode::BackendFailure, QStringLiteral("Deliberate provider failure"),
                                  QStringLiteral("payload"), QStringLiteral("test.slow")}};
        for (int i = 0; i < 5000; ++i) {
            if (cancel.isCancelled())
                return {{}, {}, {}, {ErrorCode::Cancelled, QStringLiteral("Cancelled"), {}, {}}};
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {{}, {}, {}, {ErrorCode::BackendFailure, QStringLiteral("Timeout"), {}, {}}};
    }
};
}
class TstFormatRegistry final : public QObject {
    Q_OBJECT
private slots:
    void richTextProviderRoundtrip()
    {
        RichTextDocument rich;
        rich.insertText(rich.startPosition(), QStringLiteral("A saved paragraph\nSecond line"));
        const QByteArray source = RichTextCodec::encode(rich);
        const auto provider = makeRichTextNativeFormat();
        FormatRegistry registry;
        QCOMPARE(registry.formatsForOperation(QStringLiteral("roundtrip")).size(), 2);
        QVERIFY(provider->recognizes(source.left(4096), QUrl::fromLocalFile(QStringLiteral("/tmp/example.qtkrich"))));
        const auto inspected = provider->preflightImport(source, {});
        QVERIFY2(!inspected.error, qPrintable(inspected.error.message));
        const auto imported = provider->importDocument(source, {}, {}, {});
        QVERIFY2(!imported.error, qPrintable(imported.error.message));
        OperationError error;
        const auto decoded = ReferenceDocumentCodec::decode(imported.bytes, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error.message));
        const auto snapshot = ReferenceDocumentCodec::capture(*decoded->document, decoded->extensions, &error);
        QVERIFY2(snapshot != nullptr, qPrintable(error.message));
        const auto exportPlan = provider->preflightExport(snapshot, {});
        QVERIFY2(!exportPlan.error, qPrintable(exportPlan.error.message));
        QVERIFY(exportPlan.losses.isEmpty());
        const auto exported = provider->exportDocument(snapshot, {}, {}, {});
        QVERIFY2(!exported.error, qPrintable(exported.error.message));
        QCOMPARE(exported.bytes, source);
    }

    void metadataIsOperationSpecific()
    {
        FormatRegistry formats;
        const auto entries = formats.formats();
        QCOMPARE(entries.size(), 3);
        QCOMPARE(formats.formatsForOperation(QStringLiteral("import")).size(), 3);
        QCOMPARE(formats.formatsForOperation(QStringLiteral("export")).size(), 3);
        const auto roundtrip = formats.formatsForOperation(QStringLiteral("roundtrip"));
        QCOMPARE(roundtrip.size(), 2);
        QCOMPARE(roundtrip.first().toMap().value(QStringLiteral("formatId")).toString(),
                 QStringLiteral("qindatk.reference.v2"));
        QCOMPARE(formats.formatsForOperation(QStringLiteral("render")).size(), 0);
    }
    void nativeRoundtripPreservesExtensionsAndReportsMissingLink()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        DocumentManager source;
        auto *session = source.create(textDocument(QStringLiteral("native ✓")));
        QVERIFY(session);
        const QString missing = temp.filePath(QStringLiteral("missing.png"));
        session->setExtensions({{QStringLiteral("resources"), QJsonArray{
            QJsonObject{{QStringLiteral("uri"), QUrl::fromLocalFile(missing).toString()}}}}});
        FormatRegistry formats;
        const QUrl destination = QUrl::fromLocalFile(temp.filePath(QStringLiteral("sample.qtk")));
        const auto plan = formats.preflightExportSession(session, destination,
                                                         QStringLiteral("qindatk.reference.v2"));
        QVERIFY(plan.value(QStringLiteral("ok")).toBool());
        QCOMPARE(plan.value(QStringLiteral("losses")).toList().size(), 0);
        QCOMPARE(plan.value(QStringLiteral("warnings")).toList().size(), 1);
        QCOMPARE(plan.value(QStringLiteral("preview")).toMap()
                     .value(QStringLiteral("entityCount")).toInt(), 1);
        QSignalSpy finished(formats.jobs(), &JobModel::jobFinished);
        const QString exportId = formats.startExportSession(session, destination,
                                                           QStringLiteral("qindatk.reference.v2"));
        QVERIFY(!exportId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(formats.jobs()->job(exportId).value(QStringLiteral("state")).toString(), QStringLiteral("succeeded"));
        QVERIFY(formats.jobs()->job(exportId).value(QStringLiteral("result")).toMap()
                    .value(QStringLiteral("committed")).toBool());
        OperationError error;
        auto decoded = ReferenceDocumentCodec::decode(readFile(destination.toLocalFile()), &error);
        QVERIFY(decoded);
        QCOMPARE(decoded->extensions.value(QStringLiteral("resources")).toArray().size(), 1);
        QCOMPARE(formats.detectImport(destination), QStringLiteral("qindatk.reference.v2"));
        const auto importPlan = formats.preflightImport(destination);
        QVERIFY(importPlan.value(QStringLiteral("ok")).toBool());
        QCOMPARE(importPlan.value(QStringLiteral("warnings")).toList().size(), 1);
        const QString importId = formats.startImport(destination);
        QVERIFY(!importId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        DocumentManager adopted;
        auto *restored = formats.adoptImport(&adopted, importId);
        QVERIFY(restored);
        QCOMPARE(restored->documentId(), session->documentId());
        QCOMPARE(restored->extensions().value(QStringLiteral("resources")).toArray().size(), 1);
    }
    void utf8TextImportExportAndLossReport()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString phrase = QString::fromUtf8("Hello\r\nمرحبا 世界 🎵\r\n");
        const QString input = temp.filePath(QStringLiteral("multilingual.txt"));
        const QByteArray source = QByteArray("\xef\xbb\xbf") + phrase.toUtf8();
        QVERIFY(writeFile(input, source));
        FormatRegistry formats;
        const QUrl sourceUrl = QUrl::fromLocalFile(input);
        QCOMPARE(formats.detectImport(sourceUrl), QStringLiteral("text.utf8"));
        const auto plan = formats.preflightImport(sourceUrl);
        QVERIFY(plan.value(QStringLiteral("ok")).toBool());
        QCOMPARE(plan.value(QStringLiteral("warnings")).toList().size(), 1);
        QVERIFY(plan.value(QStringLiteral("preview")).toMap()
                    .value(QStringLiteral("excerpt")).toString().startsWith(QStringLiteral("Hello")));
        QSignalSpy finished(formats.jobs(), &JobModel::jobFinished);
        const QString importId = formats.startImport(sourceUrl);
        QVERIFY(!importId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        const QByteArray native = formats.jobs()->job(importId).value(QStringLiteral("result")).toMap()
                                      .value(QStringLiteral("documentBytes")).toByteArray();
        QCOMPARE(textFromNative(native), phrase);
        DocumentManager manager;
        auto *session = formats.adoptImport(&manager, importId);
        QVERIFY(session);
        const QUrl destination = QUrl::fromLocalFile(temp.filePath(QStringLiteral("out.txt")));
        const QVariantMap options{{QStringLiteral("lineEnding"), QStringLiteral("crlf")},
                                  {QStringLiteral("bom"), true}};
        QVERIFY(formats.preflightExportSession(session, destination, QStringLiteral("text.utf8"), options)
                    .value(QStringLiteral("ok")).toBool());
        const QString exportId = formats.startExportSession(session, destination,
                                                            QStringLiteral("text.utf8"), options);
        QVERIFY(!exportId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QCOMPARE(formats.jobs()->job(exportId).value(QStringLiteral("state")).toString(), QStringLiteral("succeeded"));
        QCOMPARE(readFile(destination.toLocalFile()), source);

        auto richer = textDocument(QStringLiteral("Only text"), true);
        QVERIFY(richer);
        const QUrl lossyDestination = QUrl::fromLocalFile(temp.filePath(QStringLiteral("lossy.txt")));
        const auto denied = formats.preflightExport(richer.get(), lossyDestination, QStringLiteral("text.utf8"));
        QVERIFY(!denied.value(QStringLiteral("ok")).toBool());
        QCOMPARE(denied.value(QStringLiteral("losses")).toList().size(), 1);
        QVERIFY(formats.startExport(richer.get(), lossyDestination, QStringLiteral("text.utf8")).isEmpty());
        const QVariantMap acceptedOptions{{QStringLiteral("allowLossy"), true}};
        QVERIFY(formats.preflightExport(richer.get(), lossyDestination,
                                       QStringLiteral("text.utf8"), acceptedOptions).value(QStringLiteral("ok")).toBool());
        const QString lossyId = formats.startExport(richer.get(), lossyDestination,
                                                   QStringLiteral("text.utf8"), acceptedOptions);
        QVERIFY(!lossyId.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 5000);
        QCOMPARE(readFile(lossyDestination.toLocalFile()), QByteArray("Only text"));
        QCOMPARE(formats.jobs()->job(lossyId).value(QStringLiteral("result")).toMap()
                     .value(QStringLiteral("losses")).toList().size(), 1);
    }
    void malformedLargeAndConflictingFixturesAreRejectedWithoutClobber()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FormatRegistry formats;
        const QString changedText = temp.filePath(QStringLiteral("changed.txt"));
        QVERIFY(writeFile(changedText, QByteArray("approved")));
        const QUrl changedUrl = QUrl::fromLocalFile(changedText);
        const auto sourcePlan = formats.preflightImport(changedUrl);
        QVERIFY(sourcePlan.value(QStringLiteral("ok")).toBool());
        const auto approvedSource = sourcePlan.value(QStringLiteral("options")).toMap();
        QVERIFY(!approvedSource.value(QStringLiteral("expectedSourceSha256")).toString().isEmpty());
        QVERIFY(writeFile(changedText, QByteArray("changed")));
        QVERIFY(formats.startImport(changedUrl, QStringLiteral("text.utf8"), approvedSource).isEmpty());
        const QString badText = temp.filePath(QStringLiteral("bad.txt"));
        QVERIFY(writeFile(badText, QByteArray::fromHex("c328")));
        QVERIFY(!formats.preflightImport(QUrl::fromLocalFile(badText))
                    .value(QStringLiteral("ok")).toBool());
        const QString fakePdf = temp.filePath(QStringLiteral("fake.pdf"));
        QVERIFY(writeFile(fakePdf, QByteArray("%PDF-1.7\nlooks like ASCII")));
        QVERIFY(formats.detectImport(QUrl::fromLocalFile(fakePdf)).isEmpty());
        const QString badNative = temp.filePath(QStringLiteral("bad.qtk"));
        QVERIFY(writeFile(badNative, QByteArray("{bad json")));
        QVERIFY(!formats.preflightImport(QUrl::fromLocalFile(badNative))
                    .value(QStringLiteral("ok")).toBool());
        const QString largeText = temp.filePath(QStringLiteral("large.txt"));
        QVERIFY(writeFile(largeText, QByteArray(16 * 1024 * 1024 + 1, 'a')));
        QVERIFY(!formats.preflightImport(QUrl::fromLocalFile(largeText))
                    .value(QStringLiteral("ok")).toBool());
        auto document = textDocument(QStringLiteral("new"));
        QVERIFY(document);
        const QString destinationPath = temp.filePath(QStringLiteral("existing.qtk"));
        const QByteArray original("untouched");
        QVERIFY(writeFile(destinationPath, original));
        const QUrl destination = QUrl::fromLocalFile(destinationPath);
        QVERIFY(!formats.preflightExport(document.get(), destination,
                                        QStringLiteral("qindatk.reference.v2"))
                    .value(QStringLiteral("ok")).toBool());
        QVERIFY(formats.startExport(document.get(), destination,
                                    QStringLiteral("qindatk.reference.v2")).isEmpty());
        QCOMPARE(readFile(destinationPath), original);
        const QVariantMap wrong{{QStringLiteral("conflictPolicy"), QStringLiteral("replaceIfHash")},
                                {QStringLiteral("expectedSha256"), QString(64, QLatin1Char('0'))}};
        QVERIFY(!formats.preflightExport(document.get(), destination,
                                        QStringLiteral("qindatk.reference.v2"), wrong)
                    .value(QStringLiteral("ok")).toBool());
        const QVariantMap replaceRequest{{QStringLiteral("conflictPolicy"),
                                          QStringLiteral("replaceIfHash")}};
        const auto replacementPlan = formats.preflightExport(document.get(), destination,
                                      QStringLiteral("qindatk.reference.v2"), replaceRequest);
        QVERIFY(replacementPlan.value(QStringLiteral("ok")).toBool());
        const QVariantMap replace = replacementPlan.value(QStringLiteral("options")).toMap();
        QCOMPARE(replace.value(QStringLiteral("expectedSha256")).toString(),
                 QString::fromLatin1(FormatIO::hash(original).toHex()));
        const QByteArray raced("changed after preflight");
        QVERIFY(writeFile(destinationPath, raced));
        QVERIFY(formats.startExport(document.get(), destination,
                                    QStringLiteral("qindatk.reference.v2"), replace).isEmpty());
        QCOMPARE(readFile(destinationPath), raced);
        QVERIFY(writeFile(destinationPath, original));
        QSignalSpy finished(formats.jobs(), &JobModel::jobFinished);
        const QString id = formats.startExport(document.get(), destination,
                                               QStringLiteral("qindatk.reference.v2"), replace);
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(formats.jobs()->job(id).value(QStringLiteral("state")).toString(), QStringLiteral("succeeded"));
        QVERIFY(readFile(destinationPath) != original);
    }
    void changedDocumentInvalidatesApprovedExportOptions()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        FormatRegistry formats;
        auto document = textDocument(QStringLiteral("before"));
        QVERIFY(document);
        const QUrl destination = QUrl::fromLocalFile(temp.filePath(QStringLiteral("revision.qtk")));
        const auto plan = formats.preflightExport(document.get(), destination,
                                                  QStringLiteral("qindatk.reference.v2"));
        QVERIFY(plan.value(QStringLiteral("ok")).toBool());
        const auto approved = plan.value(QStringLiteral("options")).toMap();
        QVERIFY(!approved.value(QStringLiteral("expectedSourceRevision")).toString().isEmpty());
        QVERIFY(document->addEntity(EntityId::create(), {{QStringLiteral("text"), QStringLiteral("after")}}));
        QVERIFY(formats.startExport(document.get(), destination,
                                    QStringLiteral("qindatk.reference.v2"), approved).isEmpty());
        QVERIFY(!QFile::exists(destination.toLocalFile()));
    }
    void changedSessionExtensionsInvalidateApprovedSnapshot()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        DocumentManager manager;
        auto *session = manager.create(textDocument(QStringLiteral("body")));
        QVERIFY(session);
        FormatRegistry formats;
        const QUrl destination = QUrl::fromLocalFile(temp.filePath(QStringLiteral("extensions.qtk")));
        const auto plan = formats.preflightExportSession(session, destination,
                                                          QStringLiteral("qindatk.reference.v2"));
        QVERIFY(plan.value(QStringLiteral("ok")).toBool());
        const auto approved = plan.value(QStringLiteral("options")).toMap();
        QVERIFY(!approved.value(QStringLiteral("expectedSnapshotSha256")).toString().isEmpty());
        session->setExtensions({{QStringLiteral("changed"), true}});
        QVERIFY(formats.startExportSession(session, destination,
                                            QStringLiteral("qindatk.reference.v2"), approved).isEmpty());
        QVERIFY(!QFile::exists(destination.toLocalFile()));
    }
    void cancellationLeavesNoDestination()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto document = textDocument(QStringLiteral("cancel"));
        QVERIFY(document);
        FormatRegistry formats;
        auto slow = std::make_shared<SlowExport>();
        QVERIFY(formats.registerProvider(slow));
        const QString path = temp.filePath(QStringLiteral("cancel.slow"));
        const QString id = formats.startExport(document.get(), QUrl::fromLocalFile(path),
                                               QStringLiteral("test.slow"));
        QVERIFY(!id.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(slow->entered.load(), 5000);
        QVERIFY(formats.jobs()->cancelJob(id));
        QTRY_COMPARE_WITH_TIMEOUT(formats.jobs()->job(id).value(QStringLiteral("state")).toString(),
                                  QStringLiteral("cancelled"), 5000);
        QVERIFY(!QFile::exists(path));
    }
    void workerFailureHasStructuredError()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        auto document = textDocument(QStringLiteral("failure"));
        QVERIFY(document);
        FormatRegistry formats;
        QVERIFY(formats.registerProvider(std::make_shared<SlowExport>()));
        const QString id = formats.startExport(document.get(),
            QUrl::fromLocalFile(temp.filePath(QStringLiteral("failure.slow"))),
            QStringLiteral("test.slow"), {{QStringLiteral("failNow"), true}});
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(formats.jobs()->job(id).value(QStringLiteral("state")).toString(),
                                  QStringLiteral("failed"), 5000);
        const QVariantMap result = formats.jobs()->job(id).value(QStringLiteral("result")).toMap();
        QCOMPARE(result.value(QStringLiteral("formatId")).toString(), QStringLiteral("test.slow"));
        const QVariantMap error = result.value(QStringLiteral("error")).toMap();
        QCOMPARE(error.value(QStringLiteral("fieldPath")).toString(), QStringLiteral("payload"));
        QCOMPARE(error.value(QStringLiteral("backendId")).toString(), QStringLiteral("test.slow"));
        QCOMPARE(error.value(QStringLiteral("code")).toInt(), int(ErrorCode::BackendFailure));
    }
};
QTEST_GUILESS_MAIN(TstFormatRegistry)
#include "tst_format_registry.moc"
