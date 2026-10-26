// SPDX-License-Identifier: GPL-3.0-or-later
#include "PrintJob.h"

#include "core/Renderer.h"

#include <QCoreApplication>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QRegularExpression>
#include <cmath>

namespace lf {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("PrintJob", s); }

QPageSize pageSizeFor(const SheetLayout& l)
{
    return QPageSize(QSizeF(l.pageW, l.pageH), QPageSize::Millimeter, QString(), QPageSize::ExactMatch);
}
} // namespace

QVector<int> PrintJob::parseRange(const QString& range, int count)
{
    QVector<int> out;
    const QString r = range.trimmed();
    if (r.isEmpty()) {
        for (int i = 0; i < count; ++i)
            out << i;
        return out;
    }
    static const QRegularExpression sep(QStringLiteral("[,;\\s]+"));
    for (const QString& part : r.split(sep, Qt::SkipEmptyParts)) {
        const int dash = part.indexOf(QLatin1Char('-'));
        int a, b;
        if (dash < 0) {
            a = b = part.toInt();
        } else {
            a = part.left(dash).toInt();
            const QString rest = part.mid(dash + 1);
            b = rest.isEmpty() ? count : rest.toInt();
        }
        if (a < 1)
            a = 1;
        if (b > count)
            b = count;
        for (int i = a; i <= b; ++i)
            out << i - 1;
    }
    return out;
}

PrintJob::PrintJob(const LabelDocument& doc, const DataTable& data, const PrintSettings& settings,
                   const QString& baseDir)
    : m_doc(doc), m_data(data), m_settings(settings), m_baseDir(baseDir)
{
    m_layout = SheetLayoutLibrary::find(settings.layoutId, doc.label.width, doc.label.height);
    const int copies = std::max(1, settings.copies);
    int qtyCol = -1;
    if (!settings.quantityField.isEmpty())
        qtyCol = data.columns.indexOf(settings.quantityField);
    int seq = 1;
    if (data.isEmpty() || data.columns.isEmpty()) {
        for (int c = 0; c < copies; ++c)
            m_instances.push_back({-1, seq++});
        return;
    }
    for (int rec : parseRange(settings.recordRange, data.rowCount())) {
        int qty = copies;
        if (qtyCol >= 0) {
            bool ok = false;
            const int q = data.rows.at(rec).value(qtyCol).trimmed().toInt(&ok);
            qty = ok ? std::max(0, q) * copies : copies;
        }
        for (int c = 0; c < qty; ++c)
            m_instances.push_back({rec, seq++});
    }
}

int PrintJob::pageCount() const
{
    if (m_instances.isEmpty())
        return 0;
    if (m_layout.roll)
        return m_instances.size();
    const int per = m_layout.perPage();
    const int skip = std::clamp(m_settings.startPosition, 1, per) - 1;
    return (skip + m_instances.size() + per - 1) / per;
}

RenderContext PrintJob::contextFor(const LabelInstance& li) const
{
    RenderContext ctx;
    if (li.record >= 0) {
        ctx.columns = &m_data.columns;
        ctx.values = &m_data.rows.at(li.record);
        ctx.recordNumber = li.record + 1;
    }
    ctx.sequence = li.sequence;
    ctx.baseDir = m_baseDir;
    return ctx;
}

void PrintJob::renderPage(QPainter* p, int page, double deviceDpi) const
{
    const int per = m_layout.roll ? 1 : m_layout.perPage();
    const int skip = m_layout.roll ? 0 : std::clamp(m_settings.startPosition, 1, per) - 1;
    RenderOptions opt;
    opt.outline = m_settings.printOutlines;
    for (int i = 0; i < per; ++i) {
        const int idx = page * per + i - skip;
        if (idx < 0 || idx >= m_instances.size())
            continue;
        const QRectF cell = m_layout.roll ? QRectF(0, 0, m_layout.pageW, m_layout.pageH) : m_layout.labelRect(i);
        // centre the label in its cell if the sizes differ
        const double dx = (cell.width() - m_doc.label.width) / 2.0;
        const double dy = (cell.height() - m_doc.label.height) / 2.0;
        RenderContext ctx = contextFor(m_instances.at(idx));
        ctx.deviceDpi = deviceDpi;
        p->save();
        p->translate(cell.x() + dx + m_settings.offsetX, cell.y() + dy + m_settings.offsetY);
        renderLabel(p, m_doc, ctx, opt);
        p->restore();
    }
}

bool PrintJob::printTo(QPrinter* printer, QString* err) const
{
    if (m_instances.isEmpty()) {
        *err = tr("Нет этикеток для печати");
        return false;
    }
    printer->setFullPage(true);
    printer->setPageOrientation(QPageLayout::Portrait);
    if (!printer->setPageSize(pageSizeFor(m_layout)))
        printer->setPageSize(pageSizeFor(m_layout));
    printer->setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    QPainter painter;
    if (!painter.begin(printer)) {
        *err = tr("Не удалось начать печать");
        return false;
    }
    const double dpi = printer->resolution();
    const int pages = pageCount();
    for (int pg = 0; pg < pages; ++pg) {
        if (pg > 0 && !printer->newPage()) {
            *err = tr("Ошибка при переходе на новую страницу");
            painter.end();
            return false;
        }
        painter.save();
        painter.scale(dpi / MM_PER_INCH, dpi / MM_PER_INCH);
        renderPage(&painter, pg, printer->outputFormat() == QPrinter::NativeFormat ? dpi : 0);
        painter.restore();
    }
    painter.end();
    return true;
}

bool PrintJob::exportPdf(const QString& path, QString* err) const
{
    if (m_instances.isEmpty()) {
        *err = tr("Нет этикеток для печати");
        return false;
    }
    QPdfWriter writer(path);
    writer.setTitle(m_doc.title.isEmpty() ? QStringLiteral("LabelForge") : m_doc.title);
    writer.setCreator(QStringLiteral("LabelForge"));
    writer.setResolution(1200);
    writer.setPageSize(pageSizeFor(m_layout));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    QPainter painter;
    if (!painter.begin(&writer)) {
        *err = tr("Не удалось создать PDF: %1").arg(path);
        return false;
    }
    const double scale = writer.resolution() / MM_PER_INCH;
    const int pages = pageCount();
    for (int pg = 0; pg < pages; ++pg) {
        if (pg > 0)
            writer.newPage();
        painter.save();
        painter.scale(scale, scale);
        renderPage(&painter, pg, 0);
        painter.restore();
    }
    painter.end();
    return true;
}

bool PrintJob::exportPng(const QString& pattern, double dpi, QString* err) const
{
    int n = 1;
    const int width = QString::number(m_instances.size()).size();
    for (const LabelInstance& li : m_instances) {
        const QImage img = renderLabelImage(m_doc, contextFor(li), dpi, false);
        const QString path = pattern.contains(QLatin1String("%1"))
                                 ? pattern.arg(n, width, 10, QLatin1Char('0'))
                                 : pattern;
        if (!img.save(path, "PNG")) {
            *err = tr("Не удалось сохранить %1").arg(path);
            return false;
        }
        if (!pattern.contains(QLatin1String("%1")))
            break;
        ++n;
    }
    return true;
}

} // namespace lf
