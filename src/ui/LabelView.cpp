// SPDX-License-Identifier: GPL-3.0-or-later
#include "LabelView.h"

#include <QKeyEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

namespace lf {

namespace {
constexpr int RULER = 22;

double screenPxPerMm(const QWidget* w)
{
    const double dpi = w->logicalDpiX() > 0 ? w->logicalDpiX() : 96.0;
    return dpi / 25.4;
}
} // namespace

// ------------------------------------------------------------------ Ruler

Ruler::Ruler(Qt::Orientation o, LabelView* view) : QWidget(view), m_o(o), m_view(view) {}

void Ruler::setCursorPos(double mm)
{
    m_cursor = mm;
    update();
}

void Ruler::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const double ppm = m_view->pixelsPerMm();
    // scene coordinate at the ruler's start
    const QPointF origin = m_view->mapToScene(QPoint(0, 0));
    const double start = m_o == Qt::Horizontal ? origin.x() : origin.y();
    const int length = m_o == Qt::Horizontal ? width() : height();

    double major = 10;
    const double candidates[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
    for (double c : candidates)
        if (c * ppm >= 45) {
            major = c;
            break;
        }
    const double minor = major / (major == 2 || major == 20 || major == 200 ? 4 : (major == 5 || major == 50) ? 5 : 10);

    QFont f = font();
    f.setPixelSize(9);
    p.setFont(f);
    p.setPen(palette().color(QPalette::WindowText));
    const double end = start + length / ppm;
    for (double v = std::floor(start / minor) * minor; v <= end; v += minor) {
        const double pos = (v - start) * ppm;
        const bool isMajor = std::abs(std::remainder(v, major)) < minor * 0.01;
        const int len = isMajor ? RULER - 4 : RULER / 4;
        if (m_o == Qt::Horizontal) {
            p.drawLine(QPointF(pos, RULER), QPointF(pos, RULER - len));
            if (isMajor)
                p.drawText(QPointF(pos + 2, 10), QString::number(std::round(v)));
        } else {
            p.drawLine(QPointF(RULER, pos), QPointF(RULER - len, pos));
            if (isMajor) {
                p.save();
                p.translate(10, pos + 2);
                p.rotate(-90);
                p.drawText(QPointF(-p.fontMetrics().horizontalAdvance(QString::number(std::round(v))) - 2, 0),
                           QString::number(std::round(v)));
                p.restore();
            }
        }
    }
    p.setPen(QColor(33, 113, 181));
    const double cp = (m_cursor - start) * ppm;
    if (cp >= 0 && cp <= length) {
        if (m_o == Qt::Horizontal)
            p.drawLine(QPointF(cp, 0), QPointF(cp, RULER));
        else
            p.drawLine(QPointF(0, cp), QPointF(RULER, cp));
    }
    p.setPen(palette().color(QPalette::Mid));
    if (m_o == Qt::Horizontal)
        p.drawLine(0, RULER - 1, width(), RULER - 1);
    else
        p.drawLine(RULER - 1, 0, RULER - 1, height());
}

// ------------------------------------------------------------------ LabelView

LabelView::LabelView(QWidget* parent) : QGraphicsView(parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setViewportMargins(RULER, RULER, 0, 0);
    m_hRuler = new Ruler(Qt::Horizontal, this);
    m_vRuler = new Ruler(Qt::Vertical, this);
    m_corner = new QWidget(this);
    m_corner->setAutoFillBackground(true);
    setZoom(3.0);
}

double LabelView::pixelsPerMm() const { return transform().m11(); }

double LabelView::zoom() const { return pixelsPerMm() / screenPxPerMm(this); }

void LabelView::setZoom(double z)
{
    z = std::clamp(z, 0.1, 40.0);
    const double s = z * screenPxPerMm(this);
    setTransform(QTransform::fromScale(s, s));
    m_hRuler->update();
    m_vRuler->update();
    emit zoomChanged(z);
}

void LabelView::zoomIn() { setZoom(zoom() * 1.25); }
void LabelView::zoomOut() { setZoom(zoom() / 1.25); }

void LabelView::fitToLabel(const QRectF& r)
{
    const QRectF target = r.adjusted(-r.width() * 0.08 - 2, -r.height() * 0.08 - 2, r.width() * 0.08 + 2,
                                     r.height() * 0.08 + 2);
    const QSize vp = viewport()->size();
    if (vp.width() < 10 || vp.height() < 10)
        return;
    const double s = std::min(vp.width() / target.width(), vp.height() / target.height());
    setZoom(s / screenPxPerMm(this));
    centerOn(r.center());
}

void LabelView::setCursorPos(const QPointF& mm)
{
    m_hRuler->setCursorPos(mm.x());
    m_vRuler->setCursorPos(mm.y());
}

void LabelView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const double steps = e->angleDelta().y() / 120.0;
        setZoom(zoom() * std::pow(1.15, steps));
        e->accept();
        return;
    }
    QGraphicsView::wheelEvent(e);
}

void LabelView::keyPressEvent(QKeyEvent* e)
{
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 1.0 : 0.1;
    switch (e->key()) {
    case Qt::Key_Left: emit nudge(-step, 0); return;
    case Qt::Key_Right: emit nudge(step, 0); return;
    case Qt::Key_Up: emit nudge(0, -step); return;
    case Qt::Key_Down: emit nudge(0, step); return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace: emit deletePressed(); return;
    default: break;
    }
    QGraphicsView::keyPressEvent(e);
}

void LabelView::layoutRulers()
{
    const QRect vr = viewport()->geometry();
    m_hRuler->setGeometry(vr.left(), vr.top() - RULER, vr.width(), RULER);
    m_vRuler->setGeometry(vr.left() - RULER, vr.top(), RULER, vr.height());
    m_corner->setGeometry(vr.left() - RULER, vr.top() - RULER, RULER, RULER);
}

void LabelView::resizeEvent(QResizeEvent* e)
{
    QGraphicsView::resizeEvent(e);
    layoutRulers();
}

void LabelView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    m_hRuler->update();
    m_vRuler->update();
}

} // namespace lf
