// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QGraphicsView>
#include <QList>
#include <QUrl>
#include <QWidget>

namespace lf {

class LabelView;

class Ruler : public QWidget {
public:
    Ruler(Qt::Orientation o, LabelView* view);
    void setCursorPos(double mm);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Qt::Orientation m_o;
    LabelView* m_view;
    double m_cursor = -1e9;
};

// Graphics view with millimetre rulers, Ctrl+wheel zoom and keyboard nudging.
class LabelView : public QGraphicsView {
    Q_OBJECT
public:
    explicit LabelView(QWidget* parent = nullptr);

    double zoom() const;  // 1.0 = real size on screen
    void setZoom(double z);
    void zoomIn();
    void zoomOut();
    void fitToLabel(const QRectF& labelRect);
    double pixelsPerMm() const;
    void setCursorPos(const QPointF& mm);

signals:
    void zoomChanged(double zoom);
    void nudge(double dx, double dy);
    void deletePressed();
    // files dropped on the view; pos is the drop point in label millimetres
    void urlsDropped(const QList<QUrl>& urls, const QPointF& pos);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void layoutRulers();
    Ruler* m_hRuler;
    Ruler* m_vRuler;
    QWidget* m_corner;
};

} // namespace lf
