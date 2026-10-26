// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SheetLayout.h"
#include "core/DataSource.h"
#include "core/LabelDocument.h"

#include <QVector>
#include <functional>

class QPainter;
class QPrinter;

namespace lf {

struct LabelInstance {
    int record = -1;  // row index in the data table, -1 = no data
    int sequence = 1;
};

// Expands a document + data into concrete labels and pages.
class PrintJob {
public:
    PrintJob(const LabelDocument& doc, const DataTable& data, const PrintSettings& settings,
             const QString& baseDir = QString());

    const SheetLayout& layout() const { return m_layout; }
    const QVector<LabelInstance>& instances() const { return m_instances; }
    int labelCount() const { return m_instances.size(); }
    int pageCount() const;

    RenderContext contextFor(const LabelInstance& li) const;
    // painter in millimetres, origin at the page's top-left corner
    void renderPage(QPainter* p, int page, double deviceDpi) const;

    bool printTo(QPrinter* printer, QString* err) const;
    bool exportPdf(const QString& path, QString* err) const;
    // one PNG per label: path pattern with %1 for the number, e.g. "label_%1.png"
    bool exportPng(const QString& pattern, double dpi, QString* err) const;

    // "1-3, 5" -> {0,1,2,4} (0-based), empty -> all
    static QVector<int> parseRange(const QString& range, int count);

private:
    const LabelDocument& m_doc;
    const DataTable& m_data;
    PrintSettings m_settings;
    SheetLayout m_layout;
    QString m_baseDir;
    QVector<LabelInstance> m_instances;
};

} // namespace lf
