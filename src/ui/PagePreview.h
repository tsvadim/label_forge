// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "print/SheetLayout.h"

#include <QWidget>

namespace lf {

class PrintJob;

// Paints a scaled page: either the bare sheet layout or a page of a print job.
class PagePreview : public QWidget {
    Q_OBJECT
public:
    explicit PagePreview(QWidget* parent = nullptr);
    void setSheet(const SheetLayout& l);
    void setJob(const PrintJob* job, int page);
    QSize sizeHint() const override { return {320, 420}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    SheetLayout m_layout;
    const PrintJob* m_job = nullptr;
    int m_page = 0;
};

} // namespace lf
