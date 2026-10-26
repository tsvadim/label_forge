// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QGraphicsItem>
#include <QTransform>

namespace lf {

class LabelObject;
class LabelScene;

// Scene item that displays one LabelObject and edits its geometry:
// move, resize with eight handles and rotate with the round handle.
class ObjectItem : public QGraphicsItem {
public:
    enum { Type = UserType + 1 };
    explicit ObjectItem(LabelObject* obj);

    int type() const override { return Type; }
    LabelObject* object() const { return m_obj; }
    void syncFromObject();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter* p, const QStyleOptionGraphicsItem* opt, QWidget* w) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* e) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* e) override;

private:
    LabelScene* labelScene() const;
    double handleSize() const;  // millimetres, constant on screen
    QPointF handlePos(int i) const;
    int handleAt(const QPointF& local) const;
    void applyGeometry(double w, double h, const QPointF& topLeftInStartFrame);

    LabelObject* m_obj;
    int m_drag = -1;  // -1 move, 0..7 resize, 8 rotate
    bool m_internal = false;
    bool m_changed = false;
    QTransform m_startTransform;
    QPointF m_startPos;
    double m_startW = 0, m_startH = 0;
};

} // namespace lf
