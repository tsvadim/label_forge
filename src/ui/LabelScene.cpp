// SPDX-License-Identifier: GPL-3.0-or-later
#include "LabelScene.h"

#include "ObjectItem.h"
#include "core/LabelDocument.h"
#include "print/PrintJob.h"

#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <cmath>

namespace lf {

LabelScene::LabelScene(QObject* parent) : QGraphicsScene(parent)
{
    setItemIndexMethod(QGraphicsScene::NoIndex);
    m_cells = {QPointF()};
    m_cellCtx = {m_ctx};
    // copies of a selected object show a frame too
    connect(this, &QGraphicsScene::selectionChanged, this, [this] {
        m_selected.clear();
        for (QGraphicsItem* it : selectedItems())
            if (it->type() == ObjectItem::Type)
                m_selected.insert(static_cast<ObjectItem*>(it)->object());
        if (m_cells.size() > 1)
            update();
    });
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
    const QRectF c = contentRect();
    const double m = std::max(15.0, 0.35 * std::max(c.width(), c.height()));
    setSceneRect(c.adjusted(-m, -m, m, m));
}

bool LabelScene::updateLayout()
{
    QVector<QPointF> cells;
    QRectF carrier;
    if (m_doc && m_layoutView) {
        m_sheet = PrintJob::layoutFor(*m_doc, m_doc->print);
        if (!m_sheet.roll) {
            carrier = QRectF(0, 0, m_sheet.pageW, m_sheet.pageH);
            // labels sit in their cells as when printed (centred if the sizes differ)
            for (int i = 0; i < m_sheet.perPage(); ++i) {
                const QRectF r = m_sheet.labelRect(i);
                cells << r.topLeft() + QPointF((r.width() - m_doc->label.width) / 2, (r.height() - m_doc->label.height) / 2);
            }
        }
    }
    if (cells.isEmpty())
        cells << QPointF();
    const bool changed = cells != m_cells || carrier != m_carrier;
    m_cells = cells;
    m_carrier = carrier;
    updateCellContexts();
    return changed;
}

void LabelScene::updateCellContexts()
{
    m_cellCtx.clear();
    const int rows = m_data ? m_data->rowCount() : 0;
    for (int c = 0; c < m_cells.size(); ++c) {
        RenderContext ctx = m_ctx;
        ctx.cell = c + 1;
        ctx.sequence = m_ctx.sequence + c;
        if (rows > 0 && c > 0) {
            const int r = (m_record + c) % rows;
            ctx.values = &m_data->rows.at(r);
            ctx.recordNumber = r + 1;
        }
        m_cellCtx << ctx;
    }
}

const RenderContext& LabelScene::contextFor(int cell) const
{
    return cell >= 1 && cell <= m_cellCtx.size() ? m_cellCtx.at(cell - 1) : m_ctx;
}

void LabelScene::setLayoutView(bool on)
{
    if (m_layoutView == on)
        return;
    m_layoutView = on;
    const QList<LabelObject*> sel = selectedObjects();
    rebuild();
    selectObjects(sel);
}

QRectF LabelScene::cellRect(int cell) const
{
    if (!m_doc || cell < 1 || cell > m_cells.size())
        return QRectF();
    return QRectF(m_cells.at(cell - 1), QSizeF(m_doc->label.width, m_doc->label.height));
}

QRectF LabelScene::contentRect() const
{
    if (!m_doc)
        return QRectF();
    return m_carrier.isValid() ? m_carrier : QRectF(0, 0, m_doc->label.width, m_doc->label.height);
}

int LabelScene::cellAt(const QPointF& p) const
{
    int best = 1;
    double bestDist = 1e300;
    for (int c = 1; c <= m_cells.size(); ++c) {
        const QRectF r = cellRect(c);
        if (r.contains(p))
            return c;
        const QPointF d = r.center() - p;
        const double dist = d.x() * d.x() + d.y() * d.y();
        if (dist < bestDist) {
            bestDist = dist;
            best = c;
        }
    }
    return best;
}

QPointF LabelScene::toLabel(const QPointF& scenePos, int* cell) const
{
    const int c = cellAt(scenePos);
    if (cell)
        *cell = c;
    return scenePos - m_cells.value(c - 1);
}

void LabelScene::rebuild()
{
    clear();
    m_selected.clear();
    if (!m_doc)
        return;
    updateLayout();
    updateSceneRect();
    int z = 0;
    for (const auto& obj : m_doc->objects) {
        for (int c = 1; c <= m_cells.size(); ++c) {
            if (!obj->shownInCell(c))
                continue;
            auto* item = new ObjectItem(obj.get(), c, m_cells.at(c - 1));
            item->setZValue(z);
            addItem(item);
        }
        ++z;
    }
    update();
}

void LabelScene::refreshObject(LabelObject* obj)
{
    for (ObjectItem* it : itemsFor(obj))
        it->syncFromObject();
}

void LabelScene::refreshAll()
{
    if (updateLayout()) {
        // the layout changed: other cells, other copies
        const QList<LabelObject*> sel = selectedObjects();
        rebuild();
        selectObjects(sel);
        return;
    }
    updateSceneRect();
    for (QGraphicsItem* it : items())
        if (it->type() == ObjectItem::Type)
            static_cast<ObjectItem*>(it)->syncFromObject();
    update();
}

void LabelScene::syncCopies(ObjectItem* from)
{
    for (ObjectItem* it : itemsFor(from->object()))
        if (it != from)
            it->syncFromObject();
}

QList<ObjectItem*> LabelScene::itemsFor(const LabelObject* obj) const
{
    QList<ObjectItem*> out;
    for (QGraphicsItem* it : items(Qt::AscendingOrder))
        if (it->type() == ObjectItem::Type && static_cast<ObjectItem*>(it)->object() == obj)
            out << static_cast<ObjectItem*>(it);
    std::sort(out.begin(), out.end(), [](ObjectItem* a, ObjectItem* b) { return a->cell() < b->cell(); });
    return out;
}

ObjectItem* LabelScene::itemFor(const LabelObject* obj) const
{
    const QList<ObjectItem*> all = itemsFor(obj);
    return all.isEmpty() ? nullptr : all.first();
}

QList<LabelObject*> LabelScene::selectedObjects() const
{
    QList<LabelObject*> out;
    if (!m_doc)
        return out;
    QSet<const LabelObject*> sel;
    for (QGraphicsItem* it : selectedItems())
        if (it->type() == ObjectItem::Type)
            sel.insert(static_cast<ObjectItem*>(it)->object());
    for (const auto& o : m_doc->objects)
        if (sel.contains(o.get()))
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

void LabelScene::setContext(const RenderContext& ctx, const DataTable* data, int record)
{
    m_ctx = ctx;
    m_data = data;
    m_record = record;
    updateCellContexts();
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
    // the desk around the label follows the theme; the label itself stays white
    const bool dark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
    p->fillRect(rect, dark ? QColor(0x2b, 0x2e, 0x33) : QColor(0xdd, 0xe1, 0xe6));
    if (!m_doc)
        return;
    const double px = views().isEmpty() ? 0.25 : 1.0 / views().first()->transform().m11();
    if (m_carrier.isValid()) {
        // the sheet or the big roll label the labels sit on
        p->fillRect(m_carrier.translated(3 * px, 3 * px), QColor(0, 0, 0, 40));
        p->fillRect(m_carrier, QColor(0xf1, 0xf2, 0xf4));
    }
    for (const QPointF& origin : m_cells)
        drawLabelShape(p, origin, px);
}

void LabelScene::drawLabelShape(QPainter* p, const QPointF& origin, double px)
{
    const QRectF lr(origin, QSizeF(m_doc->label.width, m_doc->label.height));
    const double rad = m_doc->label.radius;
    QPainterPath shape;
    shape.addRoundedRect(lr, rad, rad);
    if (!m_carrier.isValid())
        p->fillPath(shape.translated(3 * px, 3 * px), QColor(0, 0, 0, 40));  // drop shadow
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
            p->drawLine(QPointF(lr.left() + x, lr.top()), QPointF(lr.left() + x, lr.bottom()));
        for (double y = step; y < lr.height(); y += step)
            p->drawLine(QPointF(lr.left(), lr.top() + y), QPointF(lr.right(), lr.top() + y));
        p->restore();
    }
}

void LabelScene::drawForeground(QPainter* p, const QRectF&)
{
    if (!m_doc)
        return;
    QPen pen(QColor(110, 110, 110), 0);
    pen.setCosmetic(true);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->setRenderHint(QPainter::Antialiasing, true);
    for (const QPointF& origin : m_cells)
        p->drawRoundedRect(QRectF(origin, QSizeF(m_doc->label.width, m_doc->label.height)), m_doc->label.radius,
                           m_doc->label.radius);
    if (m_carrier.isValid()) {
        QPen cp(QColor(110, 110, 110, 120), 0, Qt::DashLine);
        cp.setCosmetic(true);
        p->setPen(cp);
        p->drawRect(m_carrier);
    }
}

void LabelScene::mousePressEvent(QGraphicsSceneMouseEvent* e)
{
    m_snapBypass = e->modifiers() & Qt::AltModifier;
    if (m_tool != ToolSelect && e->button() == Qt::LeftButton) {
        emit insertRequested(int(m_tool), snapPoint(toLabel(e->scenePos())));
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

void LabelScene::contextMenuEvent(QGraphicsSceneContextMenuEvent* e)
{
    // objects of a multi-up layout: shown in every label or only in this one
    ObjectItem* item = nullptr;
    for (QGraphicsItem* it : items(e->scenePos()))
        if (it->type() == ObjectItem::Type) {
            item = static_cast<ObjectItem*>(it);
            break;
        }
    if (!item || m_cells.size() < 2) {
        QGraphicsScene::contextMenuEvent(e);
        return;
    }
    LabelObject* obj = item->object();
    const int cell = item->cell();
    QMenu menu;
    QAction* only = menu.addAction(tr("Только в этой этикетке (№ %1)").arg(cell));
    only->setCheckable(true);
    only->setChecked(obj->cell == cell);
    QAction* all = menu.addAction(tr("Во всех этикетках разметки"));
    all->setCheckable(true);
    all->setChecked(obj->cell <= 0);
    QAction* chosen = menu.exec(e->screenPos());
    if (chosen == only && obj->cell != cell)
        emit cellRequested(obj, cell);
    else if (chosen == all && obj->cell > 0)
        emit cellRequested(obj, 0);
    e->accept();
}

} // namespace lf
