// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "asset_provider.h"

namespace QindaTK {
// Local PDF pages and metadata, rendered in the JobModel worker by Poppler's
// pdftoppm/pdfinfo executables. A missing executable is an explicit Unsupported
// result. The process boundary keeps malformed document decoding outside Qt.
AssetResult fetchPdfAsset(const QString &path, const AssetRequest &request,
                          const CancellationToken &cancel, const JobProgress &progress);
} // namespace QindaTK
