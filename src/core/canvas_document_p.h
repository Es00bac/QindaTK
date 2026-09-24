// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

namespace QindaTK {

// The recorded canvas step: the applied edit itself (touched objects and
// layer, image and page deltas). Image pointers are shared with the
// document, so a step never copies pixels or the rest of the scene.
class CanvasCommand final : public DocumentCommand {
public:
    explicit CanvasCommand(CanvasEdit edit);
    [[nodiscard]] const CanvasEdit &edit() const { return m_edit; }
    [[nodiscard]] quint64 retainedBytes() const override { return m_bytes; }
    [[nodiscard]] bool isNoOp() const override { return m_edit.isEmpty(); }
    // Object/layer deltas at their value size; an image counts only where the
    // applied step holds the sole reference (a removed or replaced image).
    static quint64 estimate(const CanvasEdit &edit);
    static CanvasEdit composed(const CanvasEdit &first, const CanvasEdit &second);

private:
    CanvasEdit m_edit;
    quint64 m_bytes = 0;
};

} // namespace QindaTK
