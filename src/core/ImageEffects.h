// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>

namespace lf {

struct ImageAdjust {
    int brightness = 0;   // -100..100
    int contrast = 0;     // -100..100
    bool grayscale = false;
    QColor tint;          // invalid = keep colours

    bool isIdentity() const { return brightness == 0 && contrast == 0 && !grayscale && !tint.isValid(); }
};

// Applies brightness/contrast/grayscale, then the tint. Tinting treats the image
// as a one-colour icon: dark pixels take the tint colour, light ones become
// transparent. Returns Format_ARGB32.
QImage applyAdjust(const QImage& src, const ImageAdjust& a);

// Floyd-Steinberg dithering to pure black and white for thermal printers.
// The image is composited over white; pixels that were mostly transparent stay
// transparent so objects underneath remain visible. Returns Format_ARGB32 with
// only opaque black, opaque white and fully transparent pixels.
QImage ditherToMono(const QImage& src);

// Rewrites fill/stroke/stop-color values of an SVG (attributes, style="" and
// <style> blocks) to the given colour, leaving "none", url(...) and near-white
// colours alone, so a one-colour icon can be recoloured and stay vector.
// Returns the input unchanged if it is not parseable XML (e.g. gzipped .svgz).
QByteArray recolorSvg(const QByteArray& svg, const QColor& color);

bool isSvgData(const QByteArray& data);

} // namespace lf
