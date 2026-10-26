// SPDX-License-Identifier: GPL-3.0-or-later
#include "ObjectItem.h"

#include "LabelScene.h"
#include "core/LabelObject.h"
#include "core/RenderContext.h"

#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QtMath>
#include <cmath>

namespace lf {

namespace {
const QColor SELECT_COLOR(33, 113, 181);
constexpr double MIN_SIZE = 0.5;
} // namespace

ObjectItem::ObjectItem(LabelObject* obj) : m_obj(obj)
{
    setAcceptHoverEvents(true);
    syncFromObject();
}

LabelScene* ObjectItem::labelScene() const { return static_cast<LabelScene*>(scene()); }

void ObjectItem::syncFromObject()
{
    prepareGeometryChange();
    m_internal = true;
    setFlags(ItemIsSelectable | ItemSendsGeometryChanges | (m_obj->locked ? GraphicsItemFlags() : ItemIsMovable));
    setTransformOriginPoint(m_obj->w / 2, m_obj->h / 2);
    setRotation(m_obj->rotation);
    setPos(m_obj->x, m_obj->y);
    m_internal = false;
    update();
}

double ObjectItem::handleSize() const
{
    double s = 4.0;
    if (scene() && !scene()->views().isEmpty())
        s = scene()->views().first()->transform().m11();
    return 7.0 / std::max(0.01, s);
}

QRectF ObjectItem::boundingRect() const
{
    const double hs = handleSize();
    const double m = std::max(hs * 5, 0.5 * std::max(m_obj->w, m_obj->h) + 2);
    return QRectF(0, 0, m_obj->w, m_obj->h).adjusted(-m, -m, m, m);
}

QPainterPath ObjectItem::shape() const
{
    QPainterPath p;
    QRectF r(0, 0, m_obj->w, m_obj->h);
    if (m_obj->h < 2 || m_obj->w < 2) {
        const double pad = handleSize() * 0.5;
        r.adjust(-pad, -pad, pad, pad);
    }
    p.addRect(r);
    if (isSelected() && !m_obj->locked) {
        const double hs = handleSize();
        for (int i = 0; i < 9; ++i) {
            const QPointF c = handlePos(i);
            p.addRect(QRectF(c.x() - hs / 2, c.y() - hs / 2, hs, hs));
        }
    }
    return p;
}

QPointF ObjectItem::handlePos(int i) const
{
    const double w = m_obj->w, h = m_obj->h;
    switch (i) {
    case 0: return {0, 0};
    case 1: return {w / 2, 0};
    case 2: return {w, 0};
    case 3: return {w, h / 2};
    case 4: return {w, h};
    case 5: return {w / 2, h};
    case 6: return {0, h};
    case 7: return {0, h / 2};
    default: return {w / 2, -handleSize() * 3.5};
    }
}

int ObjectItem::handleAt(const QPointF& local) const
{
    if (!isSelected() || m_obj->locked)
        return -1;
    const double hs = handleSize() * 0.75;
    for (int i = 8; i >= 0; --i) {
        const QPointF d = local - handlePos(i);
        if (std::abs(d.x()) <= hs && std::abs(d.y()) <= hs)
            return i;
    }
    return -1;
}

void ObjectItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*)
{
    LabelScene* sc = labelScene();
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_obj->paint(p, sc->context());
    p->restore();

    const QRectF box(0, 0, m_obj->w, m_obj->h);
    if (!m_obj->printable)
        p->fillRect(box, QBrush(QColor(220, 0, 0, 70), Qt::BDiagPattern));

    if (isSelected()) {
        QPen pen(SELECT_COLOR, 0, Qt::DashLine);
        pen.setCosmetic(true);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(box);
        if (!m_obj->locked) {
            const double hs = handleSize();
            QPen hp(SELECT_COLOR, 0);
            hp.setCosmetic(true);
            p->setPen(hp);
            p->drawLine(handlePos(1), handlePos(8));
            p->setBrush(Qt::white);
            for (int i = 0; i < 8; ++i) {
                const QPointF c = handlePos(i);
                p->drawRect(QRectF(c.x() - hs / 2, c.y() - hs / 2, hs, hs));
            }
            p->setBrush(SELECT_COLOR);
            p->drawEllipse(handlePos(8), hs / 2, hs / 2);
        } else {
            const double hs = handleSize();
            p->setBrush(QColor(120, 120, 120));
            p->drawRect(QRectF(-hs / 2, -hs / 2, hs, hs));
        }
    } else if (sc->showObjectFrames()) {
        QPen pen(QColor(0, 0, 0, 45), 0, Qt::DotLine);
        pen.setCosmetic(true);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->drawRect(box);
    }
}

QVariant ObjectItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionChange && !m_internal && scene()) {
        QPointF p = value.toPointF();
        return labelScene()->snapPoint(p);
    }
    if (change == ItemPositionHasChanged) {
        const QPointF p = pos();
        if (m_obj->x != p.x() || m_obj->y != p.y()) {
            m_obj->x = p.x();
            m_obj->y = p.y();
            if (!m_internal && scene())
                labelScene()->markEdited();
        }
    }
    if (change == ItemSelectedHasChanged)
        prepareGeometryChange();
    return QGraphicsItem::itemChange(change, value);
}

void ObjectItem::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        const int h = handleAt(e->pos());
        if (h >= 0) {
            m_drag = h;
            m_changed = false;
            m_startTransform = sceneTransform();
            m_startPos = pos();
            m_startW = m_obj->w;
            m_startH = m_obj->h;
            e->accept();
            return;
        }
    }
    m_drag = -1;
    QGraphicsItem::mousePressEvent(e);
}

void ObjectItem::applyGeometry(double w, double h, const QPointF& tl)
{
    QTransform r;
    r.rotate(m_obj->rotation);
    const QPointF c0(m_startW / 2, m_startH / 2);
    const QPointF c1(w / 2, h / 2);
    const QPointF p1 = m_startPos + c0 - r.map(c0) + r.map(tl) - c1 + r.map(c1);
    prepareGeometryChange();
    m_obj->w = w;
    m_obj->h = h;
    m_internal = true;
    setTransformOriginPoint(c1);
    setPos(p1);
    m_internal = false;
    m_obj->x = p1.x();
    m_obj->y = p1.y();
    m_changed = true;
    update();
}

void ObjectItem::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_drag < 0) {
        QGraphicsItem::mouseMoveEvent(e);
        return;
    }
    if (m_drag == 8) {
        const QPointF c = m_startTransform.map(QPointF(m_startW / 2, m_startH / 2));
        const QPointF d = e->scenePos() - c;
        double a = qRadiansToDegrees(std::atan2(d.y(), d.x())) + 90.0;
        const double step = (e->modifiers() & Qt::ShiftModifier) ? 1.0 : 15.0;
        a = std::round(a / step) * step;
        a = std::fmod(a + 360.0, 360.0);
        if (a > 180)
            a -= 360;
        prepareGeometryChange();
        m_obj->rotation = a;
        setRotation(a);
        m_changed = true;
        return;
    }
    QPointF sp = e->scenePos();
    if (m_obj->rotation == 0)
        sp = labelScene()->snapPoint(sp);
    const QPointF p = m_startTransform.inverted().map(sp);
    double left = 0, top = 0, right = m_startW, bottom = m_startH;
    const int hnd = m_drag;
    if (hnd == 0 || hnd == 6 || hnd == 7)
        left = std::min(p.x(), right - MIN_SIZE);
    if (hnd == 2 || hnd == 3 || hnd == 4)
        right = std::max(p.x(), left + MIN_SIZE);
    if (hnd == 0 || hnd == 1 || hnd == 2)
        top = std::min(p.y(), bottom - MIN_SIZE);
    if (hnd == 4 || hnd == 5 || hnd == 6)
        bottom = std::max(p.y(), top + MIN_SIZE);
    // Shift on a corner keeps the proportions
    if ((e->modifiers() & Qt::ShiftModifier) && (hnd % 2 == 0) && m_startW > 0 && m_startH > 0) {
        const double ratio = m_startW / m_startH;
        double w = right - left, h = bottom - top;
        if (w / h > ratio)
            h = w / ratio;
        else
            w = h * ratio;
        if (hnd == 0 || hnd == 6)
            left = right - w;
        else
            right = left + w;
        if (hnd == 0 || hnd == 2)
            top = bottom - h;
        else
            bottom = top + h;
    }
    applyGeometry(right - left, bottom - top, QPointF(left, top));
}

void ObjectItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    if (m_drag >= 0) {
        m_drag = -1;
        if (m_changed)
            labelScene()->markEdited();
        labelScene()->notifyEditFinished();
        e->accept();
        return;
    }
    QGraphicsItem::mouseReleaseEvent(e);
}

void ObjectItem::hoverMoveEvent(QGraphicsSceneHoverEvent* e)
{
    const int h = handleAt(e->pos());
    static const Qt::CursorShape shapes[] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor,
                                             Qt::SizeHorCursor,   Qt::SizeFDiagCursor, Qt::SizeVerCursor,
                                             Qt::SizeBDiagCursor, Qt::SizeHorCursor};
    if (h >= 0 && h < 8) {
        // rotate the cursor direction with the object (45° steps)
        const int steps = int(std::lround(m_obj->rotation / 45.0));
        const int order[] = {0, 1, 2, 3, 4, 5, 6, 7};
        const int idx = order[((h + steps) % 8 + 8) % 8];
        setCursor(shapes[idx]);
    } else if (h == 8) {
        setCursor(Qt::CrossCursor);
    } else if (!m_obj->locked) {
        setCursor(Qt::SizeAllCursor);
    } else {
        unsetCursor();
    }
    QGraphicsItem::hoverMoveEvent(e);
}

void ObjectItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* e)
{
    unsetCursor();
    QGraphicsItem::hoverLeaveEvent(e);
}

} // namespace lf
