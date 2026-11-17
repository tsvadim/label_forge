// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <memory>

class QPainter;

namespace lf {

struct RenderContext;

// Base class of everything that can be placed on a label.
// Geometry is in millimetres: (x, y) is the top-left corner of the unrotated
// box, rotation (degrees, clockwise) is applied around the box centre.
class LabelObject {
public:
    virtual ~LabelObject() = default;

    virtual QString typeName() const = 0;     // stable id used in files
    virtual QString displayType() const = 0;  // localized, for the UI
    // Paints in local coordinates: (0,0)-(w,h) millimetres.
    virtual void paint(QPainter* p, const RenderContext& ctx) const = 0;

    virtual QJsonObject toJson() const;
    virtual void fromJson(const QJsonObject& o);

    // Paints the drop shadow, if any, and then the object; use this rather
    // than paint() when drawing a label.
    void draw(QPainter* p, const RenderContext& ctx) const;
    virtual bool supportsShadow() const { return true; }
    // Outline for a sharp vector shadow; empty = take it from a raster of paint().
    virtual QPainterPath silhouette(const RenderContext& ctx) const;
    // How far the shadow reaches outside the box, mm (0 without a shadow).
    double shadowReach() const;

    std::unique_ptr<LabelObject> clone() const;
    static std::unique_ptr<LabelObject> create(const QString& typeName);
    static std::unique_ptr<LabelObject> fromJsonAny(const QJsonObject& o);

    QRectF rect() const { return QRectF(x, y, w, h); }

    QString name;
    double x = 0, y = 0, w = 20, h = 10;
    double rotation = 0;
    bool locked = false;
    bool printable = true;

    bool shadow = false;
    double shadowDx = 1.0, shadowDy = 1.0;  // mm
    QColor shadowColor = Qt::black;
    double shadowOpacity = 0.6;
    double shadowBlur = 0;  // mm, 0 = sharp (stays vector)

    // Colours taken from a data field: property key ("color", "fill",
    // "stroke", "tint") -> template such as "{Colour}". An empty or invalid
    // value, and the design view, keep the object's own colour.
    QHash<QString, QString> colorFields;
    QColor boundColor(const QString& key, const QColor& own, const RenderContext& ctx) const;
    // #RGB, #RRGGBB, #AARRGGBB (also without '#') or an SVG colour name.
    static QColor parseColor(const QString& s, const QColor& fallback);

private:
    void paintShadow(QPainter* p, const RenderContext& ctx) const;
};

class TextObject : public LabelObject {
public:
    TextObject();
    QString typeName() const override { return QStringLiteral("text"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    QPainterPath silhouette(const RenderContext& ctx) const override;

    QString text;
    QString fontFamily = QStringLiteral("Arial");
    double fontSize = 12;  // points
    bool bold = false, italic = false, underline = false;
    QColor color = Qt::black;
    QColor fill = Qt::transparent;
    int hAlign = 0, vAlign = 0;
    bool wordWrap = true;
    bool autoShrink = false;
    double lineSpacing = 1.0;

private:
    const QPainterPath& textPath(const RenderContext& ctx) const;
    mutable QString m_cacheKey;
    mutable QPainterPath m_cachePath;
};

// Closed shape with an outline (single, double or thick + thin frame) and a
// fill (solid, gradient or hatching); see ShapePaths.h for the drawing.
class ShapeObject : public LabelObject {
public:
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    QPainterPath silhouette(const RenderContext& ctx) const override;

    QColor stroke = Qt::black;
    double strokeWidth = 0.3;  // mm, 0 = no outline
    int strokeStyle = 1;       // Qt::PenStyle, single frame only
    int frame = 0;             // shapes::Frame
    double frameGap = 0.6;     // mm between the lines of a double frame

    int fillType = 0;          // shapes::FillType
    QColor fill = Qt::transparent;  // solid colour, first gradient colour, hatching
    QColor fill2 = Qt::white;       // second gradient colour, hatching background
    double gradientAngle = 0;       // degrees
    int pattern = 2;                // shapes::Pattern
    double patternSpacing = 1.5;    // mm
    double patternWidth = 0.3;      // mm

protected:
    // The shape inside the box shrunk by `inset` on every side.
    virtual QPainterPath outline(double inset) const = 0;

private:
    QPainterPath frameRegionCached() const;
    mutable QString m_frameKey;
    mutable QPainterPath m_frame;
};

class RectObject : public ShapeObject {
public:
    QString typeName() const override { return QStringLiteral("rect"); }
    QString displayType() const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    double radius = 0;

protected:
    QPainterPath outline(double inset) const override;
};

class EllipseObject : public ShapeObject {
public:
    QString typeName() const override { return QStringLiteral("ellipse"); }
    QString displayType() const override;

protected:
    QPainterPath outline(double inset) const override;
};

// Ready-made shape: polygon, star, arrow, callout... (shapes::Kind) with two
// kind-specific parameters (counts, or percent of the box).
class PolyShapeObject : public ShapeObject {
public:
    PolyShapeObject();
    QString typeName() const override { return QStringLiteral("shape"); }
    QString displayType() const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    void setKind(int kind);  // also resets the parameters to the kind's defaults

    int kind = 0;
    double param1 = 6, param2 = 0;

protected:
    QPainterPath outline(double inset) const override;
};

// Horizontal line through the middle of its box; rotate it for other angles.
class LineObject : public LabelObject {
public:
    enum Cap { CapNone = 0, CapArrow = 1, CapTriangle = 2, CapDot = 3, CapBar = 4 };

    LineObject();
    QString typeName() const override { return QStringLiteral("line"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    QPainterPath silhouette(const RenderContext& ctx) const override;
    QColor color = Qt::black;
    double width = 0.3;
    int penStyle = 1;  // Qt::PenStyle
    int startCap = CapNone, endCap = CapNone;
    double capSize = 4;  // cap length in line widths

private:
    double capLength() const;
    QPainterPath capPath(int cap, bool atEnd) const;
    void shaftEnds(double* x0, double* x1) const;
};

// Raster (embedded PNG or file) or SVG (embedded or file) image. Crop, flip,
// opacity and fitting are done by the painter, so SVG stays vector in PDF;
// brightness/contrast/grayscale and dithering work on a raster copy.
class ImageObject : public LabelObject {
public:
    enum Fit { FitNone = 0, FitContain = 1, FitStretch = 2 };

    ImageObject();
    QString typeName() const override { return QStringLiteral("image"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    void setEmbedded(const QImage& img);
    void setEmbeddedSvg(const QByteArray& svg);
    // Embeds a raster file (EXIF-rotated, reduced to 2000 px) or an SVG file.
    bool loadFile(const QString& path, QString* err);
    // Size of the uncropped source in millimetres, empty if there is none.
    QSizeF sourceSizeMm(const RenderContext& ctx) const;

    QByteArray imageData;  // embedded PNG
    QByteArray svgData;    // or embedded SVG
    QString pathTemplate;  // or a file path, may contain {Field}
    int fit = FitContain;
    int hAlign = 1, vAlign = 1;  // 0 left/top, 1 centre, 2 right/bottom
    double cropLeft = 0, cropTop = 0, cropRight = 0, cropBottom = 0;  // percent
    bool flipH = false, flipV = false;
    double opacity = 1.0;
    int brightness = 0, contrast = 0;  // -100..100
    bool monochrome = false;           // grayscale
    bool recolor = false;              // paint a one-colour icon in tintColor
    QColor tintColor = Qt::black;
    bool dither = false;               // halftone instead of threshold on thermal printers

private:
    struct Cache;
    Cache& cache() const;
    bool loadSource(const RenderContext& ctx) const;  // fills the cache, false if nothing to draw
    mutable std::shared_ptr<Cache> m_cache;
};

class BarcodeObject : public LabelObject {
public:
    BarcodeObject();
    QString typeName() const override { return QStringLiteral("barcode"); }
    QString displayType() const override;
    void paint(QPainter* p, const RenderContext& ctx) const override;
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject& o) override;
    bool supportsShadow() const override { return false; }  // a shadow confuses scanners

    QString symbology = QStringLiteral("code128");
    QString data = QStringLiteral("1234567890");
    bool showText = true;
    QString fontFamily = QStringLiteral("Arial");
    double textSize = 8;
    QColor color = Qt::black;
    bool quietZone = true;
    int qrEcc = 1;           // 0 L, 1 M, 2 Q, 3 H
    bool code39Check = false;
};

} // namespace lf
