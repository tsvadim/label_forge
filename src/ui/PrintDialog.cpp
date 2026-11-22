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
    m_layout->setObjectName(QStringLiteral("layout"));
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
    auto* fo = m_outForm = new QFormLayout(gOut);
    m_output = new QComboBox;
    m_output->setObjectName(QStringLiteral("output"));
    m_output->addItem(tr("Принтер (через драйвер Windows)"), int(OutPrinter));
    m_output->addItem(tr("Файл PDF"), int(OutPdf));
    m_output->addItem(tr("Термопринтер напрямую (ZPL / TSPL)"), int(OutThermal));
    m_output->addItem(tr("Файл команд принтера (.zpl / .prn)"), int(OutThermalFile));
    m_output->addItem(tr("Изображения PNG"), int(OutPng));
    fo->addRow(tr("Куда:"), m_output);
    // every printer Windows knows, network ones included
    auto* prow = new QHBoxLayout;
    m_printer = new QComboBox;
    m_printer->setObjectName(QStringLiteral("printer"));
    const QString defaultName = QPrinterInfo::defaultPrinterName();
    for (const QPrinterInfo& pi : QPrinterInfo::availablePrinters()) {
        QString text = pi.printerName();
        if (pi.isRemote())
            text += tr(" (сетевой)");
        if (pi.printerName() == defaultName)
            text += tr(" — по умолчанию");
        m_printer->addItem(text, pi.printerName());
        m_printer->setItemData(m_printer->count() - 1, pi.description(), Qt::ToolTipRole);
    }
    m_printerSetup = new QToolButton;
    m_printerSetup->setObjectName(QStringLiteral("printerSetup"));
    m_printerSetup->setText(tr("Настройки…"));
    m_printerSetup->setToolTip(tr("Настройки драйвера: лоток, бумага, качество"));
    prow->addWidget(m_printer, 1);
    prow->addWidget(m_printerSetup);
    fo->addRow(tr("Принтер:"), prow);
    m_printerWarn = new QLabel;
    m_printerWarn->setObjectName(QStringLiteral("printerWarn"));
    m_printerWarn->setWordWrap(true);
    m_printerWarn->setStyleSheet(QStringLiteral("color: #c0392b;"));
    fo->addRow(m_printerWarn);
    left->addWidget(gOut);

    m_thermalBox = new QGroupBox(tr("Термопринтер"));
    auto* ft = new QFormLayout(m_thermalBox);
    m_lang = new QComboBox;
    m_lang->addItem(tr("ZPL II (Zebra и совместимые)"), QStringLiteral("zpl"));
    m_lang->addItem(tr("TSPL (TSC, Xprinter, Gprinter…)"), QStringLiteral("tspl"));
    m_lang->setCurrentIndex(std::max(0, m_lang->findData(m_settings.thermalLang)));
    ft->addRow(tr("Язык:"), m_lang);
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
    selectPrinterForLayout();
    QSettings st;
    m_output->setCurrentIndex(std::max(0, m_output->findData(st.value(QStringLiteral("print/output"), 0).toInt())));

    connect(manage, &QToolButton::clicked, this, [this] {
        SheetLayoutDialog dlg(m_layout->currentData().toString(), this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedId().isEmpty())
            m_settings.layoutId = dlg.selectedId();
        fillLayouts();
        selectPrinterForLayout();
        updateJob();
    });
    connect(m_layout, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.layoutId = m_layout->currentData().toString();
        selectPrinterForLayout();
        updateJob();
    });
    connect(m_printer, &QComboBox::currentIndexChanged, this, &PrintDialog::onPrinterChanged);
    connect(m_printerSetup, &QToolButton::clicked, this, &PrintDialog::printerSetup);
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
    {
        // the label's own multi-up setup (Properties -> Layout)
        const SheetLayout own = m_settings.layoutSetup.resolve(m_doc.label.width, m_doc.label.height);
        m_layout->addItem(tr("Разметка этикетки: %1").arg(own.summary()), QStringLiteral("custom"));
    }
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
    // the printer is remembered per layout in the program settings, not in the file
    s.printerName = s.thermalPrinter = currentPrinter();
    return s;
}

QString PrintDialog::currentPrinter() const { return m_printer->currentData().toString(); }

// Settings key of the printer remembered for the chosen layout: a roll goes to
// the thermal printer, an A4 sheet to the office one.
QString PrintDialog::printerKey() const
{
    QString id = m_layout->currentData().toString();
    if (id == QLatin1String("custom"))
        id += QStringLiteral("-%1").arg(m_settings.layoutSetup.carrier);
    return QStringLiteral("print/printerFor/") + id;
}

void PrintDialog::selectPrinterForLayout()
{
    const bool roll = m_layout->currentData().toString() == QLatin1String("roll");
    // remembered for this layout -> from an older file -> the Windows default
    const QStringList candidates{QSettings().value(printerKey()).toString(),
                                 roll ? m_doc.print.thermalPrinter : m_doc.print.printerName,
                                 roll ? m_doc.print.printerName : m_doc.print.thermalPrinter,
                                 QPrinterInfo::defaultPrinterName()};
    for (const QString& name : candidates) {
        const int i = name.isEmpty() ? -1 : m_printer->findData(name);
        if (i >= 0) {
            const QSignalBlocker b(m_printer);
            m_printer->setCurrentIndex(i);
            break;
        }
    }
    onPrinterChanged();
}

void PrintDialog::onPrinterChanged()
{
    const QString name = currentPrinter();
    if (!name.isEmpty())
        QSettings().setValue(printerKey(), name);
    // keep the driver settings made for this printer
    if (m_qprinter && m_qprinter->printerName() != name)
        m_qprinter.reset();
    updatePrinterWarning();
}

void PrintDialog::updatePrinterWarning()
{
    const auto out = Output(m_output->currentData().toInt());
    QString text;
    if (out == OutPrinter && m_job && !currentPrinter().isEmpty()) {
        const SheetLayout& l = m_job->layout();
        if (!PrintJob::pageFits(QPrinterInfo::printerInfo(currentPrinter()), l))
            text = tr("Принтер не поддерживает формат %1×%2 мм. Выберите лист (например, A4) "
                      "или другой принтер — иначе этикетки будут напечатаны на его обычной бумаге.")
                       .arg(l.pageW)
                       .arg(l.pageH);
    }
    m_printerWarn->setText(text);
    m_outForm->setRowVisible(m_printerWarn, !text.isEmpty());
}

QPrinter* PrintDialog::printerObject()
{
    if (!m_qprinter) {
        m_qprinter = std::make_unique<QPrinter>(QPrinter::HighResolution);
        if (!currentPrinter().isEmpty())
            m_qprinter->setPrinterName(currentPrinter());
    }
    m_qprinter->setDocName(m_doc.title.isEmpty() ? QStringLiteral("LabelForge") : m_doc.title);
    return m_qprinter.get();
}

// The Windows print dialog, used only for driver settings (its "Properties"
// button); picking another printer there updates our list.
void PrintDialog::printerSetup()
{
    QPrinter* printer = printerObject();
    QPrintDialog dlg(printer, this);
    dlg.setWindowTitle(tr("Настройки принтера"));
    dlg.setOption(QAbstractPrintDialog::PrintToFile, false);
    dlg.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int i = m_printer->findData(printer->printerName());
    if (i >= 0)
        m_printer->setCurrentIndex(i);
}

void PrintDialog::updateOutputUi()
{
    const auto out = Output(m_output->currentData().toInt());
    m_thermalBox->setVisible(out == OutThermal || out == OutThermalFile);
    m_outForm->setRowVisible(1, out == OutPrinter || out == OutThermal);  // the "Принтер:" row
    m_printerSetup->setVisible(out == OutPrinter);
    updatePrinterWarning();
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
    updatePrinterWarning();
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
    if (currentPrinter().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Не выбран принтер."));
        return false;
    }
    QPrinter* printer = printerObject();
    // a page size the printer does not know: print on its own paper, or force it
    bool setPage = true;
    if (!PrintJob::pageFits(QPrinterInfo::printerInfo(currentPrinter()), m_job->layout())) {
        QMessageBox box(QMessageBox::Warning, windowTitle(), m_printerWarn->text(), QMessageBox::NoButton, this);
        QPushButton* own = box.addButton(tr("На бумаге принтера"), QMessageBox::AcceptRole);
        QPushButton* force = box.addButton(tr("Задать размер этикетки"), QMessageBox::ActionRole);
        box.addButton(tr("Отмена"), QMessageBox::RejectRole);
        box.setDefaultButton(own);
        box.exec();
        if (box.clickedButton() != own && box.clickedButton() != force)
            return false;
        setPage = box.clickedButton() == force;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = m_job->printTo(printer, &err, setPage);
    QApplication::restoreOverrideCursor();
    if (!ok)
        QMessageBox::warning(this, windowTitle(), err);
    return ok;
}

void PrintDialog::showSystemPreview()
{
    updateJob();
    QPrintPreviewDialog dlg(printerObject(), this);
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
    if (currentPrinter().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Не выбран принтер."));
        return false;
    }
    QString err;
    if (!sendRawToPrinter(currentPrinter(), data,
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
