// SPDX-License-Identifier: GPL-3.0-or-later
#include "DataPanel.h"

#include "Icons.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace lf {

DataPanel::DataPanel(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 6);

    auto* top = new QHBoxLayout;
    m_source = new QLabel;
    m_source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* edit = new QPushButton(icons::get(QStringLiteral("data")), tr("Источник данных…"));
    connect(edit, &QPushButton::clicked, this, &DataPanel::editSourceRequested);
    m_reload = new QToolButton;
    m_reload->setText(tr("Обновить"));
    m_reload->setToolTip(tr("Перечитать файл или запрос"));
    connect(m_reload, &QToolButton::clicked, this, &DataPanel::reloadRequested);
    m_showData = new QCheckBox(tr("Показывать данные на этикетке"));
    m_showData->setChecked(true);
    connect(m_showData, &QCheckBox::toggled, this, &DataPanel::showDataToggled);
    top->addWidget(edit);
    top->addWidget(m_reload);
    top->addWidget(m_source, 1);
    top->addWidget(m_showData);
    lay->addLayout(top);

    m_view = new QTableWidget;
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setDefaultSectionSize(22);
    m_view->setAlternatingRowColors(true);
    lay->addWidget(m_view, 1);

    connect(m_view, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (m_loading || row < 0)
            return;
        m_current = row;
        m_nav->setText(tr("Запись %1 из %2").arg(row + 1).arg(m_table.rowCount()));
        emit currentRecordChanged(row);
    });
    connect(m_view, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        if (!m_loading && m_cfg.kind == DataKind::Inline)
            emitInlineEdit();
    });
    connect(m_view->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int col) {
        if (m_cfg.kind != DataKind::Inline)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Имя колонки"), tr("Имя поля:"), QLineEdit::Normal,
                                                   m_table.columns.value(col), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        m_table.columns[col] = name.trimmed();
        m_view->setHorizontalHeaderItem(col, new QTableWidgetItem(name.trimmed()));
        emitInlineEdit();
    });

    auto* bottom = new QHBoxLayout;
    auto mk = [&](const QString& text, const QString& tip) {
        auto* b = new QToolButton;
        b->setText(text);
        b->setToolTip(tip);
        bottom->addWidget(b);
        return b;
    };
    m_addRow = mk(tr("+ Строка"), tr("Добавить запись"));
    m_delRow = mk(tr("− Строка"), tr("Удалить текущую запись"));
    m_addCol = mk(tr("+ Колонка"), tr("Добавить поле"));
    m_delCol = mk(tr("− Колонка"), tr("Удалить текущее поле"));
    bottom->addStretch();
    auto* prev = mk(QStringLiteral("◀"), tr("Предыдущая запись"));
    m_nav = new QLabel;
    bottom->addWidget(m_nav);
    auto* next = mk(QStringLiteral("▶"), tr("Следующая запись"));
    lay->addLayout(bottom);

    connect(prev, &QToolButton::clicked, this, [this] { setCurrentRecord(m_current - 1); });
    connect(next, &QToolButton::clicked, this, [this] { setCurrentRecord(m_current + 1); });
    connect(m_addRow, &QToolButton::clicked, this, [this] {
        m_table = tableFromWidget();
        QStringList row;
        for (int i = 0; i < m_table.columns.size(); ++i)
            row << QString();
        m_table.rows << row;
        rebuildTable();
        setCurrentRecord(m_table.rowCount() - 1);
        emitInlineEdit();
    });
    connect(m_delRow, &QToolButton::clicked, this, [this] {
        m_table = tableFromWidget();
        const int r = m_view->currentRow();
        if (r < 0 || r >= m_table.rowCount())
            return;
        m_table.rows.removeAt(r);
        rebuildTable();
        emitInlineEdit();
    });
    connect(m_addCol, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Новое поле"), tr("Имя поля:"), QLineEdit::Normal,
                                                   tr("Поле%1").arg(m_table.columns.size() + 1), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        m_table = tableFromWidget();
        m_table.columns << name.trimmed();
        for (QStringList& r : m_table.rows)
            r << QString();
        if (m_table.rows.isEmpty())
            m_table.rows << QStringList(QString());
        rebuildTable();
        emitInlineEdit();
    });
    connect(m_delCol, &QToolButton::clicked, this, [this] {
        const int c = m_view->currentColumn();
        m_table = tableFromWidget();
        if (c < 0 || c >= m_table.columns.size())
            return;
        m_table.columns.removeAt(c);
        for (QStringList& r : m_table.rows)
            if (c < r.size())
                r.removeAt(c);
        rebuildTable();
        emitInlineEdit();
    });
}

bool DataPanel::showData() const { return m_showData->isChecked(); }

void DataPanel::setData(const DataSourceConfig& cfg, const DataTable& table)
{
    m_cfg = cfg;
    m_table = table;
    m_source->setText(cfg.describe() + (table.columns.isEmpty() ? QString()
                                                                : tr(" — записей: %1").arg(table.rowCount())));
    const bool inl = cfg.kind == DataKind::Inline;
    for (QToolButton* b : {m_addRow, m_delRow, m_addCol, m_delCol})
        b->setVisible(inl);
    m_reload->setVisible(cfg.kind == DataKind::Csv || cfg.kind == DataKind::Excel || cfg.kind == DataKind::Odbc);
    m_view->setEditTriggers(inl ? QAbstractItemView::AllEditTriggers : QAbstractItemView::NoEditTriggers);
    if (m_current >= table.rowCount())
        m_current = std::max(0, table.rowCount() - 1);
    rebuildTable();
}

void DataPanel::rebuildTable()
{
    m_loading = true;
    m_view->clear();
    m_view->setColumnCount(m_table.columns.size());
    m_view->setRowCount(m_table.rowCount());
    m_view->setHorizontalHeaderLabels(m_table.columns);
    const int maxRows = std::min(m_table.rowCount(), 100000);
    for (int r = 0; r < maxRows; ++r) {
        const QStringList& row = m_table.rows.at(r);
        for (int c = 0; c < m_table.columns.size(); ++c)
            m_view->setItem(r, c, new QTableWidgetItem(row.value(c)));
    }
    m_view->resizeColumnsToContents();
    for (int c = 0; c < m_view->columnCount(); ++c)
        m_view->setColumnWidth(c, std::min(m_view->columnWidth(c) + 12, 320));
    if (m_table.rowCount() > 0)
        m_view->setCurrentCell(std::clamp(m_current, 0, m_table.rowCount() - 1), 0);
    m_nav->setText(m_table.rowCount() ? tr("Запись %1 из %2").arg(m_current + 1).arg(m_table.rowCount())
                                      : tr("Нет записей"));
    m_loading = false;
}

void DataPanel::setCurrentRecord(int row)
{
    if (m_table.rowCount() == 0)
        return;
    row = std::clamp(row, 0, m_table.rowCount() - 1);
    m_view->setCurrentCell(row, std::max(0, m_view->currentColumn()));
}

DataTable DataPanel::tableFromWidget() const
{
    DataTable t;
    t.columns = m_table.columns;
    for (int r = 0; r < m_view->rowCount(); ++r) {
        QStringList row;
        for (int c = 0; c < m_view->columnCount(); ++c) {
            const QTableWidgetItem* it = m_view->item(r, c);
            row << (it ? it->text() : QString());
        }
        t.rows << row;
    }
    return t;
}

void DataPanel::emitInlineEdit()
{
    m_table = tableFromWidget();
    emit inlineTableEdited(m_table);
}

} // namespace lf
