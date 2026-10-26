// SPDX-License-Identifier: GPL-3.0-or-later
#include "PagePreview.h"

#include "print/PrintJob.h"

#include <QPainter>

namespace lf {

PagePreview::PagePreview(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(200, 240);
}

void PagePreview::setSheet(const SheetLayout& l)
{
    m_layout = l;
    m_job = nullptr;
    update();
}

void PagePreview::setJob(const PrintJob* job, int page)
{
    m_job = job;
    m_page = page;
    if (job)
        m_layout = job->layout();
    update();
}

void PagePreview::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), palette().color(QPalette::Mid).lighter(150));
    const double pw = std::max(1.0, m_layout.pageW), ph = std::max(1.0, m_layout.pageH);
    const double margin = 12;
    const double s = std::min((width() - 2 * margin) / pw, (height() - 2 * margin) / ph);
    const QRectF page((width() - pw * s) / 2, (height() - ph * s) / 2, pw * s, ph * s);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(page.translated(3, 3), QColor(0, 0, 0, 50));
    p.fillRect(page, Qt::white);
    p.save();
    p.translate(page.topLeft());
    p.scale(s, s);
    if (m_job) {
        m_job->renderPage(&p, m_page, 0);
    }
    // label outlines
    QPen pen(QColor(33, 113, 181, 150), 0, Qt::DashLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const int n = m_layout.roll ? 1 : m_layout.perPage();
    for (int i = 0; i < n; ++i) {
        const QRectF r = m_layout.roll ? QRectF(0, 0, m_layout.labelW, m_layout.labelH) : m_layout.labelRect(i);
        p.drawRoundedRect(r, m_layout.radius, m_layout.radius);
    }
    p.restore();
    p.setPen(QColor(120, 120, 120));
    p.drawRect(page);
}

} // namespace lf
