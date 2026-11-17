// SPDX-License-Identifier: GPL-3.0-or-later
#include "LabelObject.h"

#include "ImageEffects.h"
#include "RenderContext.h"
#include "ShapePaths.h"
#include "TextLayout.h"
#include "barcode/Barcode.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QPainter>
#include <QPainterPathStroker>
#include <QSvgRenderer>
#include <algorithm>
#include <cmath>

namespace lf {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("LabelObject", s); }

QString colorToString(const QColor& c) { return c.name(QColor::HexArgb); }
QColor colorFrom(const QJsonValue& v, const QColor& def)
{
    if (!v.isString())
        return def;
    QColor c(v.toString());
    return c.isValid() ? c : def;
}
double num(const QJsonObject& o, const char* key, double def)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? v.toDouble() : def;
}
bool flag(const QJsonObject& o, const char* key, bool def)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : def;
}
QString str(const QJsonObject& o, const char* key, const QString& def)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : def;
}

// Snap a size to whole device dots when painting on a raster device.
double snapToDots(double mm, double dpi, int count)
{
    if (dpi <= 0 || count <= 0)
        return mm / std::max(1, count);
    const double dot = MM_PER_INCH / dpi;
    double m = std::floor(mm / count / dot) * dot;
    if (m < dot)
        m = dot;
    return m;
}

// Position of the local origin snapped to the nearest device pixel.
QPointF pixelAlignedOrigin(QPainter* p, const QPointF& local, double dpi)
{
    if (dpi <= 0)
        return local;
    const QTransform t = p->deviceTransform();
    if (std::abs(t.m12()) > 1e-9 || std::abs(t.m21()) > 1e-9)
        return local;  // rotated: leave as is
    const QPointF dev = t.map(local);
    const QPointF snapped(std::round(dev.x()), std::round(dev.y()));
    return t.inverted().map(snapped);
}
} // namespace

// ================================================================ base

QJsonObject LabelObject::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("type")] = typeName();
    if (!name.isEmpty())
        o[QStringLiteral("name")] = name;
    o[QStringLiteral("x")] = x;
    o[QStringLiteral("y")] = y;
    o[QStringLiteral("w")] = w;
    o[QStringLiteral("h")] = h;
    if (rotation != 0)
        o[QStringLiteral("rotation")] = rotation;
    if (locked)
        o[QStringLiteral("locked")] = true;
    if (!printable)
        o[QStringLiteral("printable")] = false;
    if (shadow) {
        QJsonObject s;
        s[QStringLiteral("dx")] = shadowDx;
        s[QStringLiteral("dy")] = shadowDy;
        s[QStringLiteral("color")] = colorToString(shadowColor);
        s[QStringLiteral("opacity")] = shadowOpacity;
        if (shadowBlur > 0)
            s[QStringLiteral("blur")] = shadowBlur;
        o[QStringLiteral("shadow")] = s;
    }
    return o;
}

void LabelObject::fromJson(const QJsonObject& o)
{
    name = str(o, "name", QString());
    x = num(o, "x", 0);
    y = num(o, "y", 0);
    w = std::max(0.1, num(o, "w", 20));
    h = std::max(0.1, num(o, "h", 10));
    rotation = num(o, "rotation", 0);
    locked = flag(o, "locked", false);
    printable = flag(o, "printable", true);
    const QJsonValue sv = o.value(QLatin1String("shadow"));
    shadow = sv.isObject();
    const QJsonObject s = sv.toObject();
    shadowDx = std::clamp(num(s, "dx", 1.0), -50.0, 50.0);
    shadowDy = std::clamp(num(s, "dy", 1.0), -50.0, 50.0);
    shadowColor = colorFrom(s.value(QLatin1String("color")), Qt::black);
    shadowOpacity = std::clamp(num(s, "opacity", 0.6), 0.0, 1.0);
    shadowBlur = std::clamp(num(s, "blur", 0), 0.0, 20.0);
}

void LabelObject::draw(QPainter* p, const RenderContext& ctx) const
{
    if (shadow && supportsShadow() && shadowOpacity > 0 && shadowColor.alpha() > 0)
        paintShadow(p, ctx);
    paint(p, ctx);
}

QPainterPath LabelObject::silhouette(const RenderContext&) const { return QPainterPath(); }

double LabelObject::shadowReach() const
{
    if (!shadow || !supportsShadow())
        return 0;
    return std::max(std::abs(shadowDx), std::abs(shadowDy)) + shadowBlur * 1.5 + 0.5;
}

void LabelObject::paintShadow(QPainter* p, const RenderContext& ctx) const
{
    QColor c = shadowColor;
    c.setAlphaF(float(c.alphaF() * shadowOpacity));
    const QPainterPath sil = silhouette(ctx);
    if (shadowBlur <= 0 && !sil.isEmpty()) {
        p->fillPath(sil.translated(shadowDx, shadowDy), c);  // sharp: stays vector
        return;
    }
    // soft shadow (or an object without a vector outline): a blurred raster
    const double dpi = ctx.deviceDpi > 0 ? ctx.deviceDpi : 300;
    const double pad = shadowBlur * 1.5 + 0.5;
    const QRectF area = (sil.isEmpty() ? QRectF(0, 0, w, h) : sil.boundingRect()).adjusted(-pad, -pad, pad, pad);
    QImage img = shapes::rasterize(area, dpi, [&](QPainter* q) {
        if (!sil.isEmpty()) {
            q->fillPath(sil, Qt::black);
        } else {
            RenderContext plain = ctx;
            plain.monoDither = false;
            paint(q, plain);
        }
    });
    img = blurSilhouette(img, int(std::lround(mmToDots(shadowBlur, dpi))), c);
    p->save();
    if (ctx.monoDither) {
        img = shapes::ditherOverWhite(img);
        p->setRenderHint(QPainter::SmoothPixmapTransform, false);
    }
    p->drawImage(area.translated(shadowDx, shadowDy), img);
    p->restore();
}

std::unique_ptr<LabelObject> LabelObject::clone() const
{
    auto o = create(typeName());
    if (o)
        o->fromJson(toJson());
    return o;
}

std::unique_ptr<LabelObject> LabelObject::create(const QString& t)
{
    if (t == QLatin1String("text"))
        return std::make_unique<TextObject>();
    if (t == QLatin1String("rect"))
        return std::make_unique<RectObject>();
    if (t == QLatin1String("ellipse"))
        return std::make_unique<EllipseObject>();
    if (t == QLatin1String("shape"))
        return std::make_unique<PolyShapeObject>();
    if (t == QLatin1String("line"))
        return std::make_unique<LineObject>();
    if (t == QLatin1String("image"))
        return std::make_unique<ImageObject>();
    if (t == QLatin1String("barcode"))
        return std::make_unique<BarcodeObject>();
    return nullptr;
}

std::unique_ptr<LabelObject> LabelObject::fromJsonAny(const QJsonObject& o)
{
    auto obj = create(o.value(QStringLiteral("type")).toString());
    if (obj)
        obj->fromJson(o);
    return obj;
}

// ================================================================ text

TextObject::TextObject() : text(tr("Текст")) { w = 40; h = 8; }

QString TextObject::displayType() const { return tr("Текст"); }

void TextObject::paint(QPainter* p, const RenderContext& ctx) const
{
    if (fill.alpha() > 0)
        p->fillRect(QRectF(0, 0, w, h), fill);
    const QPainterPath& path = textPath(ctx);
    if (path.isEmpty())
        return;
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawPath(path);
}

QPainterPath TextObject::silhouette(const RenderContext& ctx) const
{
    QPainterPath s = textPath(ctx);
    if (fill.alpha() > 0) {
        s = QPainterPath();
        s.addRect(QRectF(0, 0, w, h));
    }
    return s;
}

const QPainterPath& TextObject::textPath(const RenderContext& ctx) const
{
    const QString s = expandTemplate(text, ctx);
    if (s.isEmpty()) {
        static const QPainterPath empty;
        return empty;
    }
    const QString key = s + QLatin1Char('\x1f') + fontFamily +
                        QStringLiteral("|%1|%2%3%4|%5|%6|%7%8|%9|%10x%11")
                            .arg(fontSize)
                            .arg(bold)
                            .arg(italic)
                            .arg(underline)
                            .arg(hAlign)
                            .arg(vAlign)
                            .arg(wordWrap)
                            .arg(autoShrink)
                            .arg(lineSpacing)
                            .arg(w)
                            .arg(h);
    if (key != m_cacheKey) {
        TextStyle st;
        st.family = fontFamily;
        st.sizePt = fontSize;
        st.bold = bold;
        st.italic = italic;
        st.underline = underline;
        st.hAlign = hAlign;
        st.vAlign = vAlign;
        st.wordWrap = wordWrap;
        st.autoShrink = autoShrink;
        st.lineSpacing = lineSpacing;
        m_cachePath = layoutText(s, st, w, h);
        m_cacheKey = key;
    }
    return m_cachePath;
}

QJsonObject TextObject::toJson() const
{
    QJsonObject o = LabelObject::toJson();
    o[QStringLiteral("text")] = text;
    o[QStringLiteral("font")] = fontFamily;
    o[QStringLiteral("size")] = fontSize;
    o[QStringLiteral("bold")] = bold;
    o[QStringLiteral("italic")] = italic;
    o[QStringLiteral("underline")] = underline;
    o[QStringLiteral("color")] = colorToString(color);
    o[QStringLiteral("fill")] = colorToString(fill);
    o[QStringLiteral("hAlign")] = hAlign;
    o[QStringLiteral("vAlign")] = vAlign;
    o[QStringLiteral("wrap")] = wordWrap;
    o[QStringLiteral("autoShrink")] = autoShrink;
    o[QStringLiteral("lineSpacing")] = lineSpacing;
    return o;
}

void TextObject::fromJson(const QJsonObject& o)
{
    LabelObject::fromJson(o);
    text = str(o, "text", QString());
    fontFamily = str(o, "font", QStringLiteral("Arial"));
    fontSize = num(o, "size", 12);
    bold = flag(o, "bold", false);
    italic = flag(o, "italic", false);
    underline = flag(o, "underline", false);
    color = colorFrom(o.value(QStringLiteral("color")), Qt::black);
    fill = colorFrom(o.value(QStringLiteral("fill")), Qt::transparent);
    hAlign = int(num(o, "hAlign", 0));
    vAlign = int(num(o, "vAlign", 0));
    wordWrap = flag(o, "wrap", true);
    autoShrink = flag(o, "autoShrink", false);
    lineSpacing = num(o, "lineSpacing", 1.0);
    m_cacheKey.clear();
}

// ================================================================ shapes

namespace {
const char* const FRAME_IDS[] = {"single", "double", "thick-thin"};
const char* const FILL_IDS[] = {"none", "solid", "linear", "radial", "pattern"};
const char* const CAP_IDS[] = {"none", "arrow", "triangle", "dot", "bar"};

int indexOf(const char* const* ids, int n, const QString& id, int def)
{
    for (int i = 0; i < n; ++i)
        if (id == QLatin1String(ids[i]))
            return i;
    return def;
}
} // namespace

QJsonObject ShapeObject::toJson() const
{
    QJsonObject o = LabelObject::toJson();
    o[QStringLiteral("stroke")] = colorToString(stroke);
    o[QStringLiteral("strokeWidth")] = strokeWidth;
    o[QStringLiteral("fill")] = colorToString(fill);
    if (strokeStyle != 1)
        o[QStringLiteral("strokeStyle")] = strokeStyle;
    if (frame != shapes::FrameSingle) {
        o[QStringLiteral("frame")] = QLatin1String(FRAME_IDS[std::clamp(frame, 0, 2)]);
        o[QStringLiteral("frameGap")] = frameGap;
    }
    o[QStringLiteral("fillType")] = QLatin1String(FILL_IDS[std::clamp(fillType, 0, 4)]);
    if (fillType == shapes::FillLinear || fillType == shapes::FillRadial || fillType == shapes::FillPattern)
        o[QStringLiteral("fill2")] = colorToString(fill2);
    if (fillType == shapes::FillLinear)
        o[QStringLiteral("gradientAngle")] = gradientAngle;
    if (fillType == shapes::FillPattern) {
        o[QStringLiteral("pattern")] = shapes::patternId(pattern);
        o[QStringLiteral("patternSpacing")] = patternSpacing;
        o[QStringLiteral("patternWidth")] = patternWidth;
    }
    return o;
}

void ShapeObject::fromJson(const QJsonObject& o)
{
    LabelObject::fromJson(o);
    stroke = colorFrom(o.value(QStringLiteral("stroke")), Qt::black);
    strokeWidth = std::max(0.0, num(o, "strokeWidth", 0.3));
    fill = colorFrom(o.value(QStringLiteral("fill")), Qt::transparent);
    strokeStyle = std::clamp(int(num(o, "strokeStyle", 1)), 1, 5);
    frame = indexOf(FRAME_IDS, 3, str(o, "frame", QString()), shapes::FrameSingle);
    frameGap = std::clamp(num(o, "frameGap", 0.6), 0.05, 20.0);
    // files written before "fillType" existed: a visible fill colour means a solid fill
    const int legacy = fill.alpha() > 0 ? shapes::FillSolid : shapes::FillNone;
    fillType = indexOf(FILL_IDS, 5, str(o, "fillType", QString()), legacy);
    fill2 = colorFrom(o.value(QStringLiteral("fill2")), Qt::white);
    gradientAngle = num(o, "gradientAngle", 0);
    const int pat = shapes::patternFromId(str(o, "pattern", QString()));
    pattern = pat >= 0 ? pat : shapes::PatDiagonal;
    patternSpacing = std::clamp(num(o, "patternSpacing", 1.5), 0.2, 50.0);
    patternWidth = std::clamp(num(o, "patternWidth", 0.3), 0.05, 20.0);
    m_frameKey.clear();
}

QPainterPath ShapeObject::frameRegionCached() const
{
    // the file form covers every parameter of the outline, including the subclass ones
    const QString key = QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
    if (key != m_frameKey) {
        m_frame = shapes::frameRegion(outline(0), frame, strokeWidth, frameGap);
        m_frameKey = key;
    }
    return m_frame;
}

void ShapeObject::paint(QPainter* p, const RenderContext& ctx) const
{
    shapes::FillStyle fs;
    fs.type = fillType;
    fs.color = fill;
    fs.color2 = fill2;
    fs.angle = gradientAngle;
    fs.pattern = pattern;
    fs.spacing = patternSpacing;
    fs.lineWidth = patternWidth;
    const QRectF box(0, 0, w, h);
    const bool hasStroke = strokeWidth > 0 && stroke.alpha() > 0;
    if (frame == shapes::FrameSingle || !hasStroke) {
        // the outline stays inside the box
        const QPainterPath path = outline(hasStroke ? strokeWidth / 2 : 0);
        shapes::fillPath(p, path, fs, box, ctx);
        if (hasStroke) {
            QPen pen(stroke, strokeWidth);
            pen.setJoinStyle(Qt::MiterJoin);
            pen.setStyle(Qt::PenStyle(strokeStyle));
            p->strokePath(path, pen);
        }
        return;
    }
    shapes::fillPath(p, outline(0), fs, box, ctx);
    p->fillPath(frameRegionCached(), stroke);
}

QPainterPath ShapeObject::silhouette(const RenderContext&) const
{
    const bool hasStroke = strokeWidth > 0 && stroke.alpha() > 0;
    if (fillType != shapes::FillNone && !(fillType == shapes::FillSolid && fill.alpha() == 0))
        return outline(0);
    if (!hasStroke)
        return QPainterPath();
    if (frame != shapes::FrameSingle)
        return frameRegionCached();
    QPainterPathStroker s;
    s.setWidth(strokeWidth);
    s.setJoinStyle(Qt::MiterJoin);
    s.setDashPattern(Qt::PenStyle(strokeStyle));
    return s.createStroke(outline(strokeWidth / 2));
}

QString RectObject::displayType() const { return tr("Прямоугольник"); }

QPainterPath RectObject::outline(double inset) const
{
    QPainterPath p;
    const QRectF r = QRectF(0, 0, w, h).adjusted(inset, inset, -inset, -inset);
    if (radius > 0)
        p.addRoundedRect(r, radius, radius);
    else
        p.addRect(r);
    return p;
}

QJsonObject RectObject::toJson() const
{
    QJsonObject o = ShapeObject::toJson();
    o[QStringLiteral("radius")] = radius;
    return o;
}

void RectObject::fromJson(const QJsonObject& o)
{
    ShapeObject::fromJson(o);
    radius = num(o, "radius", 0);
}

QString EllipseObject::displayType() const { return tr("Эллипс"); }

QPainterPath EllipseObject::outline(double inset) const
{
    QPainterPath p;
    p.addEllipse(QRectF(0, 0, w, h).adjusted(inset, inset, -inset, -inset));
    return p;
}

PolyShapeObject::PolyShapeObject()
{
    w = 20;
    h = 20;
    setKind(shapes::Star);
}

QString PolyShapeObject::displayType() const { return shapes::kindName(kind); }

void PolyShapeObject::setKind(int k)
{
    kind = std::clamp(k, 0, int(shapes::KindCount) - 1);
    double mn, mx;
    int dec;
    shapes::paramRange(kind, 1, &mn, &mx, &param1, &dec);
    shapes::paramRange(kind, 2, &mn, &mx, &param2, &dec);
}

QPainterPath PolyShapeObject::outline(double inset) const
{
    return shapes::shapePath(kind, std::max(0.01, w - 2 * inset), std::max(0.01, h - 2 * inset), param1, param2)
        .translated(inset, inset);
}

QJsonObject PolyShapeObject::toJson() const
{
    QJsonObject o = ShapeObject::toJson();
    o[QStringLiteral("kind")] = shapes::kindId(kind);
    o[QStringLiteral("param1")] = param1;
    o[QStringLiteral("param2")] = param2;
    return o;
}

void PolyShapeObject::fromJson(const QJsonObject& o)
{
    ShapeObject::fromJson(o);
    const int k = shapes::kindFromId(str(o, "kind", QString()));
    setKind(k >= 0 ? k : shapes::Polygon);
    double mn, mx, def;
    int dec;
    shapes::paramRange(kind, 1, &mn, &mx, &def, &dec);
    param1 = std::clamp(num(o, "param1", def), mn, mx);
    shapes::paramRange(kind, 2, &mn, &mx, &def, &dec);
    param2 = std::clamp(num(o, "param2", def), mn, mx);
}

// ================================================================ line

LineObject::LineObject() { w = 30; h = 2; }

QString LineObject::displayType() const { return tr("Линия"); }

double LineObject::capLength() const { return std::max(0.5, std::clamp(capSize, 1.0, 20.0) * std::max(width, 0.1)); }

// Where the drawn shaft starts and ends: a filled head covers the shaft end,
// so the flat line end does not poke out of its tip.
void LineObject::shaftEnds(double* x0, double* x1) const
{
    const double len = capLength();
    *x0 = startCap == CapTriangle ? len * 0.8 : startCap == CapArrow ? width / 2 : 0;
    *x1 = w - (endCap == CapTriangle ? len * 0.8 : endCap == CapArrow ? width / 2 : 0);
}

QPainterPath LineObject::capPath(int cap, bool atEnd) const
{
    QPainterPath path;
    const double len = capLength(), cy = h / 2;
    const double tip = atEnd ? w : 0;
    const double dir = atEnd ? -1 : 1;  // from the tip into the line
    switch (cap) {
    case CapTriangle: {
        QPolygonF t{{tip, cy}, {tip + dir * len, cy - len * 0.4}, {tip + dir * len, cy + len * 0.4}};
        path.addPolygon(t);
        path.closeSubpath();
        break;
    }
    case CapArrow: {
        // open head drawn with the line's width; its outer corner sits on the tip
        const double t = tip + dir * width;
        QPainterPath v;
        v.moveTo(t + dir * len, cy - len * 0.45);
        v.lineTo(t, cy);
        v.lineTo(t + dir * len, cy + len * 0.45);
        QPainterPathStroker s;
        s.setWidth(width);
        s.setJoinStyle(Qt::MiterJoin);
        s.setCapStyle(Qt::FlatCap);
        path = s.createStroke(v);
        break;
    }
    case CapDot: {
        const double r = len * 0.3;
        path.addEllipse(QPointF(tip + dir * r, cy), r, r);
        break;
    }
    case CapBar: {
        const double bw = std::max(width, 0.2);
        path.addRect(QRectF(atEnd ? tip - bw : tip, cy - len / 2, bw, len));
        break;
    }
    default: break;
    }
    return path;
}

void LineObject::paint(QPainter* p, const RenderContext&) const
{
    QPen pen(color, width);
    pen.setStyle(Qt::PenStyle(qBound(1, penStyle, 5)));
    pen.setCapStyle(Qt::FlatCap);
    p->setPen(pen);
    double x0, x1;
    shaftEnds(&x0, &x1);
    if (x1 > x0)
        p->drawLine(QPointF(x0, h / 2), QPointF(x1, h / 2));
    p->setPen(Qt::NoPen);
    if (startCap != CapNone)
        p->fillPath(capPath(startCap, false), color);
    if (endCap != CapNone)
        p->fillPath(capPath(endCap, true), color);
}

QPainterPath LineObject::silhouette(const RenderContext&) const
{
    double x0, x1;
    shaftEnds(&x0, &x1);
    QPainterPath shaft;
    shaft.moveTo(x0, h / 2);
    shaft.lineTo(std::max(x0, x1), h / 2);
    QPainterPathStroker s;
    s.setWidth(width);
    s.setCapStyle(Qt::FlatCap);
    s.setDashPattern(Qt::PenStyle(qBound(1, penStyle, 5)));
    QPainterPath out = s.createStroke(shaft);
    out.addPath(capPath(startCap, false));
    out.addPath(capPath(endCap, true));
    return out;
}

QJsonObject LineObject::toJson() const
{
    QJsonObject o = LabelObject::toJson();
    o[QStringLiteral("color")] = colorToString(color);
    o[QStringLiteral("width")] = width;
    o[QStringLiteral("style")] = penStyle;
    if (startCap != CapNone)
        o[QStringLiteral("startCap")] = QLatin1String(CAP_IDS[std::clamp(startCap, 0, 4)]);
    if (endCap != CapNone)
        o[QStringLiteral("endCap")] = QLatin1String(CAP_IDS[std::clamp(endCap, 0, 4)]);
    if (startCap != CapNone || endCap != CapNone)
        o[QStringLiteral("capSize")] = capSize;
    return o;
}

void LineObject::fromJson(const QJsonObject& o)
{
    LabelObject::fromJson(o);
    color = colorFrom(o.value(QStringLiteral("color")), Qt::black);
    width = num(o, "width", 0.3);
    penStyle = int(num(o, "style", 1));
    startCap = indexOf(CAP_IDS, 5, str(o, "startCap", QString()), CapNone);
    endCap = indexOf(CAP_IDS, 5, str(o, "endCap", QString()), CapNone);
    capSize = std::clamp(num(o, "capSize", 4), 1.0, 20.0);
}

// ================================================================ image

struct ImageObject::Cache {
    QString srcKey;  // which source is loaded
    QImage raster;
    QByteArray svg;
    std::shared_ptr<QSvgRenderer> renderer;  // svg, recoloured if needed
    QString rendererKey;
    QString procKey;  // adjusted / rasterized / dithered copy
    QImage processed;
};

namespace {
const char* const FIT_NAMES[] = {"none", "contain", "stretch"};

QSizeF naturalSizeMm(const QSvgRenderer* svg, const QImage& raster)
{
    if (svg) {
        QSizeF s = svg->defaultSize();
        if (s.isEmpty())
            s = svg->viewBoxF().size();
        if (s.isEmpty())
            s = QSizeF(100, 100);
        return s * (MM_PER_INCH / 96.0);  // SVG user units are CSS pixels
    }
    const double fallback = 96 / 0.0254;
    const double dpmX = raster.dotsPerMeterX() > 0 ? raster.dotsPerMeterX() : fallback;
    const double dpmY = raster.dotsPerMeterY() > 0 ? raster.dotsPerMeterY() : fallback;
    return QSizeF(raster.width() * 1000.0 / dpmX, raster.height() * 1000.0 / dpmY);
}

// Opacity for 1-bit output: blend towards white before dithering.
QImage fadeToWhite(QImage img, double opacity)
{
    if (opacity >= 1.0)
        return img;
    img = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            auto f = [opacity](int v) { return int(std::lround(v * opacity + 255 * (1 - opacity))); };
            line[x] = qRgba(f(qRed(px)), f(qGreen(px)), f(qBlue(px)), qAlpha(px));
        }
    }
    return img;
}

double alignOffset(double avail, double len, int align)
{
    return align == 0 ? 0.0 : align == 2 ? avail - len : (avail - len) / 2;
}
} // namespace

ImageObject::ImageObject() { dither = true; }

QString ImageObject::displayType() const { return tr("Изображение"); }

ImageObject::Cache& ImageObject::cache() const
{
    if (!m_cache)
        m_cache = std::make_shared<Cache>();
    return *m_cache;
}

void ImageObject::setEmbedded(const QImage& img)
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    imageData = ba;
    svgData.clear();
    pathTemplate.clear();
}

void ImageObject::setEmbeddedSvg(const QByteArray& svg)
{
    svgData = svg;
    imageData.clear();
    pathTemplate.clear();
}

bool ImageObject::loadFile(const QString& path, QString* err)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("svg") || suffix == QLatin1String("svgz")) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            if (err)
                *err = f.errorString();
            return false;
        }
        const QByteArray data = f.readAll();
        if (!QSvgRenderer(data).isValid()) {
            if (err)
                *err = tr("Файл не является корректным SVG");
            return false;
        }
        setEmbeddedSvg(data);
        return true;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        if (err)
            *err = reader.errorString();
        return false;
    }
    if (img.width() > 2000 || img.height() > 2000) {
        // keep the physical size: scale the resolution along with the pixels
        const QSizeF mm = naturalSizeMm(nullptr, img);
        img = img.scaled(2000, 2000, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        img.setDotsPerMeterX(int(std::lround(img.width() * 1000.0 / mm.width())));
        img.setDotsPerMeterY(int(std::lround(img.height() * 1000.0 / mm.height())));
    }
    setEmbedded(img);
    return true;
}

bool ImageObject::loadSource(const RenderContext& ctx) const
{
    Cache& c = cache();
    QString key, path;
    if (!svgData.isEmpty()) {
        key = QStringLiteral("svg:%1:%2").arg(qHash(svgData)).arg(svgData.size());
    } else if (!imageData.isEmpty()) {
        key = QStringLiteral("png:%1:%2").arg(qHash(imageData)).arg(imageData.size());
    } else if (!pathTemplate.isEmpty()) {
        path = expandTemplate(pathTemplate, ctx);
        if (QFileInfo(path).isRelative() && !ctx.baseDir.isEmpty())
            path = QDir(ctx.baseDir).filePath(path);
        key = QStringLiteral("file:") + path;
    }
    if (key != c.srcKey) {
        c = Cache();
        c.srcKey = key;
        if (!svgData.isEmpty()) {
            c.svg = svgData;
        } else if (!imageData.isEmpty()) {
            c.raster = QImage::fromData(imageData);
        } else if (!path.isEmpty()) {
            const QString suffix = QFileInfo(path).suffix().toLower();
            if (suffix == QLatin1String("svg") || suffix == QLatin1String("svgz")) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly))
                    c.svg = f.readAll();
            } else {
                QImageReader reader(path);
                reader.setAutoTransform(true);
                c.raster = reader.read();
            }
        }
    }
    if (!c.svg.isEmpty()) {
        const QString rk = recolor ? tintColor.name(QColor::HexRgb) : QString();
        if (!c.renderer || c.rendererKey != rk) {
            c.renderer = std::make_shared<QSvgRenderer>(recolor ? recolorSvg(c.svg, tintColor) : c.svg);
            c.rendererKey = rk;
            c.procKey.clear();
        }
        return c.renderer->isValid();
    }
    return !c.raster.isNull();
}

QSizeF ImageObject::sourceSizeMm(const RenderContext& ctx) const
{
    if (!loadSource(ctx))
        return QSizeF();
    return naturalSizeMm(cache().renderer.get(), cache().raster);
}

void ImageObject::paint(QPainter* p, const RenderContext& ctx) const
{
    const QRectF box(0, 0, w, h);
    if (!loadSource(ctx)) {
        if (ctx.rawTemplates || pathTemplate.isEmpty()) {
            QPen pen(QColor(150, 150, 150), 0);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            p->drawRect(box);
            p->drawLine(box.topLeft(), box.bottomRight());
            p->drawLine(box.topRight(), box.bottomLeft());
        }
        return;
    }
    Cache& c = cache();
    const QSizeF natural = naturalSizeMm(c.renderer.get(), c.raster);
    // visible part of the source, as fractions
    const double x0 = std::clamp(cropLeft, 0.0, 100.0) / 100, y0 = std::clamp(cropTop, 0.0, 100.0) / 100;
    const double x1 = 1 - std::clamp(cropRight, 0.0, 100.0) / 100, y1 = 1 - std::clamp(cropBottom, 0.0, 100.0) / 100;
    if (x1 - x0 < 0.001 || y1 - y0 < 0.001 || natural.isEmpty())
        return;
    const QSizeF visible(natural.width() * (x1 - x0), natural.height() * (y1 - y0));
    QSizeF size = box.size();
    if (fit == FitContain)
        size = visible * std::min(w / visible.width(), h / visible.height());
    else if (fit == FitNone)
        size = visible;
    const QRectF target = fit == FitStretch
                              ? box
                              : QRectF(alignOffset(w, size.width(), hAlign), alignOffset(h, size.height(), vAlign),
                                       size.width(), size.height());
    // the whole source, positioned so that its visible part lands on target
    const double fw = target.width() / (x1 - x0), fh = target.height() / (y1 - y0);
    const QRectF full(target.x() - x0 * fw, target.y() - y0 * fh, fw, fh);

    p->save();
    p->setClipRect(box, Qt::IntersectClip);
    if (flipH || flipV) {
        const QPointF ctr = target.center();
        p->translate(ctr);
        p->scale(flipH ? -1 : 1, flipV ? -1 : 1);
        p->translate(-ctr);
    }
    p->setClipRect(target, Qt::IntersectClip);
    const double op = std::clamp(opacity, 0.0, 1.0);
    const bool mono = ctx.monoDither && dither;
    ImageAdjust adj;
    adj.brightness = brightness;
    adj.contrast = contrast;
    adj.grayscale = monochrome;
    if (recolor && !c.renderer)
        adj.tint = tintColor;  // SVG is recoloured as vector in loadSource()

    if (c.renderer && adj.isIdentity() && !mono) {
        p->setOpacity(p->opacity() * op);
        c.renderer->render(p, full);
        p->restore();
        return;
    }

    // raster path: SVG is rasterized at device resolution (300 dpi for PDF/screen),
    // dithering needs the image resampled to exactly one pixel per printer dot
    const double dpi = ctx.deviceDpi > 0 ? ctx.deviceDpi : 300;
    QSize px;
    if (c.renderer || mono)
        px = QSize(std::clamp(int(std::lround(mmToDots(full.width(), dpi))), 1, 8000),
                   std::clamp(int(std::lround(mmToDots(full.height(), dpi))), 1, 8000));
    const QString key = c.rendererKey + QLatin1Char('|') + QString::number(brightness) + QLatin1Char('|') +
                        QString::number(contrast) + QLatin1Char('|') + QString::number(int(monochrome)) +
                        QLatin1Char('|') + (adj.tint.isValid() ? adj.tint.name(QColor::HexArgb) : QString()) +
                        QLatin1Char('|') + (mono ? QString::number(op) : QStringLiteral("-")) + QLatin1Char('|') +
                        QString::number(px.width()) + QLatin1Char('x') + QString::number(px.height());
    if (key != c.procKey) {
        QImage img;
        if (c.renderer) {
            img = QImage(px, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            QPainter sp(&img);
            c.renderer->render(&sp, QRectF(QPointF(0, 0), QSizeF(px)));
        } else {
            img = c.raster;
        }
        img = applyAdjust(img, adj);
        if (mono) {
            if (img.size() != px)
                img = img.scaled(px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            img = ditherToMono(fadeToWhite(img, op));
        }
        c.processed = img;
        c.procKey = key;
    }
    if (mono) {
        p->setRenderHint(QPainter::SmoothPixmapTransform, false);
    } else {
        p->setOpacity(p->opacity() * op);
        p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    }
    p->drawImage(full, c.processed);
    p->restore();
}

QJsonObject ImageObject::toJson() const
{
    QJsonObject o = LabelObject::toJson();
    if (!imageData.isEmpty())
        o[QStringLiteral("png")] = QString::fromLatin1(imageData.toBase64());
    if (!svgData.isEmpty())
        o[QStringLiteral("svg")] = QString::fromLatin1(svgData.toBase64());
    if (!pathTemplate.isEmpty())
        o[QStringLiteral("path")] = pathTemplate;
    o[QStringLiteral("fit")] = QLatin1String(FIT_NAMES[std::clamp(fit, 0, 2)]);
    if (hAlign != 1)
        o[QStringLiteral("hAlign")] = hAlign;
    if (vAlign != 1)
        o[QStringLiteral("vAlign")] = vAlign;
    if (cropLeft != 0)
        o[QStringLiteral("cropLeft")] = cropLeft;
    if (cropTop != 0)
        o[QStringLiteral("cropTop")] = cropTop;
    if (cropRight != 0)
        o[QStringLiteral("cropRight")] = cropRight;
    if (cropBottom != 0)
        o[QStringLiteral("cropBottom")] = cropBottom;
    if (flipH)
        o[QStringLiteral("flipH")] = true;
    if (flipV)
        o[QStringLiteral("flipV")] = true;
    if (opacity != 1.0)
        o[QStringLiteral("opacity")] = opacity;
    if (brightness != 0)
        o[QStringLiteral("brightness")] = brightness;
    if (contrast != 0)
        o[QStringLiteral("contrast")] = contrast;
    o[QStringLiteral("monochrome")] = monochrome;
    if (recolor)
        o[QStringLiteral("recolor")] = true;
    if (tintColor != QColor(Qt::black))
        o[QStringLiteral("tint")] = colorToString(tintColor);
    if (dither)
        o[QStringLiteral("dither")] = true;
    return o;
}

void ImageObject::fromJson(const QJsonObject& o)
{
    LabelObject::fromJson(o);
    imageData = QByteArray::fromBase64(str(o, "png", QString()).toLatin1());
    svgData = QByteArray::fromBase64(str(o, "svg", QString()).toLatin1());
    pathTemplate = str(o, "path", QString());
    const QString f = str(o, "fit", QString());
    if (f == QLatin1String(FIT_NAMES[FitNone]))
        fit = FitNone;
    else if (f == QLatin1String(FIT_NAMES[FitStretch]))
        fit = FitStretch;
    else if (f == QLatin1String(FIT_NAMES[FitContain]))
        fit = FitContain;
    else  // files written before "fit" existed
        fit = flag(o, "keepAspect", true) ? FitContain : FitStretch;
    hAlign = std::clamp(int(num(o, "hAlign", 1)), 0, 2);
    vAlign = std::clamp(int(num(o, "vAlign", 1)), 0, 2);
    cropLeft = std::clamp(num(o, "cropLeft", 0), 0.0, 100.0);
    cropTop = std::clamp(num(o, "cropTop", 0), 0.0, 100.0);
    cropRight = std::clamp(num(o, "cropRight", 0), 0.0, 100.0);
    cropBottom = std::clamp(num(o, "cropBottom", 0), 0.0, 100.0);
    flipH = flag(o, "flipH", false);
    flipV = flag(o, "flipV", false);
    opacity = std::clamp(num(o, "opacity", 1.0), 0.0, 1.0);
    brightness = std::clamp(int(num(o, "brightness", 0)), -100, 100);
    contrast = std::clamp(int(num(o, "contrast", 0)), -100, 100);
    monochrome = flag(o, "monochrome", false);
    recolor = flag(o, "recolor", false);
    tintColor = colorFrom(o.value(QLatin1String("tint")), Qt::black);
    dither = flag(o, "dither", false);
    m_cache.reset();
}

// ================================================================ barcode

BarcodeObject::BarcodeObject() { w = 40; h = 20; }

QString BarcodeObject::displayType() const { return tr("Штрихкод"); }

static void drawError(QPainter* p, double w, double h, const QString& msg)
{
    p->setPen(QPen(QColor(220, 0, 0), 0));
    p->setBrush(QColor(255, 230, 230));
    p->drawRect(QRectF(0, 0, w, h));
    TextStyle st;
    st.sizePt = 6;
    st.hAlign = 1;
    st.vAlign = 1;
    st.autoShrink = true;
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(200, 0, 0));
    p->drawPath(layoutText(msg, st, w, h));
}

void BarcodeObject::paint(QPainter* p, const RenderContext& ctx) const
{
    const bool placeholder = ctx.rawTemplates && data.contains(QLatin1Char('{'));
    barcode::Options opt;
    opt.qrEcc = qrEcc;
    opt.code39Check = code39Check;
    const QString sym = symbology;
    QString encodeValue = expandTemplate(data, ctx);
    if (placeholder) {
        // design view of a template: show a valid sample of the chosen symbology
        for (const auto& s : barcode::symbologies())
            if (s.id == symbology)
                encodeValue = s.sample;
    }
    const barcode::Result r = barcode::encode(sym, encodeValue, opt);
    if (!r.ok) {
        drawError(p, w, h, r.error);
        return;
    }

    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    const double dpi = ctx.deviceDpi;

    if (r.is2D) {
        const int q = quietZone ? r.matrixQuiet : 0;
        const int cols = r.width + 2 * q, rows = r.height + 2 * q;
        double mod = std::min(w / cols, h / rows);
        if (dpi > 0)
            mod = snapToDots(mod, dpi, 1);
        QPointF o((w - mod * cols) / 2 + q * mod, (h - mod * rows) / 2 + q * mod);
        o = pixelAlignedOrigin(p, o, dpi);
        QPainterPath path;
        for (int y = 0; y < r.height; ++y) {
            int x = 0;
            while (x < r.width) {
                if (!r.dark(x, y)) {
                    ++x;
                    continue;
                }
                int e = x;
                while (e < r.width && r.dark(e, y))
                    ++e;
                path.addRect(QRectF(o.x() + x * mod, o.y() + y * mod, (e - x) * mod, mod));
                x = e;
            }
        }
        path.setFillRule(Qt::WindingFill);
        p->drawPath(path);
        p->restore();
        return;
    }

    const int q = quietZone ? r.quiet : 0;
    const int total = int(r.bars.size()) + 2 * q;
    const QString human = placeholder ? data : r.humanText;
    const bool text = showText && !human.isEmpty();
    const double textH = text ? textSize * PT_TO_MM * 1.15 : 0;
    const double barsH = std::max(0.5, h - textH);
    double mod = w / total;
    if (dpi > 0)
        mod = snapToDots(w, dpi, total);
    QPointF o((w - mod * total) / 2 + q * mod, 0);
    o = pixelAlignedOrigin(p, o, dpi);
    const bool ean = r.eanType != 0 && text;
    const double guardExtra = ean ? textH * 0.55 : 0;

    QPainterPath path;
    const int n = int(r.bars.size());
    for (int i = 0; i < n;) {
        if (!r.bars[size_t(i)]) {
            ++i;
            continue;
        }
        const bool g = !r.guards.empty() && r.guards[size_t(i)];
        int e = i;
        while (e < n && r.bars[size_t(e)] &&
               (r.guards.empty() || bool(r.guards[size_t(e)]) == g))
            ++e;
        path.addRect(QRectF(o.x() + i * mod, o.y(), (e - i) * mod, barsH + (g ? guardExtra : 0)));
        i = e;
    }
    path.setFillRule(Qt::WindingFill);
    p->drawPath(path);

    if (text) {
        p->setRenderHint(QPainter::Antialiasing, true);
        TextStyle st;
        st.family = fontFamily;
        st.sizePt = textSize;
        st.hAlign = 1;
        st.vAlign = 0;
        st.wordWrap = false;
        st.autoShrink = true;
        const double ty = barsH + textH * 0.05;
        auto drawGroup = [&](const QString& s, double fromModule, double toModule) {
            const double x0 = o.x() + fromModule * mod;
            const double gw = (toModule - fromModule) * mod;
            QPainterPath tp = layoutText(s, st, gw, textH);
            tp.translate(x0, ty);
            p->drawPath(tp);
        };
        const int eanType = placeholder ? 0 : r.eanType;
        if (eanType == 13) {
            drawGroup(human.left(1), -std::max(q, 8), -1);
            drawGroup(human.mid(1, 6), 3, 45);
            drawGroup(human.mid(7, 6), 50, 92);
        } else if (eanType == 12) {
            drawGroup(human.left(1), -std::max(q, 8), -1);
            drawGroup(human.mid(1, 5), 10, 45);
            drawGroup(human.mid(6, 5), 50, 85);
            drawGroup(human.right(1), n + 1, n + std::max(q, 8));
        } else if (eanType == 8) {
            drawGroup(human.left(4), 3, 31);
            drawGroup(human.mid(4, 4), 36, 64);
        } else {
            drawGroup(human, 0, n);
        }
    }
    p->restore();
}

QJsonObject BarcodeObject::toJson() const
{
    QJsonObject o = LabelObject::toJson();
    o[QStringLiteral("symbology")] = symbology;
    o[QStringLiteral("data")] = data;
    o[QStringLiteral("showText")] = showText;
    o[QStringLiteral("font")] = fontFamily;
    o[QStringLiteral("textSize")] = textSize;
    o[QStringLiteral("color")] = colorToString(color);
    o[QStringLiteral("quietZone")] = quietZone;
    o[QStringLiteral("qrEcc")] = qrEcc;
    o[QStringLiteral("code39Check")] = code39Check;
    return o;
}

void BarcodeObject::fromJson(const QJsonObject& o)
{
    LabelObject::fromJson(o);
    symbology = str(o, "symbology", QStringLiteral("code128"));
    data = str(o, "data", QString());
    showText = flag(o, "showText", true);
    fontFamily = str(o, "font", QStringLiteral("Arial"));
    textSize = num(o, "textSize", 8);
    color = colorFrom(o.value(QStringLiteral("color")), Qt::black);
    quietZone = flag(o, "quietZone", true);
    qrEcc = int(num(o, "qrEcc", 1));
    code39Check = flag(o, "code39Check", false);
}

} // namespace lf
