// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "RenderContext.h"

#include <QImage>

class QPainter;

namespace lf {

class LabelDocument;
class LabelObject;

struct RenderOptions {
    bool printableOnly = true;
    bool background = true;
    bool clip = true;
    bool outline = false;  // draw the label outline (test prints / previews)
};

// Paints one label with the painter positioned at the label's top-left corner,
// painter units = millimetres.
void renderLabel(QPainter* p, const LabelDocument& doc, const RenderContext& ctx,
                 const RenderOptions& opt = {});

// Paints a single object, including its position and rotation.
void renderObject(QPainter* p, const LabelObject& obj, const RenderContext& ctx);

// Rasterizes a label. mono = threshold to pure black/white (thermal printers).
QImage renderLabelImage(const LabelDocument& doc, const RenderContext& ctx, double dpi, bool mono,
                        const RenderOptions& opt = {});

} // namespace lf
