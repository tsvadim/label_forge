// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/LabelDocument.h"

#include <QDialog>
#include <memory>

class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QGroupBox;
class QPushButton;
class QToolButton;
class QPrinter;

namespace lf {

class PrintJob;
class PagePreview;

class PrintDialog : public QDialog {
    Q_OBJECT
public:
    PrintDialog(const LabelDocument& doc, const DataTable& data, const QString& baseDir, QWidget* parent = nullptr);
    ~PrintDialog() override;
    PrintSettings settings() const;

private:
    enum Output { OutPrinter, OutPdf, OutThermal, OutThermalFile, OutPng };
    void fillLayouts();
    void updateJob();
    void updateOutputUi();
    void run();
    void showSystemPreview();
    bool printToPrinter();
    bool exportPdf();
    bool sendThermal(bool toFile);
    bool exportPng();

    const LabelDocument& m_doc;
    const DataTable& m_data;
    QString m_baseDir;
    PrintSettings m_settings;
    std::unique_ptr<PrintJob> m_job;
    int m_page = 0;

    QComboBox* m_layout;
    QLabel* m_layoutInfo;
    QLineEdit* m_range;
    QSpinBox* m_copies;
    QComboBox* m_qtyField;
    QSpinBox* m_start;
    QCheckBox* m_outlines;
    QDoubleSpinBox* m_offX;
    QDoubleSpinBox* m_offY;
    QComboBox* m_output;
    QGroupBox* m_thermalBox;
    QComboBox* m_lang;
    QComboBox* m_dpi;
    QSpinBox* m_darkness;
    QSpinBox* m_speed;
    QDoubleSpinBox* m_gap;
    QCheckBox* m_invert;
    QComboBox* m_thermalPrinter;
    QLabel* m_summary;
    PagePreview* m_preview;
    QToolButton* m_prev;
    QToolButton* m_next;
    QLabel* m_pageLabel;
    QPushButton* m_run;
    QPushButton* m_sysPreview;
};

} // namespace lf
