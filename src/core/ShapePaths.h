// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QString>
#include <functional>

class QPainter;

namespace lf {

struct RenderContext;

// Geometry of the ready-made shapes and the drawing helpers shared by the
// shape objects: frames, fills (gradients, hatching) and 1-bit dithering.
// All coordinates are millimetres in the object's local 0..w x 0..h box.
namespace shapes {

enum Kind {
    Polygon,
    Star,
    Triangle,
    RightTriangle,
    Rhombus,
    Parallelogram,
    Trapezoid,
    Cross,
    Arrow,
    DoubleArrow,
    Chevron,
    Callout,
    Banner,
    KindCount
};

QString kindId(int kind);            // stable id for files, e.g. "star"
int kindFromId(const QString& id);   // -1 if unknown
QString kindName(int kind);          // localized
// Meaning of the two parameters; an empty label means the parameter is unused.
QString param1Label(int kind);
QString param2Label(int kind);
void paramRange(int kind, int param, double* min, double* max, double* def, int* decimals);

// Outline of the shape filling the box.
QPainterPath shapePath(int kind, double w, double h, double p1, double p2);

enum Frame { FrameSingle = 0, FrameDouble = 1, FrameThickThin = 2 };
// Filled region of a double or "thick + thin" frame lying inside the path:
// bands at the given inward distances from the outline.
QPainterPath frameRegion(const QPainterPath& path, int frame, double width, double gap);

enum FillType { FillNone = 0, FillSolid = 1, FillLinear = 2, FillRadial = 3, FillPattern = 4 };
enum Pattern { PatHorizontal, PatVertical, PatDiagonal, PatBackDiagonal, PatCross, PatDiagonalCross, PatDots,
               PatternCount };
QString patternId(int pattern);
int patternFromId(const QString& id);
QString patternName(int pattern);

struct FillStyle {
    int type = FillNone;
    QColor color = Qt::black;   // solid colour, first gradient colour, pattern lines
    QColor color2 = Qt::white;  // second gradient colour, pattern background
    double angle = 0;           // linear gradient direction, degrees clockwise from "left to right"
    int pattern = PatDiagonal;
    double spacing = 1.5;       // pattern step, mm
    double lineWidth = 0.3;     // pattern line width / dot diameter, mm
};

// Fills the path. Gradients are dithered to device dots when the context asks
// for 1-bit output, the rest stays vector.
void fillPath(QPainter* p, const QPainterPath& path, const FillStyle& f, const QRectF& box, const RenderContext& ctx);

// Renders paint() into an ARGB image covering `area` (mm) at `dpi`.
QImage rasterize(const QRectF& area, double dpi, const std::function<void(QPainter*)>& paint);
// For 1-bit output: composites over white, dithers, makes white transparent.
QImage ditherOverWhite(const QImage& img);

} // namespace shapes
} // namespace lf
