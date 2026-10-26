// SPDX-License-Identifier: GPL-3.0-or-later
#include "NewLabelDialog.h"

#include "core/LabelDocument.h"
#include "core/Renderer.h"
#include "data/DataLoader.h"
#include "print/SheetLayout.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace lf {

NewLabelDialog::NewLabelDialog(Mode mode, QWidget* parent) : QDialog(parent), m_mode(mode)
{
    setWindowTitle(mode == CreateMode ? tr("Новая этикетка") : tr("Шаблоны этикеток"));
    resize(900, 620);
    auto* lay = new QVBoxLayout(this);
    m_tabs = new QTabWidget;
    lay->addWidget(m_tabs, 1);

    // ------------------------------------------------ templates tab
    auto* tw = new QWidget;
    auto* tl = new QVBoxLayout(tw);
    auto* top = new QHBoxLayout;
    m_category = new QComboBox;
    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("Поиск…"));
    m_search->setClearButtonEnabled(true);
    top->addWidget(new QLabel(tr("Категория:")));
    top->addWidget(m_category);
    top->addWidget(m_search, 1);
    tl->addLayout(top);
    m_list = new QListWidget;
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(180, 130));
    m_list->setGridSize(QSize(206, 178));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    tl->addWidget(m_list, 1);
    m_details = new QLabel;
    m_details->setWordWrap(true);
    m_details->setMinimumHeight(40);
    tl->addWidget(m_details);
    auto* tb = new QHBoxLayout;
    auto* imp = new QPushButton(tr("Импорт…"));
    m_export = new QPushButton(tr("Экспорт…"));
    m_delete = new QPushButton(tr("Удалить"));
    tb->addWidget(imp);
    tb->addWidget(m_export);
    tb->addWidget(m_delete);
    tb->addStretch();
    tl->addLayout(tb);
    m_tabs->addTab(tw, tr("Из шаблона"));

    // ------------------------------------------------ blank tab
    auto* bw = new QWidget;
    auto* bf = new QFormLayout(bw);
    m_layout = new QComboBox;
    m_layout->addItem(tr("Свой размер (рулон / термопринтер)"), QStringLiteral("roll"));
    for (const SheetLayout& l : SheetLayoutLibrary::all())
        if (l.id != QLatin1String("roll"))
            m_layout->addItem(QStringLiteral("%1 — %2").arg(l.category, l.name), l.id);
    bf->addRow(tr("Лист / рулон:"), m_layout);
    m_w = new QDoubleSpinBox;
    m_h = new QDoubleSpinBox;
    m_r = new QDoubleSpinBox;
    for (QDoubleSpinBox* s : {m_w, m_h, m_r}) {
        s->setRange(0, 1000);
        s->setDecimals(2);
        s->setSuffix(tr(" мм"));
    }
    m_w->setMinimum(1);
    m_h->setMinimum(1);
    m_w->setValue(58);
    m_h->setValue(40);
    bf->addRow(tr("Ширина:"), m_w);
    bf->addRow(tr("Высота:"), m_h);
    bf->addRow(tr("Скругление углов:"), m_r);
    if (mode == CreateMode)
        m_tabs->addTab(bw, tr("Пустая этикетка"));
    else
        bw->deleteLater();

    auto* bb = new QDialogButtonBox(mode == CreateMode ? QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                                       : QDialogButtonBox::Close);
    if (mode == CreateMode)
        bb->button(QDialogButtonBox::Ok)->setText(tr("Создать"));
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);

    connect(m_category, &QComboBox::currentIndexChanged, this, &NewLabelDialog::filter);
    connect(m_search, &QLineEdit::textChanged, this, &NewLabelDialog::filter);
    connect(m_list, &QListWidget::currentItemChanged, this, &NewLabelDialog::updateDetails);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] {
        if (m_mode == CreateMode)
            accept();
    });
    connect(m_layout, &QComboBox::currentIndexChanged, this, &NewLabelDialog::applyLayoutToSize);
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const TemplateInfo* t = currentTemplate();
        if (!t || t->builtin)
            return;
        if (QMessageBox::question(this, windowTitle(), tr("Удалить шаблон «%1»?").arg(t->title)) == QMessageBox::Yes) {
            TemplateLibrary::remove(*t);
            reloadTemplates();
        }
    });
    connect(imp, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Импорт шаблонов"), QString(),
                                                                tr("Этикетки LabelForge (*.lfl)"));
        for (const QString& f : files) {
            QString err;
            if (!TemplateLibrary::importFile(f, &err))
                QMessageBox::warning(this, windowTitle(), QStringLiteral("%1\n%2").arg(f, err));
        }
        reloadTemplates();
    });
    connect(m_export, &QPushButton::clicked, this, [this] {
        const TemplateInfo* t = currentTemplate();
        if (!t)
            return;
        LabelDocument doc;
        QString err;
        if (!TemplateLibrary::load(*t, doc, &err))
            return;
        const QString path = QFileDialog::getSaveFileName(this, tr("Экспорт шаблона"), t->title + QStringLiteral(".lfl"),
                                                          tr("Этикетки LabelForge (*.lfl)"));
        if (!path.isEmpty() && !doc.save(path, &err))
            QMessageBox::warning(this, windowTitle(), err);
    });

    reloadTemplates();
}

QIcon NewLabelDialog::thumbnail(const TemplateInfo& t)
{
    LabelDocument doc;
    QString err;
    const QSize box(180, 130);
    QPixmap pm(box);
    pm.fill(Qt::transparent);
    if (!TemplateLibrary::load(t, doc, &err))
        return QIcon(pm);
    DataTable data;
    loadData(doc.data, QString(), &data, &err);
    RenderContext ctx;
    if (!data.isEmpty()) {
        ctx.columns = &data.columns;
        ctx.values = &data.rows.first();
    }
    const double s = std::min((box.width() - 8) / doc.label.width, (box.height() - 8) / doc.label.height);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r((box.width() - doc.label.width * s) / 2, (box.height() - doc.label.height * s) / 2,
                   doc.label.width * s, doc.label.height * s);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(r.translated(2, 2), doc.label.radius * s, doc.label.radius * s);
    p.setBrush(Qt::white);
    p.drawRoundedRect(r, doc.label.radius * s, doc.label.radius * s);
    p.translate(r.topLeft());
    p.scale(s, s);
    renderLabel(&p, doc, ctx);
    p.resetTransform();
    p.setPen(QColor(150, 150, 150));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, doc.label.radius * s, doc.label.radius * s);
    p.end();
    return QIcon(pm);
}

void NewLabelDialog::reloadTemplates()
{
    m_templates = TemplateLibrary::all();
    const QString keep = m_category->currentText();
    {
        const QSignalBlocker b(m_category);
        m_category->clear();
        m_category->addItem(tr("Все"));
        m_category->addItems(TemplateLibrary::categories());
        m_category->setCurrentText(keep);
    }
    m_list->clear();
    for (int i = 0; i < m_templates.size(); ++i) {
        const TemplateInfo& t = m_templates.at(i);
        auto* it = new QListWidgetItem(thumbnail(t), t.title);
        it->setData(Qt::UserRole, i);
        it->setToolTip(t.description);
        m_list->addItem(it);
    }
    filter();
    if (m_list->count())
        m_list->setCurrentRow(0);
}

void NewLabelDialog::filter()
{
    const QString cat = m_category->currentIndex() > 0 ? m_category->currentText() : QString();
    const QString q = m_search->text().trimmed();
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        const TemplateInfo& t = m_templates.at(it->data(Qt::UserRole).toInt());
        const bool show = (cat.isEmpty() || t.category == cat) &&
                          (q.isEmpty() || t.title.contains(q, Qt::CaseInsensitive) ||
                           t.description.contains(q, Qt::CaseInsensitive));
        it->setHidden(!show);
    }
}

const TemplateInfo* NewLabelDialog::currentTemplate() const
{
    QListWidgetItem* it = m_list->currentItem();
    if (!it)
        return nullptr;
    return &m_templates.at(it->data(Qt::UserRole).toInt());
}

void NewLabelDialog::updateDetails()
{
    const TemplateInfo* t = currentTemplate();
    if (!t) {
        m_details->clear();
        return;
    }
    m_details->setText(QStringLiteral("<b>%1</b> — %2<br>%3")
                           .arg(t->title.toHtmlEscaped(),
                                t->builtin ? tr("встроенный") : tr("пользовательский"),
                                t->description.toHtmlEscaped()));
    m_delete->setEnabled(!t->builtin);
}

void NewLabelDialog::applyLayoutToSize()
{
    const QString id = m_layout->currentData().toString();
    if (id == QLatin1String("roll"))
        return;
    const SheetLayout l = SheetLayoutLibrary::find(id, m_w->value(), m_h->value());
    m_w->setValue(l.labelW);
    m_h->setValue(l.labelH);
    m_r->setValue(l.radius);
}

bool NewLabelDialog::createDocument(LabelDocument& doc, QString* err) const
{
    if (m_tabs->currentIndex() == 0) {
        const TemplateInfo* t = currentTemplate();
        if (!t) {
            *err = tr("Шаблон не выбран");
            return false;
        }
        if (!TemplateLibrary::load(*t, doc, err))
            return false;
        doc.category.clear();
        doc.description.clear();
        return true;
    }
    doc.clear();
    doc.label.width = m_w->value();
    doc.label.height = m_h->value();
    doc.label.radius = m_r->value();
    doc.print.layoutId = m_layout->currentData().toString();
    return true;
}

} // namespace lf
