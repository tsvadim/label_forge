// SPDX-License-Identifier: GPL-3.0-or-later
#include "DataSourceDialog.h"

#include "data/DataLoader.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lf {

DataSourceDialog::DataSourceDialog(const DataSourceConfig& cfg, const DataTable& current, const QString& baseDir,
                                   QWidget* parent)
    : QDialog(parent), m_cfg(cfg), m_current(current), m_baseDir(baseDir)
{
    setWindowTitle(tr("Источник данных"));
    resize(760, 560);
    auto* lay = new QVBoxLayout(this);

    auto* kindRow = new QFormLayout;
    m_kind = new QComboBox;
    m_kind->addItem(tr("Нет (фиксированные этикетки)"), int(DataKind::None));
    m_kind->addItem(tr("Встроенная таблица (хранится в файле этикетки)"), int(DataKind::Inline));
    m_kind->addItem(tr("Файл CSV / TXT"), int(DataKind::Csv));
    m_kind->addItem(tr("Книга Excel (.xlsx)"), int(DataKind::Excel));
    m_kind->addItem(tr("База данных (ODBC: SQL Server, MySQL, Access, 1С…)"), int(DataKind::Odbc));
    kindRow->addRow(tr("Тип источника:"), m_kind);
    lay->addLayout(kindRow);

    m_pages = new QStackedWidget;
    // none
    auto* none = new QLabel(tr("Этикетки печатаются без данных. Переменные {seq} и {date} работают."));
    none->setWordWrap(true);
    m_pages->addWidget(none);
    // inline
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        m_inlineCols = new QLineEdit(cfg.table.columns.join(QStringLiteral("; ")));
        m_inlineCols->setPlaceholderText(tr("Наименование; Цена; Штрихкод"));
        f->addRow(tr("Поля (через «;»):"), m_inlineCols);
        m_inlineFromCurrent = new QCheckBox(tr("Скопировать текущие данные (%1 записей) во встроенную таблицу")
                                                .arg(current.rowCount()));
        m_inlineFromCurrent->setEnabled(!current.columns.isEmpty() && cfg.kind != DataKind::Inline);
        f->addRow(m_inlineFromCurrent);
        auto* hint = new QLabel(tr("Записи редактируются в панели «Данные» внизу окна."));
        hint->setWordWrap(true);
        f->addRow(hint);
        m_pages->addWidget(w);
    }
    // csv
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        auto* row = new QHBoxLayout;
        m_csvPath = new QLineEdit(cfg.kind == DataKind::Csv ? cfg.path : QString());
        auto* b = new QPushButton(tr("Обзор…"));
        connect(b, &QPushButton::clicked, this, [this] {
            browse(m_csvPath, tr("Текст с разделителями (*.csv *.txt *.tsv);;Все файлы (*)"));
        });
        row->addWidget(m_csvPath);
        row->addWidget(b);
        f->addRow(tr("Файл:"), row);
        m_csvDelim = new QComboBox;
        m_csvDelim->addItem(tr("Автоопределение"), QStringLiteral("auto"));
        m_csvDelim->addItem(tr("Точка с запятой ;"), QStringLiteral(";"));
        m_csvDelim->addItem(tr("Запятая ,"), QStringLiteral(","));
        m_csvDelim->addItem(tr("Табуляция"), QStringLiteral("tab"));
        m_csvDelim->addItem(tr("Вертикальная черта |"), QStringLiteral("|"));
        m_csvDelim->setCurrentIndex(std::max(0, m_csvDelim->findData(cfg.delimiter)));
        f->addRow(tr("Разделитель:"), m_csvDelim);
        m_csvEnc = new QComboBox;
        m_csvEnc->addItem(tr("Автоопределение (UTF-8 / Windows-1251)"), QStringLiteral("auto"));
        m_csvEnc->addItem(QStringLiteral("UTF-8"), QStringLiteral("UTF-8"));
        m_csvEnc->addItem(QStringLiteral("Windows-1251"), QStringLiteral("Windows-1251"));
        m_csvEnc->addItem(QStringLiteral("UTF-16 LE"), QStringLiteral("UTF-16LE"));
        m_csvEnc->addItem(tr("Системная"), QStringLiteral("System"));
        m_csvEnc->setCurrentIndex(std::max(0, m_csvEnc->findData(cfg.encoding)));
        f->addRow(tr("Кодировка:"), m_csvEnc);
        m_csvHeader = new QCheckBox(tr("Первая строка — имена полей"));
        m_csvHeader->setChecked(cfg.hasHeader);
        f->addRow(m_csvHeader);
        m_pages->addWidget(w);
    }
    // excel
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        auto* row = new QHBoxLayout;
        m_xlsPath = new QLineEdit(cfg.kind == DataKind::Excel ? cfg.path : QString());
        auto* b = new QPushButton(tr("Обзор…"));
        connect(b, &QPushButton::clicked, this, [this] {
            browse(m_xlsPath, tr("Книги Excel (*.xlsx *.xlsm);;Все файлы (*)"));
            reloadSheets();
        });
        connect(m_xlsPath, &QLineEdit::editingFinished, this, &DataSourceDialog::reloadSheets);
        row->addWidget(m_xlsPath);
        row->addWidget(b);
        f->addRow(tr("Файл:"), row);
        m_xlsSheet = new QComboBox;
        f->addRow(tr("Лист:"), m_xlsSheet);
        m_xlsHeader = new QCheckBox(tr("Первая строка — имена полей"));
        m_xlsHeader->setChecked(cfg.hasHeader);
        f->addRow(m_xlsHeader);
        auto* hint = new QLabel(tr("Старый формат .xls сохраните в Excel как .xlsx или подключите через ODBC."));
        hint->setWordWrap(true);
        f->addRow(hint);
        m_pages->addWidget(w);
    }
    // odbc
    {
        auto* w = new QWidget;
        auto* f = new QFormLayout(w);
        m_odbcConn = new QLineEdit(cfg.connection);
        m_odbcConn->setPlaceholderText(QStringLiteral("DSN=MyBase;UID=user;PWD=…"));
        m_odbcConn->setToolTip(tr("Примеры:\n"
                                  "DSN=Склад\n"
                                  "Driver={ODBC Driver 17 for SQL Server};Server=srv;Database=db;Trusted_Connection=yes;\n"
                                  "Driver={Microsoft Access Driver (*.mdb, *.accdb)};DBQ=C:\\data\\base.accdb;\n"
                                  "Driver={MySQL ODBC 8.0 Unicode Driver};Server=host;Database=db;User=u;Password=p;"));
        f->addRow(tr("Строка подключения:"), m_odbcConn);
        m_odbcQuery = new QPlainTextEdit(cfg.query);
        m_odbcQuery->setPlaceholderText(QStringLiteral("SELECT name, price, barcode FROM goods WHERE active = 1"));
        f->addRow(tr("SQL-запрос:"), m_odbcQuery);
        m_pages->addWidget(w);
    }
    lay->addWidget(m_pages);

    m_embed = new QCheckBox(tr("Встроить загруженные данные в файл этикетки (файл-источник больше не нужен)"));
    lay->addWidget(m_embed);

    auto* pv = new QHBoxLayout;
    auto* test = new QPushButton(tr("Проверить / предпросмотр"));
    connect(test, &QPushButton::clicked, this, &DataSourceDialog::preview);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    pv->addWidget(test);
    pv->addWidget(m_status, 1);
    lay->addLayout(pv);
    m_preview = new QTableWidget;
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->horizontalHeader()->setStretchLastSection(true);
    lay->addWidget(m_preview, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    m_kind->setCurrentIndex(std::max(0, m_kind->findData(int(cfg.kind))));
    connect(m_kind, &QComboBox::currentIndexChanged, this, &DataSourceDialog::updatePage);
    if (cfg.kind == DataKind::Excel) {
        reloadSheets();
        m_xlsSheet->setCurrentText(cfg.sheet);
    }
    updatePage();
}

void DataSourceDialog::updatePage()
{
    const auto kind = DataKind(m_kind->currentData().toInt());
    m_pages->setCurrentIndex(m_kind->currentIndex());
    m_embed->setVisible(kind == DataKind::Csv || kind == DataKind::Excel || kind == DataKind::Odbc);
}

void DataSourceDialog::browse(QLineEdit* target, const QString& filter)
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Выбор файла данных"),
                                                      target->text().isEmpty() ? m_baseDir : target->text(), filter);
    if (!path.isEmpty())
        target->setText(QDir::toNativeSeparators(path));
}

void DataSourceDialog::reloadSheets()
{
    const QString keep = m_xlsSheet->currentText();
    m_xlsSheet->clear();
    if (m_xlsPath->text().isEmpty())
        return;
    QString err;
    const QStringList names = xlsxSheetNames(QDir::fromNativeSeparators(m_xlsPath->text()), &err);
    m_xlsSheet->addItems(names);
    if (names.contains(keep))
        m_xlsSheet->setCurrentText(keep);
    if (!err.isEmpty())
        m_status->setText(err);
}

DataSourceConfig DataSourceDialog::config() const
{
    DataSourceConfig c;
    c.kind = DataKind(m_kind->currentData().toInt());
    switch (c.kind) {
    case DataKind::None: break;
    case DataKind::Inline: {
        if (m_inlineFromCurrent->isChecked()) {
            c.table = m_current;
        } else {
            QStringList cols;
            for (const QString& s : m_inlineCols->text().split(QLatin1Char(';')))
                if (!s.trimmed().isEmpty())
                    cols << s.trimmed();
            c.table = m_cfg.kind == DataKind::Inline ? m_cfg.table : DataTable();
            // keep existing values for columns that still exist
            DataTable t;
            t.columns = cols;
            for (const QStringList& r : c.table.rows) {
                QStringList nr;
                for (const QString& col : cols) {
                    const int i = c.table.columns.indexOf(col);
                    nr << (i >= 0 ? r.value(i) : QString());
                }
                t.rows << nr;
            }
            if (t.rows.isEmpty() && !cols.isEmpty()) {
                QStringList empty;
                for (int i = 0; i < cols.size(); ++i)
                    empty << QString();
                t.rows << empty;
            }
            c.table = t;
        }
        break;
    }
    case DataKind::Csv:
        c.path = QDir::fromNativeSeparators(m_csvPath->text().trimmed());
        c.delimiter = m_csvDelim->currentData().toString();
        c.encoding = m_csvEnc->currentData().toString();
        c.hasHeader = m_csvHeader->isChecked();
        break;
    case DataKind::Excel:
        c.path = QDir::fromNativeSeparators(m_xlsPath->text().trimmed());
        c.sheet = m_xlsSheet->currentText();
        c.hasHeader = m_xlsHeader->isChecked();
        break;
    case DataKind::Odbc:
        c.connection = m_odbcConn->text().trimmed();
        c.query = m_odbcQuery->toPlainText().trimmed();
        break;
    }
    if (m_embed->isVisible() && m_embed->isChecked()) {
        DataTable t;
        QString err;
        if (loadData(c, m_baseDir, &t, &err)) {
            DataSourceConfig e;
            e.kind = DataKind::Inline;
            e.table = t;
            return e;
        }
    }
    return c;
}

void DataSourceDialog::preview()
{
    const DataSourceConfig c = config();
    DataTable t;
    QString err;
    if (!loadData(c, m_baseDir, &t, &err)) {
        m_status->setText(QStringLiteral("<span style='color:#b00'>%1</span>").arg(err.toHtmlEscaped()));
        m_preview->clear();
        m_preview->setRowCount(0);
        return;
    }
    m_status->setText(tr("Полей: %1, записей: %2").arg(t.columns.size()).arg(t.rowCount()));
    m_preview->clear();
    m_preview->setColumnCount(t.columns.size());
    m_preview->setHorizontalHeaderLabels(t.columns);
    const int n = std::min(200, t.rowCount());
    m_preview->setRowCount(n);
    for (int r = 0; r < n; ++r)
        for (int col = 0; col < t.columns.size(); ++col)
            m_preview->setItem(r, col, new QTableWidgetItem(t.rows.at(r).value(col)));
}

} // namespace lf
