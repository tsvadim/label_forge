// SPDX-License-Identifier: GPL-3.0-or-later
#include "Renderer.h"

#include "LabelDocument.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace lf {

void renderObject(QPainter* p, const LabelObject& obj, const RenderContext& ctx)
{
    p->save();
    p->translate(obj.x, obj.y);
    if (obj.rotation != 0) {
        p->translate(obj.w / 2, obj.h / 2);
        p->rotate(obj.rotation);
        p->translate(-obj.w / 2, -obj.h / 2);
    }
    obj.paint(p, ctx);
    p->restore();
}

void renderLabel(QPainter* p, const LabelDocument& doc, const RenderContext& ctx, const RenderOptions& opt)
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::TextAntialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF r(0, 0, doc.label.width, doc.label.height);
    QPainterPath shape;
    if (doc.label.radius > 0)
        shape.addRoundedRect(r, doc.label.radius, doc.label.radius);
    else
        shape.addRect(r);
    if (opt.background && doc.label.background.alpha() > 0 && doc.label.background != QColor(Qt::white))
        p->fillPath(shape, doc.label.background);
    if (opt.clip)
        p->setClipPath(shape, Qt::IntersectClip);
    for (const auto& obj : doc.objects) {
        if (opt.printableOnly && !obj->printable)
            continue;
        renderObject(p, *obj, ctx);
    }
    if (opt.outline) {
        p->setClipping(false);
        p->setPen(QPen(QColor(160, 160, 160), 0.15));
        p->setBrush(Qt::NoBrush);
        p->drawPath(shape);
    }
    p->restore();
}

QImage renderLabelImage(const LabelDocument& doc, const RenderContext& ctxIn, double dpi, bool mono,
                        const RenderOptions& opt)
{
    const int wpx = std::max(1, int(std::lround(mmToDots(doc.label.width, dpi))));
    const int hpx = std::max(1, int(std::lround(mmToDots(doc.label.height, dpi))));
    QImage img(wpx, hpx, QImage::Format_ARGB32_Premultiplied);
    img.setDotsPerMeterX(int(std::lround(dpi / 0.0254)));
    img.setDotsPerMeterY(int(std::lround(dpi / 0.0254)));
    img.fill(Qt::white);
    RenderContext ctx = ctxIn;
    ctx.deviceDpi = dpi;
    {
        QPainter p(&img);
        p.scale(dpi / MM_PER_INCH, dpi / MM_PER_INCH);
        renderLabel(&p, doc, ctx, opt);
    }
    if (!mono)
        return img;
    QImage out(wpx, hpx, QImage::Format_Mono);
    out.setColor(0, qRgb(255, 255, 255));
    out.setColor(1, qRgb(0, 0, 0));
    out.fill(0);
    for (int y = 0; y < hpx; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < wpx; ++x) {
            if (qGray(line[x]) < 128)
                out.setPixel(x, y, 1);
        }
    }
    return out;
}

} // namespace lf
