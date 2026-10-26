// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QLineEdit;
class QComboBox;
class QPlainTextEdit;
class QCheckBox;

namespace lf {

class LabelDocument;

class SaveTemplateDialog : public QDialog {
    Q_OBJECT
public:
    SaveTemplateDialog(const LabelDocument& doc, QWidget* parent = nullptr);
    QString title() const;
    QString category() const;
    QString description() const;
    bool includeData() const;

private:
    QLineEdit* m_title;
    QComboBox* m_category;
    QPlainTextEdit* m_description;
    QCheckBox* m_data;
};

} // namespace lf
