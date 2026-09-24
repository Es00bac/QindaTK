// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "color_context.h"

namespace QindaTK {

// Synchronous CPU service. Calls are serialized; do not use on a render or
// realtime callback. Share a service or create one per worker. Destruction
// requires callers to have finished. The bounded cache owns its transforms.
class ColorEngine {
public:
    explicit ColorEngine(int maxCachedTransforms = 32);
    ~ColorEngine();
    ColorEngine(const ColorEngine &) = delete;
    ColorEngine &operator=(const ColorEngine &) = delete;
    ColorTransformResult convert(const ColorTransformRequest &request);
    int cachedTransformCount() const;
private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace QindaTK
