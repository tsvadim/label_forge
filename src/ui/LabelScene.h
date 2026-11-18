// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/RenderContext.h"
#include "print/SheetLayout.h"

#include <QGraphicsScene>
#include <QList>
#include <QSet>
#include <QVector>

namespace lf {

class LabelDocument;
class LabelObject;
class ObjectItem;
struct DataTable;

class LabelScene : public QGraphicsScene {
    Q_OBJECT
public:
    enum Tool { ToolSelect, ToolText, ToolRect, ToolEllipse, ToolLine, ToolImage, ToolBarcode, ToolQr, ToolShape };

    explicit LabelScene(QObject* parent = nullptr);

    void setDocument(LabelDocument* doc);
    LabelDocument* document() const { return m_doc; }
    void rebuild();
    void refreshObject(LabelObject* obj);
    void refreshAll();
    ObjectItem* itemFor(const LabelObject* obj) const;  // the first copy
    QList<ObjectItem*> itemsFor(const LabelObject* obj) const;
    bool isObjectSelected(const LabelObject* obj) const { return m_selected.contains(obj); }
    QList<LabelObject*> selectedObjects() const;  // in document (z) order
    void selectObjects(const QList<LabelObject*>& objs);

    const RenderContext& context() const { return m_ctx; }
    // ctx is the current record; with data, the other labels of the layout
    // show the following records, as they will be printed
    void setContext(const RenderContext& ctx, const DataTable* data = nullptr, int record = 0);
    const RenderContext& contextFor(int cell) const;

    // Whole-layout view: the sheet or roll label with every label on it.
    // Objects shared by all labels appear in each one and move together.
    void setLayoutView(bool on);
    bool layoutView() const { return m_layoutView; }
    int cellCount() const { return int(m_cells.size()); }
    QRectF cellRect(int cell) const;  // 1-based, scene coordinates
    QRectF contentRect() const;       // the carrier, or the label
    int cellAt(const QPointF& scenePos) const;
    // scene point -> coordinates of the label under it (or the nearest one)
    QPointF toLabel(const QPointF& scenePos, int* cell = nullptr) const;
    void syncCopies(ObjectItem* from);  // the other copies follow an edited one

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
    void insertRequested(int tool, const QPointF& pos);  // pos in label coordinates
    void cellRequested(LabelObject* obj, int cell);      // context menu: 0 = every label
    void cursorMoved(const QPointF& pos);

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* e) override;
    void contextMenuEvent(QGraphicsSceneContextMenuEvent* e) override;

private:
    void updateSceneRect();
    bool updateLayout();  // true if the cells moved or changed in number
    void updateCellContexts();
    void drawLabelShape(QPainter* p, const QPointF& origin, double px);

    LabelDocument* m_doc = nullptr;
    RenderContext m_ctx;
    double m_grid = 1.0;
    bool m_snap = true;
    bool m_showGrid = true;
    bool m_frames = true;
    bool m_edited = false;
    bool m_snapBypass = false;  // Alt held during the current mouse gesture
    Tool m_tool = ToolSelect;

    bool m_layoutView = false;
    SheetLayout m_sheet;
    QRectF m_carrier;           // empty: no carrier shown
    QVector<QPointF> m_cells;   // label origins, one per cell
    QVector<RenderContext> m_cellCtx;
    const DataTable* m_data = nullptr;
    int m_record = 0;
    QSet<const LabelObject*> m_selected;
};

} // namespace lf
