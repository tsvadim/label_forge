// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShapePaths.h"

#include "ImageEffects.h"
#include "RenderContext.h"

#include <QCoreApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QRadialGradient>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace lf::shapes {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Shapes", s); }

constexpr double PI = 3.14159265358979323846;

const char* const KIND_IDS[KindCount] = {"polygon",  "star",   "triangle", "right-triangle", "rhombus",
                                         "parallelogram", "trapezoid", "cross", "arrow", "double-arrow",
                                         "chevron",  "callout", "banner"};
const char* const PATTERN_IDS[PatternCount] = {"horizontal", "vertical", "diagonal", "back-diagonal",
                                               "cross",      "diagonal-cross", "dots"};

QPainterPath polygon(const QPolygonF& pts)
{
    QPainterPath p;
    p.addPolygon(pts);
    p.closeSubpath();
    return p;
}

// Points around the unit circle, stretched so their bounding box fills w x h.
QPainterPath fitted(const QPolygonF& unit, double w, double h)
{
    const QRectF b = unit.boundingRect();
    QPolygonF out;
    for (const QPointF& pt : unit)
        out << QPointF((pt.x() - b.left()) / b.width() * w, (pt.y() - b.top()) / b.height() * h);
    return polygon(out);
}

// Boolean path operations flatten curves with a tolerance in path units; in
// millimetres that gives visibly faceted arcs, so they run on a scaled copy.
constexpr double OP_SCALE = 100;

QPainterPath up(const QPainterPath& p) { return QTransform::fromScale(OP_SCALE, OP_SCALE).map(p); }
QPainterPath down(const QPainterPath& p) { return QTransform::fromScale(1 / OP_SCALE, 1 / OP_SCALE).map(p); }

// Region inside the (scaled) path further than d (scaled) from its outline.
QPainterPath insideBeyond(const QPainterPath& path, double d)
{
    if (d <= 0)
        return path;
    QPainterPathStroker s;
    s.setWidth(2 * d);
    s.setJoinStyle(Qt::MiterJoin);
    s.setMiterLimit(20);
    s.setCapStyle(Qt::FlatCap);
    return path.subtracted(s.createStroke(path));
}

QPainterPath band(const QPainterPath& path, double from, double to)
{
    return insideBeyond(path, from * OP_SCALE).subtracted(insideBeyond(path, to * OP_SCALE));
}
} // namespace

QString kindId(int kind) { return QLatin1String(KIND_IDS[std::clamp(kind, 0, KindCount - 1)]); }

int kindFromId(const QString& id)
{
    for (int i = 0; i < KindCount; ++i)
        if (id == QLatin1String(KIND_IDS[i]))
            return i;
    return -1;
}

QString kindName(int kind)
{
    switch (kind) {
    case Polygon: return tr("Многоугольник");
    case Star: return tr("Звезда");
    case Triangle: return tr("Треугольник");
    case RightTriangle: return tr("Прямоугольный треугольник");
    case Rhombus: return tr("Ромб");
    case Parallelogram: return tr("Параллелограмм");
    case Trapezoid: return tr("Трапеция");
    case Cross: return tr("Крест");
    case Arrow: return tr("Стрелка");
    case DoubleArrow: return tr("Двойная стрелка");
    case Chevron: return tr("Шеврон");
    case Callout: return tr("Выноска");
    case Banner: return tr("Лента");
    default: return QString();
    }
}

QString param1Label(int kind)
{
    switch (kind) {
    case Polygon: return tr("Сторон:");
    case Star: return tr("Лучей:");
    case Parallelogram:
    case Trapezoid: return tr("Скос:");
    case Cross: return tr("Толщина:");
    case Arrow:
    case DoubleArrow: return tr("Толщина древка:");
    case Chevron:
    case Banner: return tr("Вырез:");
    case Callout: return tr("Хвостик, положение:");
    default: return QString();
    }
}

QString param2Label(int kind)
{
    switch (kind) {
    case Star: return tr("Глубина лучей:");
    case Arrow:
    case DoubleArrow: return tr("Наконечник:");
    case Callout: return tr("Хвостик, длина:");
    default: return QString();
    }
}

void paramRange(int kind, int param, double* min, double* max, double* def, int* decimals)
{
    // counts are whole numbers, proportions are in percent of the box
    *decimals = 0;
    *min = 5;
    *max = 95;
    *def = 0;
    if (param == 1) {
        switch (kind) {
        case Polygon: *min = 3; *max = 12; *def = 6; break;
        case Star: *min = 3; *max = 24; *def = 5; break;
        case Parallelogram: *max = 90; *def = 25; break;
        case Trapezoid: *max = 90; *def = 40; break;
        case Cross: *def = 33; break;
        case Arrow:
        case DoubleArrow: *def = 50; break;
        case Chevron: *max = 90; *def = 30; break;
        case Callout: *def = 25; break;
        case Banner: *min = 0; *max = 45; *def = 15; break;
        default: break;
        }
    } else {
        switch (kind) {
        case Star: *min = 10; *max = 90; *def = 45; break;
        case Arrow: *max = 90; *def = 35; break;
        case DoubleArrow: *max = 50; *def = 25; break;
        case Callout: *min = 10; *max = 60; *def = 25; break;
        default: break;
        }
    }
}

QPainterPath shapePath(int kind, double w, double h, double p1, double p2)
{
    auto pct = [](double v) { return std::clamp(v, 0.0, 100.0) / 100.0; };
    switch (kind) {
    case Polygon:
    case Star: {
        const int n = std::clamp(int(std::lround(p1)), 3, 24);
        const bool star = kind == Star;
        const double inner = star ? std::clamp(pct(p2), 0.05, 0.95) : 1.0;
        QPolygonF unit;
        const int count = star ? 2 * n : n;
        for (int i = 0; i < count; ++i) {
            const double a = -PI / 2 + i * 2 * PI / count;
            const double r = star && (i % 2) ? inner : 1.0;
            unit << QPointF(r * std::cos(a), r * std::sin(a));
        }
        return fitted(unit, w, h);
    }
    case Triangle: return polygon({{w / 2, 0}, {w, h}, {0, h}});
    case RightTriangle: return polygon({{0, 0}, {0, h}, {w, h}});
    case Rhombus: return polygon({{w / 2, 0}, {w, h / 2}, {w / 2, h}, {0, h / 2}});
    case Parallelogram: {
        const double s = w * pct(p1);
        return polygon({{s, 0}, {w, 0}, {w - s, h}, {0, h}});
    }
    case Trapezoid: {
        const double s = w * pct(p1) / 2;
        return polygon({{s, 0}, {w - s, 0}, {w, h}, {0, h}});
    }
    case Cross: {
        const double t = pct(p1);
        const double x0 = w * (1 - t) / 2, x1 = w - x0, y0 = h * (1 - t) / 2, y1 = h - y0;
        return polygon({{x0, 0}, {x1, 0}, {x1, y0}, {w, y0}, {w, y1}, {x1, y1}, {x1, h}, {x0, h}, {x0, y1},
                        {0, y1}, {0, y0}, {x0, y0}});
    }
    case Arrow: {
        const double t = pct(p1), hl = w * pct(p2);
        const double a = h * (1 - t) / 2, b = h - a;
        return polygon({{0, a}, {w - hl, a}, {w - hl, 0}, {w, h / 2}, {w - hl, h}, {w - hl, b}, {0, b}});
    }
    case DoubleArrow: {
        const double t = pct(p1), hl = w * std::min(pct(p2), 0.5);
        const double a = h * (1 - t) / 2, b = h - a;
        return polygon({{0, h / 2}, {hl, 0}, {hl, a}, {w - hl, a}, {w - hl, 0}, {w, h / 2}, {w - hl, h},
                        {w - hl, b}, {hl, b}, {hl, h}});
    }
    case Chevron: {
        const double d = w * pct(p1);
        return polygon({{0, 0}, {w - d, 0}, {w, h / 2}, {w - d, h}, {0, h}, {d, h / 2}});
    }
    case Callout: {
        const double tail = h * std::clamp(pct(p2), 0.1, 0.6);
        const double bodyH = h - tail;
        const double r = std::min(bodyH, w) * 0.15;
        const double tx = w * std::clamp(pct(p1), 0.05, 0.95);
        const double half = std::min(w * 0.08, 4.0);
        const double bx0 = std::clamp(tx - half, r, std::max(r, w - r - 2 * half));
        QPainterPath body;
        body.addRoundedRect(QRectF(0, 0, w, bodyH), r, r);
        // the tail leans towards the nearer side, like a speech bubble
        const double tipX = tx < w / 2 ? bx0 : bx0 + 2 * half;
        const QPainterPath t = polygon({{bx0, bodyH - r * 0.5}, {bx0 + 2 * half, bodyH - r * 0.5}, {tipX, h}});
        return down(up(body).united(up(t)));
    }
    case Banner: {
        const double d = w * pct(p1);
        return polygon({{0, 0}, {w, 0}, {w - d, h / 2}, {w, h}, {0, h}, {d, h / 2}});
    }
    default: {
        QPainterPath p;
        p.addRect(0, 0, w, h);
        return p;
    }
    }
}

QPainterPath frameRegion(const QPainterPath& path, int frame, double width, double gap)
{
    width = std::max(0.05, width);
    gap = std::max(0.05, gap);
    const QPainterPath big = up(path);
    if (frame == FrameThickThin) {
        const double thin = std::max(0.1, width / 3);
        return down(band(big, 0, width).united(band(big, width + gap, width + gap + thin)));
    }
    return down(band(big, 0, width).united(band(big, width + gap, 2 * width + gap)));
}

QString patternId(int pattern) { return QLatin1String(PATTERN_IDS[std::clamp(pattern, 0, PatternCount - 1)]); }

int patternFromId(const QString& id)
{
    for (int i = 0; i < PatternCount; ++i)
        if (id == QLatin1String(PATTERN_IDS[i]))
            return i;
    return -1;
}

QString patternName(int pattern)
{
    switch (pattern) {
    case PatHorizontal: return tr("Горизонтальная штриховка");
    case PatVertical: return tr("Вертикальная штриховка");
    case PatDiagonal: return tr("Диагональная штриховка");
    case PatBackDiagonal: return tr("Обратная диагональ");
    case PatCross: return tr("Клетка");
    case PatDiagonalCross: return tr("Косая клетка");
    case PatDots: return tr("Точки");
    default: return QString();
    }
}

QImage rasterize(const QRectF& area, double dpi, const std::function<void(QPainter*)>& paint)
{
    const int pw = std::clamp(int(std::ceil(mmToDots(area.width(), dpi))), 1, 8000);
    const int ph = std::clamp(int(std::ceil(mmToDots(area.height(), dpi))), 1, 8000);
    QImage img(pw, ph, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(pw / area.width(), ph / area.height());
    p.translate(-area.topLeft());
    paint(&p);
    return img;
}

QImage ditherOverWhite(const QImage& src)
{
    QImage img = src.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            const int g = (qGray(px) * a + 255 * (255 - a)) / 255;
            line[x] = qRgb(g, g, g);
        }
    }
    QImage out = ditherToMono(img);
    for (int y = 0; y < out.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
            if (line[x] == 0xFFFFFFFFu)
                line[x] = 0;
    }
    return out;
}

void fillPath(QPainter* p, const QPainterPath& path, const FillStyle& f, const QRectF& box, const RenderContext& ctx)
{
    switch (f.type) {
    case FillSolid:
        if (f.color.alpha() > 0)
            p->fillPath(path, f.color);
        return;
    case FillLinear:
    case FillRadial: {
        QBrush brush;
        if (f.type == FillLinear) {
            const double a = f.angle * PI / 180;
            const QPointF dir(std::cos(a), std::sin(a));
            const double half = std::abs(box.width() / 2 * dir.x()) + std::abs(box.height() / 2 * dir.y());
            QLinearGradient g(box.center() - dir * half, box.center() + dir * half);
            g.setColorAt(0, f.color);
            g.setColorAt(1, f.color2);
            brush = QBrush(g);
        } else {
            QRadialGradient g(box.center(), std::hypot(box.width(), box.height()) / 2);
            g.setColorAt(0, f.color);
            g.setColorAt(1, f.color2);
            brush = QBrush(g);
        }
        if (!ctx.monoDither) {
            p->fillPath(path, brush);
            return;
        }
        // 1-bit thermal output: a threshold would turn the gradient into a hard step
        const QRectF area = path.boundingRect();
        const QImage img = ditherOverWhite(
            rasterize(area, ctx.deviceDpi > 0 ? ctx.deviceDpi : 203, [&](QPainter* q) { q->fillPath(path, brush); }));
        p->save();
        p->setRenderHint(QPainter::SmoothPixmapTransform, false);
        p->drawImage(area, img);
        p->restore();
        return;
    }
    case FillPattern: {
        if (f.color2.alpha() > 0)
            p->fillPath(path, f.color2);
        const QRectF b = path.boundingRect();
        const double step = std::max(0.2, f.spacing);
        const double lw = std::max(0.05, f.lineWidth);
        QPainterPath marks;
        if (f.pattern == PatDots) {
            for (double y = b.top() + step / 2; y < b.bottom() + step; y += step)
                for (double x = b.left() + step / 2; x < b.right() + step; x += step)
                    marks.addEllipse(QPointF(x, y), lw / 2, lw / 2);
            p->save();
            p->setClipPath(path, Qt::IntersectClip);
            p->fillPath(marks, f.color);
            p->restore();
            return;
        }
        QList<double> angles;
        switch (f.pattern) {
        case PatHorizontal: angles = {0}; break;
        case PatVertical: angles = {90}; break;
        case PatBackDiagonal: angles = {45}; break;
        case PatCross: angles = {0, 90}; break;
        case PatDiagonalCross: angles = {45, -45}; break;
        default: angles = {-45}; break;  // "/" on screen
        }
        const double reach = std::hypot(b.width(), b.height()) / 2 + step;
        for (double deg : angles) {
            const double a = deg * PI / 180;
            const QPointF d(std::cos(a), std::sin(a)), n(-d.y(), d.x());
            for (double t = -reach; t <= reach; t += step) {
                const QPointF c = b.center() + n * t;
                marks.moveTo(c - d * reach);
                marks.lineTo(c + d * reach);
            }
        }
        QPen pen(f.color, lw);
        pen.setCapStyle(Qt::FlatCap);
        p->save();
        p->setClipPath(path, Qt::IntersectClip);
        p->strokePath(marks, pen);
        p->restore();
        return;
    }
    default: return;
    }
}

} // namespace lf::shapes
