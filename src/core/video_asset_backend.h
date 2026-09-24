// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "asset_provider.h"

namespace QindaTK {
AssetResult fetchVideoAsset(const QString &path, const AssetRequest &request,
                            const CancellationToken &cancel, const JobProgress &progress);
bool isVideoFileSuffix(const QString &suffix);
} // namespace QindaTK
