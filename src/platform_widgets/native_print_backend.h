// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_output.h"

namespace QindaTK {

// The platform's printers through Qt PrintSupport (CUPS on Linux), plus
// "print-to-file": the same print engine writing a PDF, committed on finish.
// Optional (qindatk_widgets, built when Qt PrintSupport is found); register it
// once with PrintAdapter::registerDefaultBackend after creating the
// application object. Destination IDs: "printer:<name>" and "print-to-file".
// AGENT-NOTE (D-131): CUPS receives the job when finish() succeeds; before
// that, abort() discards everything, so deliveredSheets() stays 0.
class NativePrintBackend final : public Print::Backend {
public:
    static QString printerId(const QString &printerName) { return QStringLiteral("printer:") + printerName; }
    static QString fileDestinationId() { return QStringLiteral("print-to-file"); }
    QVector<Print::Capabilities> destinations() override;
    std::unique_ptr<Print::Device> open(const Print::Ticket &ticket, const Print::Capabilities &destination,
                                        QString *error) override;
};

} // namespace QindaTK
