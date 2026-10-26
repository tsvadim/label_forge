// SPDX-License-Identifier: GPL-3.0-or-later
#include "SheetLayoutDialog.h"

#include "PagePreview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace lf {

namespace {
QDoubleSpinBox* mm(double max = 1000)
{
    auto* s = new QDoubleSpinBox;
    s->setRange(0, max);
    s->setDecimals(2);
    s->setSingleStep(0.5);
    s->setSuffix(QObject::tr(" мм"));
    return s;
}

struct Paper {
    const char* name;
    double w, h;
};
const Paper PAPERS[] = {{"A4", 210, 297}, {"A5", 148, 210}, {"A3", 297, 420}, {"Рулон 100×70", 100, 70},
                        {"Рулон 100×100", 100, 100}, {"Рулон 100×150", 100, 150}, {"Рулон 58×40", 58, 40}, {"Letter", 215.9, 279.4},
                        {"Legal", 215.9, 355.6}};
} // namespace

SheetLayoutDialog::SheetLayoutDialog(const QString& currentId, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Шаблоны листов и рулонов"));
    resize(980, 620);
    auto* lay = new QVBoxLayout(this);
    auto* split = new QSplitter;
    lay->addWidget(split, 1);

    m_list = new QTreeWidget;
    m_list->setHeaderLabels({tr("Шаблон"), tr("Этикетка")});
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_list->setMinimumWidth(360);
    m_list->setRootIsDecorated(true);
    split->addWidget(m_list);

    auto* editor = new QWidget;
    auto* el = new QVBoxLayout(editor);
    auto* form = new QFormLayout;
    m_name = new QLineEdit;
    m_category = new QLineEdit;
    form->addRow(tr("Название:"), m_name);
    form->addRow(tr("Категория:"), m_category);
    m_roll = new QCheckBox(tr("Рулон (одна этикетка на странице, термопринтер)"));
    form->addRow(m_roll);
    m_paper = new QComboBox;
    for (const Paper& p : PAPERS)
        m_paper->addItem(QStringLiteral("%1 (%2×%3)").arg(QString::fromUtf8(p.name)).arg(p.w).arg(p.h));
    m_paper->addItem(tr("Свой размер"));
    form->addRow(tr("Бумага:"), m_paper);
    auto* pageRow = new QHBoxLayout;
    m_pageW = mm();
    m_pageH = mm();
    pageRow->addWidget(m_pageW);
    pageRow->addWidget(new QLabel(QStringLiteral("×")));
    pageRow->addWidget(m_pageH);
    form->addRow(tr("Размер листа:"), pageRow);
    auto* lblRow = new QHBoxLayout;
    m_labelW = mm();
    m_labelH = mm();
    lblRow->addWidget(m_labelW);
    lblRow->addWidget(new QLabel(QStringLiteral("×")));
    lblRow->addWidget(m_labelH);
    form->addRow(tr("Этикетка:"), lblRow);
    auto* grid = new QHBoxLayout;
    m_cols = new QSpinBox;
    m_cols->setRange(1, 50);
    m_rows = new QSpinBox;
    m_rows->setRange(1, 100);
    grid->addWidget(new QLabel(tr("колонок")));
    grid->addWidget(m_cols);
    grid->addWidget(new QLabel(tr("рядов")));
    grid->addWidget(m_rows);
    form->addRow(tr("Сетка:"), grid);
    auto* mrow = new QHBoxLayout;
    m_left = mm();
    m_top = mm();
    mrow->addWidget(new QLabel(tr("слева")));
    mrow->addWidget(m_left);
    mrow->addWidget(new QLabel(tr("сверху")));
    mrow->addWidget(m_top);
    form->addRow(tr("Поля:"), mrow);
    auto* prow = new QHBoxLayout;
    m_pitchX = mm();
    m_pitchY = mm();
    prow->addWidget(new QLabel(tr("гориз.")));
    prow->addWidget(m_pitchX);
    prow->addWidget(new QLabel(tr("верт.")));
    prow->addWidget(m_pitchY);
    form->addRow(tr("Шаг:"), prow);
    m_radius = mm(100);
    form->addRow(tr("Скругление:"), m_radius);
    auto* fitRow = new QHBoxLayout;
    m_gap = mm(50);
    m_gap->setValue(1.5);
    m_fit = new QPushButton(tr("Разложить автоматически"));
    m_fit->setToolTip(tr("Посчитать колонки, ряды, поля и шаг так, чтобы на лист поместилось как можно больше "
                         "этикеток заданного размера с указанным зазором"));
    fitRow->addWidget(new QLabel(tr("зазор")));
    fitRow->addWidget(m_gap);
    fitRow->addWidget(m_fit, 1);
    form->addRow(tr("Авто:"), fitRow);
    connect(m_fit, &QPushButton::clicked, this, [this] {
        SheetLayout l = fromForm();
        l.roll = false;
        SheetLayout::fitGrid(l, m_gap->value(), m_gap->value());
        m_loading = true;
        m_roll->setChecked(false);
        m_cols->setValue(l.cols);
        m_rows->setValue(l.rows);
        m_left->setValue(l.marginLeft);
        m_top->setValue(l.marginTop);
        m_pitchX->setValue(l.pitchX);
        m_pitchY->setValue(l.pitchY);
        m_loading = false;
        updatePreview();
    });
    el->addLayout(form);
    auto* pitchHint = new QLabel(tr("Шаг — расстояние от левого (верхнего) края одной этикетки до края соседней, "
                                    "т. е. размер этикетки плюс промежуток."));
    pitchHint->setWordWrap(true);
    pitchHint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    el->addWidget(pitchHint);
    m_info = new QLabel;
    m_info->setWordWrap(true);
    el->addWidget(m_info);

    auto* btns = new QHBoxLayout;
    auto* nw = new QPushButton(tr("Новый"));
    auto* dup = new QPushButton(tr("Дублировать"));
    m_save = new QPushButton(tr("Сохранить"));
    m_delete = new QPushButton(tr("Удалить"));
    btns->addWidget(nw);
    btns->addWidget(dup);
    btns->addWidget(m_save);
    btns->addWidget(m_delete);
    el->addLayout(btns);
    el->addStretch();
    split->addWidget(editor);

    m_preview = new PagePreview;
    split->addWidget(m_preview);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setSizes({380, 360, 240});

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(tr("Выбрать"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(nw, &QPushButton::clicked, this, &SheetLayoutDialog::onNew);
    connect(dup, &QPushButton::clicked, this, &SheetLayoutDialog::onDuplicate);
    connect(m_save, &QPushButton::clicked, this, &SheetLayoutDialog::onSave);
    connect(m_delete, &QPushButton::clicked, this, &SheetLayoutDialog::onDelete);
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        if (!it || it->data(0, Qt::UserRole).toString().isEmpty())
            return;
        const QString id = it->data(0, Qt::UserRole).toString();
        for (const SheetLayout& l : SheetLayoutLibrary::all())
            if (l.id == id) {
                m_selectedId = id;
                showLayout(l);
            }
    });
    connect(m_paper, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (m_loading || i >= int(std::size(PAPERS)))
            return;
        m_pageW->setValue(PAPERS[i].w);
        m_pageH->setValue(PAPERS[i].h);
    });
    for (QDoubleSpinBox* s : {m_pageW, m_pageH, m_labelW, m_labelH, m_left, m_top, m_pitchX, m_pitchY, m_radius})
        connect(s, &QDoubleSpinBox::valueChanged, this, &SheetLayoutDialog::updatePreview);
    for (QSpinBox* s : {m_cols, m_rows})
        connect(s, &QSpinBox::valueChanged, this, &SheetLayoutDialog::updatePreview);
    connect(m_roll, &QCheckBox::toggled, this, &SheetLayoutDialog::updatePreview);

    reloadList(currentId.isEmpty() ? QStringLiteral("avery-l7160") : currentId);
}

void SheetLayoutDialog::reloadList(const QString& selectId)
{
    m_list->clear();
    QHash<QString, QTreeWidgetItem*> groups;
    QTreeWidgetItem* toSelect = nullptr;
    for (const SheetLayout& l : SheetLayoutLibrary::all()) {
        if (l.id == QLatin1String("roll") || l.autoFit)
            continue;
        const QString cat = l.builtin ? l.category : tr("Мои шаблоны");
        QTreeWidgetItem* g = groups.value(cat);
        if (!g) {
            g = new QTreeWidgetItem(m_list, {cat});
            g->setFlags(Qt::ItemIsEnabled);
            g->setExpanded(true);
            groups.insert(cat, g);
        }
        auto* it = new QTreeWidgetItem(g, {l.name, QStringLiteral("%1×%2").arg(l.labelW).arg(l.labelH)});
        it->setData(0, Qt::UserRole, l.id);
        if (l.id == selectId)
            toSelect = it;
    }
    if (toSelect)
        m_list->setCurrentItem(toSelect);
}

void SheetLayoutDialog::showLayout(const SheetLayout& l)
{
    m_loading = true;
    m_current = l;
    m_name->setText(l.name);
    m_category->setText(l.category);
    m_roll->setChecked(l.roll);
    int paper = int(std::size(PAPERS));
    for (int i = 0; i < int(std::size(PAPERS)); ++i)
        if (std::abs(PAPERS[i].w - l.pageW) < 0.1 && std::abs(PAPERS[i].h - l.pageH) < 0.1)
            paper = i;
    m_paper->setCurrentIndex(paper);
    m_pageW->setValue(l.pageW);
    m_pageH->setValue(l.pageH);
    m_labelW->setValue(l.labelW);
    m_labelH->setValue(l.labelH);
    m_cols->setValue(l.cols);
    m_rows->setValue(l.rows);
    m_left->setValue(l.marginLeft);
    m_top->setValue(l.marginTop);
    m_pitchX->setValue(l.pitchX > 0 ? l.pitchX : l.labelW);
    m_pitchY->setValue(l.pitchY > 0 ? l.pitchY : l.labelH);
    m_radius->setValue(l.radius);
    setEditable(!l.builtin);
    m_loading = false;
    updatePreview();
}

void SheetLayoutDialog::setEditable(bool on)
{
    for (QWidget* w : std::initializer_list<QWidget*>{m_name, m_category, m_roll, m_paper, m_pageW, m_pageH,
                                                      m_labelW, m_labelH, m_cols, m_rows, m_left, m_top, m_pitchX,
                                                      m_pitchY, m_radius, m_gap, m_fit})
        w->setEnabled(on);
    m_save->setEnabled(on);
    m_delete->setEnabled(on);
    m_info->setText(on ? QString() : tr("Встроенный шаблон нельзя изменить — нажмите «Дублировать»."));
}

SheetLayout SheetLayoutDialog::fromForm() const
{
    SheetLayout l = m_current;
    l.name = m_name->text().trimmed();
    l.category = m_category->text().trimmed();
    l.roll = m_roll->isChecked();
    l.labelW = m_labelW->value();
    l.labelH = m_labelH->value();
    if (l.roll) {
        l.pageW = l.labelW;
        l.pageH = l.labelH;
        l.cols = l.rows = 1;
        l.marginLeft = l.marginTop = 0;
        l.pitchX = l.labelW;
        l.pitchY = l.labelH;
    } else {
        l.pageW = m_pageW->value();
        l.pageH = m_pageH->value();
        l.cols = m_cols->value();
        l.rows = m_rows->value();
        l.marginLeft = m_left->value();
        l.marginTop = m_top->value();
        l.pitchX = m_pitchX->value();
        l.pitchY = m_pitchY->value();
    }
    l.radius = m_radius->value();
    return l;
}

void SheetLayoutDialog::updatePreview()
{
    if (m_loading)
        return;
    const SheetLayout l = fromForm();
    m_preview->setSheet(l);
    QString warn;
    if (!l.roll) {
        const QRectF last = l.labelRect(l.perPage() - 1);
        if (last.right() > l.pageW + 0.01 || last.bottom() > l.pageH + 0.01)
            warn = tr("<br><span style='color:#b00'>Этикетки выходят за пределы листа.</span>");
    }
    m_info->setText((m_current.builtin ? tr("Встроенный шаблон нельзя изменить — нажмите «Дублировать».<br>") : QString()) +
                    l.summary() + warn);
}

void SheetLayoutDialog::onNew()
{
    SheetLayout l;
    l.name = tr("Новый лист");
    l.category = tr("Мои шаблоны");
    l.cols = 3;
    l.rows = 8;
    l.labelW = 70;
    l.labelH = 37;
    l.pitchX = 70;
    l.pitchY = 37;
    l.marginTop = 0.5;
    showLayout(l);
}

void SheetLayoutDialog::onDuplicate()
{
    SheetLayout l = fromForm();
    l.id.clear();
    l.builtin = false;
    l.name = tr("%1 (копия)").arg(l.name);
    l.category = tr("Мои шаблоны");
    showLayout(l);
}

void SheetLayoutDialog::onSave()
{
    SheetLayout l = fromForm();
    if (l.name.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("Введите название шаблона."));
        return;
    }
    QString err;
    if (!SheetLayoutLibrary::saveUser(l, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    m_selectedId = l.id;
    reloadList(l.id);
}

void SheetLayoutDialog::onDelete()
{
    if (m_current.builtin || m_current.id.isEmpty())
        return;
    if (QMessageBox::question(this, windowTitle(), tr("Удалить шаблон «%1»?").arg(m_current.name)) !=
        QMessageBox::Yes)
        return;
    SheetLayoutLibrary::removeUser(m_current.id);
    m_selectedId.clear();
    reloadList(QString());
}

} // namespace lf
