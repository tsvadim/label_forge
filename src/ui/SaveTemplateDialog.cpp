// SPDX-License-Identifier: GPL-3.0-or-later
#include "SaveTemplateDialog.h"

#include "core/LabelDocument.h"
#include "templates/TemplateLibrary.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

namespace lf {

SaveTemplateDialog::SaveTemplateDialog(const LabelDocument& doc, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Сохранить как шаблон"));
    resize(460, 300);
    auto* f = new QFormLayout(this);
    m_title = new QLineEdit(doc.title);
    m_title->setPlaceholderText(tr("Например: Ценник 58×40 акция"));
    f->addRow(tr("Название:"), m_title);
    m_category = new QComboBox;
    m_category->setEditable(true);
    m_category->addItems(TemplateLibrary::categories());
    m_category->setCurrentText(doc.category.isEmpty() ? tr("Мои шаблоны") : doc.category);
    f->addRow(tr("Категория:"), m_category);
    m_description = new QPlainTextEdit(doc.description);
    m_description->setMaximumHeight(90);
    f->addRow(tr("Описание:"), m_description);
    m_data = new QCheckBox(tr("Сохранить вместе с источником данных"));
    m_data->setChecked(doc.data.kind == DataKind::Inline);
    m_data->setEnabled(doc.data.kind != DataKind::None);
    f->addRow(m_data);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, this, [this] {
        if (!m_title->text().trimmed().isEmpty())
            accept();
        else
            m_title->setFocus();
    });
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    f->addRow(bb);
}

QString SaveTemplateDialog::title() const { return m_title->text().trimmed(); }
QString SaveTemplateDialog::category() const { return m_category->currentText().trimmed(); }
QString SaveTemplateDialog::description() const { return m_description->toPlainText().trimmed(); }
bool SaveTemplateDialog::includeData() const { return m_data->isChecked(); }

} // namespace lf
