// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/RenderContext.h"

#include <QGraphicsScene>
#include <QList>

namespace lf {

class LabelDocument;
class LabelObject;
class ObjectItem;

class LabelScene : public QGraphicsScene {
    Q_OBJECT
public:
    enum Tool { ToolSelect, ToolText, ToolRect, ToolEllipse, ToolLine, ToolImage, ToolBarcode, ToolQr };

    explicit LabelScene(QObject* parent = nullptr);

    void setDocument(LabelDocument* doc);
    LabelDocument* document() const { return m_doc; }
    void rebuild();
    void refreshObject(LabelObject* obj);
    void refreshAll();
    ObjectItem* itemFor(const LabelObject* obj) const;
    QList<LabelObject*> selectedObjects() const;  // in document (z) order
    void selectObjects(const QList<LabelObject*>& objs);

    const RenderContext& context() const { return m_ctx; }
    void setContext(const RenderContext& ctx);

    double gridStep() const { return m_grid; }
    void setGridStep(double mm);
    bool snapEnabled() const { return m_snap; }
    void setSnapEnabled(bool on) { m_snap = on; }
    bool gridVisible() const { return m_showGrid; }
    void setGridVisible(bool on);
    bool showObjectFrames() const { return m_frames; }
    void setShowObjectFrames(bool on);
    QPointF snapPoint(const QPointF& p) const;

    Tool tool() const { return m_tool; }
    void setTool(Tool t);

    void markEdited() { m_edited = true; }
    void notifyEditFinished();

signals:
    void geometryEdited();
    void insertRequested(int tool, const QPointF& pos);
    void cursorMoved(const QPointF& pos);

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;

private:
    void updateSceneRect();

    LabelDocument* m_doc = nullptr;
    RenderContext m_ctx;
    double m_grid = 1.0;
    bool m_snap = true;
    bool m_showGrid = true;
    bool m_frames = true;
    bool m_edited = false;
    bool m_snapBypass = false;  // Alt held during the current mouse gesture
    Tool m_tool = ToolSelect;
};

} // namespace lf
