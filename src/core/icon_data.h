// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

namespace QindaTK::IconData {

// One built-in stroked icon: a Lucide name and its SVG path strings in a
// 24x24 viewBox. Generated into icon_data.cpp by tools/scripts/gen_icons.py.
struct BuiltinIcon final {
    const char *name;
    std::vector<const char *> paths;
};

// A deprecated Lucide name and the current name it resolves to.
struct IconAlias final {
    const char *name;
    const char *target;
};

[[nodiscard]] const std::span<const BuiltinIcon> builtinIcons();
[[nodiscard]] const std::span<const IconAlias> iconAliases();

} // namespace QindaTK::IconData
