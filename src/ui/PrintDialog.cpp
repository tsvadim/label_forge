// SPDX-License-Identifier: GPL-3.0-or-later
#include "PrintDialog.h"

#include "PagePreview.h"
#include "SheetLayoutDialog.h"
#include "print/PrintJob.h"
#include "print/ThermalExport.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf {

PrintDialog::PrintDialog(const LabelDocument& doc, const DataTable& data, const QString& baseDir, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_data(data), m_baseDir(baseDir), m_settings(doc.print)
{
    setWindowTitle(tr("Печать этикеток"));
    resize(1000, 680);
    auto* root = new QHBoxLayout(this);
    auto* left = new QVBoxLayout;
    root->addLayout(left, 0);

    // ---------------- layout
    auto* gLayout = new QGroupBox(tr("Макет"));
    auto* fl = new QFormLayout(gLayout);
    auto* lrow = new QHBoxLayout;
    m_layout = new QComboBox;
    m_layout->setMinimumWidth(260);
    auto* manage = new QToolButton;
    manage->setText(tr("…"));
    manage->setToolTip(tr("Шаблоны листов: создать, изменить"));
    lrow->addWidget(m_layout, 1);
    lrow->addWidget(manage);
    fl->addRow(tr("Лист:"), lrow);
    m_layoutInfo = new QLabel;
    m_layoutInfo->setWordWrap(true);
    fl->addRow(m_layoutInfo);
    m_start = new QSpinBox;
    m_start->setRange(1, 1000);
    m_start->setValue(m_settings.startPosition);
    m_start->setToolTip(tr("Для частично использованного листа: номер первой свободной этикетки"));
    fl->addRow(tr("Начать с позиции:"), m_start);
    left->addWidget(gLayout);

    // ---------------- data
    auto* gData = new QGroupBox(tr("Записи и копии"));
    auto* fd = new QFormLayout(gData);
    m_range = new QLineEdit(m_settings.recordRange);
    m_range->setPlaceholderText(tr("все, или например 1-5, 8"));
    m_range->setEnabled(!data.columns.isEmpty());
    fd->addRow(tr("Записи:"), m_range);
    m_copies = new QSpinBox;
    m_copies->setRange(1, 100000);
    m_copies->setValue(m_settings.copies);
    fd->addRow(tr("Копий каждой:"), m_copies);
    m_qtyField = new QComboBox;
    m_qtyField->addItem(tr("— не использовать —"), QString());
    for (const QString& c : data.columns)
        m_qtyField->addItem(c, c);
    m_qtyField->setCurrentIndex(std::max(0, m_qtyField->findData(m_settings.quantityField)));
    m_qtyField->setEnabled(!data.columns.isEmpty());
    m_qtyField->setToolTip(tr("Поле с количеством этикеток для каждой записи"));
    fd->addRow(tr("Количество из поля:"), m_qtyField);
    m_outlines = new QCheckBox(tr("Печатать контуры этикеток (тест)"));
    m_outlines->setChecked(m_settings.printOutlines);
    fd->addRow(m_outlines);
    left->addWidget(gData);

    // ---------------- calibration
    auto* gCal = new QGroupBox(tr("Калибровка смещения"));
    auto* fc = new QHBoxLayout(gCal);
    m_offX = new QDoubleSpinBox;
    m_offY = new QDoubleSpinBox;
    for (QDoubleSpinBox* s : {m_offX, m_offY}) {
        s->setRange(-50, 50);
        s->setDecimals(2);
        s->setSingleStep(0.25);
        s->setSuffix(tr(" мм"));
    }
    m_offX->setValue(m_settings.offsetX);
    m_offY->setValue(m_settings.offsetY);
    fc->addWidget(new QLabel(tr("X:")));
    fc->addWidget(m_offX);
    fc->addWidget(new QLabel(tr("Y:")));
    fc->addWidget(m_offY);
    left->addWidget(gCal);

    // ---------------- output
    auto* gOut = new QGroupBox(tr("Вывод"));
    auto* fo = new QFormLayout(gOut);
    m_output = new QComboBox;
    m_output->addItem(tr("Принтер (через драйвер Windows)"), int(OutPrinter));
    m_output->addItem(tr("Файл PDF"), int(OutPdf));
    m_output->addItem(tr("Термопринтер напрямую (ZPL / TSPL)"), int(OutThermal));
    m_output->addItem(tr("Файл команд принтера (.zpl / .prn)"), int(OutThermalFile));
    m_output->addItem(tr("Изображения PNG"), int(OutPng));
    fo->addRow(tr("Куда:"), m_output);
    left->addWidget(gOut);

    m_thermalBox = new QGroupBox(tr("Термопринтер"));
    auto* ft = new QFormLayout(m_thermalBox);
    m_lang = new QComboBox;
    m_lang->addItem(tr("ZPL II (Zebra и совместимые)"), QStringLiteral("zpl"));
    m_lang->addItem(tr("TSPL (TSC, Xprinter, Gprinter…)"), QStringLiteral("tspl"));
    m_lang->setCurrentIndex(std::max(0, m_lang->findData(m_settings.thermalLang)));
    ft->addRow(tr("Язык:"), m_lang);
    m_thermalPrinter = new QComboBox;
    m_thermalPrinter->addItems(QPrinterInfo::availablePrinterNames());
    if (!m_settings.thermalPrinter.isEmpty())
        m_thermalPrinter->setCurrentText(m_settings.thermalPrinter);
    else if (!QPrinterInfo::defaultPrinterName().isEmpty())
        m_thermalPrinter->setCurrentText(QPrinterInfo::defaultPrinterName());
    ft->addRow(tr("Принтер:"), m_thermalPrinter);
    m_dpi = new QComboBox;
    for (int d : {203, 300, 600})
        m_dpi->addItem(QStringLiteral("%1 dpi").arg(d), d);
    m_dpi->setCurrentIndex(std::max(0, m_dpi->findData(m_settings.thermalDpi)));
    ft->addRow(tr("Разрешение:"), m_dpi);
    m_darkness = new QSpinBox;
    m_darkness->setRange(0, 30);
    m_darkness->setValue(m_settings.darkness);
    ft->addRow(tr("Плотность (0–30):"), m_darkness);
    m_speed = new QSpinBox;
    m_speed->setRange(1, 14);
    m_speed->setValue(m_settings.speed);
    ft->addRow(tr("Скорость, дюйм/с:"), m_speed);
    m_gap = new QDoubleSpinBox;
    m_gap->setRange(0, 20);
    m_gap->setDecimals(1);
    m_gap->setSuffix(tr(" мм"));
    m_gap->setValue(m_settings.gapMm);
    ft->addRow(tr("Зазор (TSPL):"), m_gap);
    m_invert = new QCheckBox(tr("Инвертировать растр (TSPL)"));
    m_invert->setChecked(m_settings.tsplInvert);
    m_invert->setToolTip(tr("Если принтер печатает негатив — переключите"));
    ft->addRow(m_invert);
    left->addWidget(m_thermalBox);
    left->addStretch();

    // ---------------- right: preview
    auto* right = new QVBoxLayout;
    root->addLayout(right, 1);
    m_preview = new PagePreview;
    right->addWidget(m_preview, 1);
    auto* nav = new QHBoxLayout;
    m_prev = new QToolButton;
    m_prev->setText(QStringLiteral("◀"));
    m_next = new QToolButton;
    m_next->setText(QStringLiteral("▶"));
    m_pageLabel = new QLabel;
    nav->addStretch();
    nav->addWidget(m_prev);
    nav->addWidget(m_pageLabel);
    nav->addWidget(m_next);
    nav->addStretch();
    right->addLayout(nav);
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    right->addWidget(m_summary);
    auto* bb = new QDialogButtonBox;
    m_sysPreview = bb->addButton(tr("Предпросмотр"), QDialogButtonBox::ActionRole);
    m_run = bb->addButton(tr("Печать"), QDialogButtonBox::AcceptRole);
    m_run->setDefault(true);
    bb->addButton(QDialogButtonBox::Close);
    right->addWidget(bb);

    fillLayouts();
    QSettings st;
    m_output->setCurrentIndex(std::max(0, m_output->findData(st.value(QStringLiteral("print/output"), 0).toInt())));

    connect(manage, &QToolButton::clicked, this, [this] {
        SheetLayoutDialog dlg(m_layout->currentData().toString(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedId().isEmpty())
            m_settings.layoutId = dlg.selectedId();
        fillLayouts();
        updateJob();
    });
    connect(m_layout, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.layoutId = m_layout->currentData().toString();
        updateJob();
    });
    connect(m_range, &QLineEdit::textChanged, this, &PrintDialog::updateJob);
    connect(m_copies, &QSpinBox::valueChanged, this, &PrintDialog::updateJob);
    connect(m_qtyField, &QComboBox::currentIndexChanged, this, &PrintDialog::updateJob);
    connect(m_start, &QSpinBox::valueChanged, this, &PrintDialog::updateJob);
    connect(m_outlines, &QCheckBox::toggled, this, &PrintDialog::updateJob);
    connect(m_offX, &QDoubleSpinBox::valueChanged, this, &PrintDialog::updateJob);
    connect(m_offY, &QDoubleSpinBox::valueChanged, this, &PrintDialog::updateJob);
    connect(m_output, &QComboBox::currentIndexChanged, this, &PrintDialog::updateOutputUi);
    connect(m_prev, &QToolButton::clicked, this, [this] {
        m_page = std::max(0, m_page - 1);
        updateJob();
    });
    connect(m_next, &QToolButton::clicked, this, [this] {
        ++m_page;
        updateJob();
    });
    connect(m_run, &QPushButton::clicked, this, &PrintDialog::run);
    connect(m_sysPreview, &QPushButton::clicked, this, &PrintDialog::showSystemPreview);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateOutputUi();
    updateJob();
}

PrintDialog::~PrintDialog() = default;

void PrintDialog::fillLayouts()
{
    const QSignalBlocker b(m_layout);
    m_layout->clear();
    m_layout->addItem(tr("Рулон / термопринтер (%1×%2 мм)").arg(m_doc.label.width).arg(m_doc.label.height),
                      QStringLiteral("roll"));
    for (const SheetLayout& l : SheetLayoutLibrary::all())
        if (l.id != QLatin1String("roll"))
            m_layout->addItem(l.name, l.id);
    m_layout->setCurrentIndex(std::max(0, m_layout->findData(m_settings.layoutId)));
}

PrintSettings PrintDialog::settings() const
{
    PrintSettings s = m_settings;
    s.layoutId = m_layout->currentData().toString();
    s.recordRange = m_range->text().trimmed();
    s.copies = m_copies->value();
    s.quantityField = m_qtyField->currentData().toString();
    s.startPosition = m_start->value();
    s.printOutlines = m_outlines->isChecked();
    s.offsetX = m_offX->value();
    s.offsetY = m_offY->value();
    s.thermalLang = m_lang->currentData().toString();
    s.thermalDpi = m_dpi->currentData().toInt();
    s.darkness = m_darkness->value();
    s.speed = m_speed->value();
    s.gapMm = m_gap->value();
    s.tsplInvert = m_invert->isChecked();
    s.thermalPrinter = m_thermalPrinter->currentText();
    return s;
}

void PrintDialog::updateOutputUi()
{
    const auto out = Output(m_output->currentData().toInt());
    m_thermalBox->setVisible(out == OutThermal || out == OutThermalFile);
    m_thermalPrinter->setEnabled(out == OutThermal);
    m_sysPreview->setVisible(out == OutPrinter);
    static const char* labels[] = {"Печать", "Сохранить PDF", "Отправить на принтер", "Сохранить файл",
                                   "Сохранить PNG"};
    m_run->setText(tr(labels[int(out)]));
    QSettings().setValue(QStringLiteral("print/output"), int(out));
}

void PrintDialog::updateJob()
{
    m_settings = settings();
    m_job = std::make_unique<PrintJob>(m_doc, m_data, m_settings, m_baseDir);
    const SheetLayout& l = m_job->layout();
    m_start->setMaximum(l.roll ? 1 : l.perPage());
    m_start->setEnabled(!l.roll);
    QString info = l.summary();
    if (!l.roll && (std::abs(l.labelW - m_doc.label.width) > 0.3 || std::abs(l.labelH - m_doc.label.height) > 0.3))
        info += tr("<br><span style='color:#b00'>Размер этикетки %1×%2 мм не совпадает с ячейкой листа — "
                   "этикетка будет отцентрирована в ячейке.</span>")
                    .arg(m_doc.label.width)
                    .arg(m_doc.label.height);
    m_layoutInfo->setText(info);
    const int pages = m_job->pageCount();
    m_page = std::clamp(m_page, 0, std::max(0, pages - 1));
    m_preview->setJob(m_job.get(), m_page);
    m_pageLabel->setText(pages ? tr("Страница %1 из %2").arg(m_page + 1).arg(pages) : tr("Нет страниц"));
    m_prev->setEnabled(m_page > 0);
    m_next->setEnabled(m_page + 1 < pages);
    m_summary->setText(tr("Этикеток: <b>%1</b>, страниц: <b>%2</b>").arg(m_job->labelCount()).arg(pages));
    m_run->setEnabled(m_job->labelCount() > 0);
}

void PrintDialog::run()
{
    updateJob();
    bool ok = false;
    switch (Output(m_output->currentData().toInt())) {
    case OutPrinter: ok = printToPrinter(); break;
    case OutPdf: ok = exportPdf(); break;
    case OutThermal: ok = sendThermal(false); break;
    case OutThermalFile: ok = sendThermal(true); break;
    case OutPng: ok = exportPng(); break;
    }
    if (ok)
        accept();
}

bool PrintDialog::printToPrinter()
{
    QPrinter printer(QPrinter::HighResolution);
    if (!m_settings.printerName.isEmpty())
        printer.setPrinterName(m_settings.printerName);
    printer.setDocName(m_doc.title.isEmpty() ? QStringLiteral("LabelForge") : m_doc.title);
    QPrintDialog dlg(&printer, this);
    dlg.setOption(QAbstractPrintDialog::PrintToFile, false);
    dlg.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    m_settings.printerName = printer.printerName();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = m_job->printTo(&printer, &err);
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, windowTitle(), err);
    return ok;
}

void PrintDialog::showSystemPreview()
{
    updateJob();
    QPrinter printer(QPrinter::HighResolution);
    if (!m_settings.printerName.isEmpty())
        printer.setPrinterName(m_settings.printerName);
    QPrintPreviewDialog dlg(&printer, this);
    dlg.resize(1000, 750);
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [this](QPrinter* p) {
        QString err;
        m_job->printTo(p, &err);
    });
    dlg.exec();
}

bool PrintDialog::exportPdf()
{
    QSettings st;
    const QString dir = st.value(QStringLiteral("print/pdfDir"), m_baseDir).toString();
    const QString base = m_doc.title.isEmpty() ? tr("этикетки") : m_doc.title;
    const QString path = QFileDialog::getSaveFileName(this, tr("Сохранить PDF"), QDir(dir).filePath(base + QStringLiteral(".pdf")),
                                                      tr("PDF (*.pdf)"));
    if (path.isEmpty())
        return false;
    st.setValue(QStringLiteral("print/pdfDir"), QFileInfo(path).absolutePath());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = m_job->exportPdf(path, &err);
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, windowTitle(), err);
    return ok;
}

bool PrintDialog::sendThermal(bool toFile)
{
    const int total = m_job->labelCount();
    QProgressDialog progress(tr("Подготовка этикеток…"), tr("Отмена"), 0, total, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    const QByteArray data = buildThermalJob(*m_job, m_doc, m_settings, [&](int i, int) {
        progress.setValue(i);
        QApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.setValue(total);
    if (data.isEmpty())
        return false;
    if (toFile) {
        const bool zpl = m_settings.thermalLang == QLatin1String("zpl");
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Сохранить команды принтера"),
            QDir(m_baseDir).filePath(zpl ? QStringLiteral("labels.zpl") : QStringLiteral("labels.prn")),
            zpl ? tr("ZPL (*.zpl *.txt)") : tr("TSPL (*.prn *.txt)"));
        if (path.isEmpty())
            return false;
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            QMessageBox::warning(this, windowTitle(), f.errorString());
            return false;
        }
        return true;
    }
    if (m_thermalPrinter->currentText().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Не выбран принтер."));
        return false;
    }
    QString err;
    if (!sendRawToPrinter(m_thermalPrinter->currentText(), data,
                          m_doc.title.isEmpty() ? QStringLiteral("LabelForge") : m_doc.title, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return false;
    }
    return true;
}

bool PrintDialog::exportPng()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка для изображений"), m_baseDir);
    if (dir.isEmpty())
        return false;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = m_job->exportPng(QDir(dir).filePath(QStringLiteral("label_%1.png")), 300, &err);
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, windowTitle(), err);
    return ok;
}

} // namespace lf
