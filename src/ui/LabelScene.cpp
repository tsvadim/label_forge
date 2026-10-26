// SPDX-License-Identifier: GPL-3.0-or-later
#include "LabelScene.h"

#include "ObjectItem.h"
#include "core/LabelDocument.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace lf {

LabelScene::LabelScene(QObject* parent) : QGraphicsScene(parent)
{
    setItemIndexMethod(QGraphicsScene::NoIndex);
}

void LabelScene::setDocument(LabelDocument* doc)
{
    m_doc = doc;
    rebuild();
}

void LabelScene::updateSceneRect()
{
    if (!m_doc)
        return;
    const double w = m_doc->label.width, h = m_doc->label.height;
    const double m = std::max(15.0, 0.35 * std::max(w, h));
    setSceneRect(QRectF(-m, -m, w + 2 * m, h + 2 * m));
}

void LabelScene::rebuild()
{
    clear();
    if (!m_doc)
        return;
    updateSceneRect();
    int z = 0;
    for (const auto& obj : m_doc->objects) {
        auto* item = new ObjectItem(obj.get());
        item->setZValue(z++);
        addItem(item);
    }
    update();
}

void LabelScene::refreshObject(LabelObject* obj)
{
    if (ObjectItem* it = itemFor(obj))
        it->syncFromObject();
}

void LabelScene::refreshAll()
{
    updateSceneRect();
    for (QGraphicsItem* it : items())
        if (it->type() == ObjectItem::Type)
            static_cast<ObjectItem*>(it)->syncFromObject();
    update();
}

ObjectItem* LabelScene::itemFor(const LabelObject* obj) const
{
    for (QGraphicsItem* it : items())
        if (it->type() == ObjectItem::Type && static_cast<ObjectItem*>(it)->object() == obj)
            return static_cast<ObjectItem*>(it);
    return nullptr;
}

QList<LabelObject*> LabelScene::selectedObjects() const
{
    QList<LabelObject*> out;
    if (!m_doc)
        return out;
    const QList<QGraphicsItem*> sel = selectedItems();
    for (const auto& o : m_doc->objects)
        for (QGraphicsItem* it : sel)
            if (it->type() == ObjectItem::Type && static_cast<ObjectItem*>(it)->object() == o.get())
                out << o.get();
    return out;
}

void LabelScene::selectObjects(const QList<LabelObject*>& objs)
{
    const QSignalBlocker block(this);
    clearSelection();
    for (LabelObject* o : objs)
        if (ObjectItem* it = itemFor(o))
            it->setSelected(true);
    blockSignals(false);
    emit selectionChanged();
}

void LabelScene::setContext(const RenderContext& ctx)
{
    m_ctx = ctx;
    update();
}

void LabelScene::setGridStep(double mm)
{
    m_grid = std::max(0.1, mm);
    update();
}

void LabelScene::setGridVisible(bool on)
{
    m_showGrid = on;
    update();
}

void LabelScene::setShowObjectFrames(bool on)
{
    m_frames = on;
    update();
}

QPointF LabelScene::snapPoint(const QPointF& p) const
{
    if (!m_snap || m_grid <= 0 || m_snapBypass)
        return p;
    return QPointF(std::round(p.x() / m_grid) * m_grid, std::round(p.y() / m_grid) * m_grid);
}

void LabelScene::setTool(Tool t)
{
    m_tool = t;
    for (QGraphicsView* v : views())
        v->viewport()->setCursor(t == ToolSelect ? Qt::ArrowCursor : Qt::CrossCursor);
}

void LabelScene::notifyEditFinished()
{
    if (m_edited) {
        m_edited = false;
        emit geometryEdited();
    }
}

void LabelScene::drawBackground(QPainter* p, const QRectF& rect)
{
    p->fillRect(rect, QColor(0xdd, 0xe1, 0xe6));
    if (!m_doc)
        return;
    const QRectF lr(0, 0, m_doc->label.width, m_doc->label.height);
    const double rad = m_doc->label.radius;
    QPainterPath shape;
    shape.addRoundedRect(lr, rad, rad);
    // drop shadow
    const double px = views().isEmpty() ? 0.25 : 1.0 / views().first()->transform().m11();
    p->fillPath(shape.translated(3 * px, 3 * px), QColor(0, 0, 0, 40));
    p->fillPath(shape, m_doc->label.background.alpha() > 0 ? m_doc->label.background : QColor(Qt::white));

    if (m_showGrid && m_grid > 0) {
        double step = m_grid;
        while (step / px < 6)  // keep lines at least 6 px apart
            step *= 5;
        QPen pen(QColor(30, 80, 160, 28), 0);
        pen.setCosmetic(true);
        p->setPen(pen);
        p->save();
        p->setClipPath(shape);
        for (double x = step; x < lr.width(); x += step)
            p->drawLine(QPointF(x, 0), QPointF(x, lr.height()));
        for (double y = step; y < lr.height(); y += step)
            p->drawLine(QPointF(0, y), QPointF(lr.width(), y));
        p->restore();
    }
}

void LabelScene::drawForeground(QPainter* p, const QRectF&)
{
    if (!m_doc)
        return;
    const QRectF lr(0, 0, m_doc->label.width, m_doc->label.height);
    QPen pen(QColor(110, 110, 110), 0);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->drawRoundedRect(lr, m_doc->label.radius, m_doc->label.radius);
}

void LabelScene::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    m_snapBypass = e->modifiers() & Qt::AltModifier;
    if (m_tool != ToolSelect && e->button() == Qt::LeftButton) {
        emit insertRequested(int(m_tool), snapPoint(e->scenePos()));
        e->accept();
        return;
    }
    m_edited = false;
    QGraphicsScene::mousePressEvent(e);
}

void LabelScene::mouseMoveEvent(QGraphicsSceneMouseEvent* e)
{
    m_snapBypass = e->modifiers() & Qt::AltModifier;
    emit cursorMoved(e->scenePos());
    QGraphicsScene::mouseMoveEvent(e);
}

void LabelScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* e)
{
    QGraphicsScene::mouseReleaseEvent(e);
    notifyEditFinished();
}

} // namespace lf
