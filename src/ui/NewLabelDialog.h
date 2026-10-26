// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "templates/TemplateLibrary.h"

#include <QDialog>

class QListWidget;
class QListWidgetItem;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTabWidget;
class QPushButton;

namespace lf {

class LabelDocument;

// "New label" dialog: start from a template (built-in or own) or from an empty
// label sized by a sheet/roll layout. Also used to manage templates.
class NewLabelDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { CreateMode, ManageMode };
    explicit NewLabelDialog(Mode mode, QWidget* parent = nullptr);

    // fills doc with the chosen template or blank label
    bool createDocument(LabelDocument& doc, QString* err) const;

private:
    void reloadTemplates();
    void filter();
    void updateDetails();
    void applyLayoutToSize();
    QIcon thumbnail(const TemplateInfo& t);
    const TemplateInfo* currentTemplate() const;

    Mode m_mode;
    QTabWidget* m_tabs;
    QComboBox* m_category;
    QLineEdit* m_search;
    QListWidget* m_list;
    QLabel* m_details;
    QPushButton* m_delete;
    QPushButton* m_export;
    QComboBox* m_layout;
    QDoubleSpinBox* m_w;
    QDoubleSpinBox* m_h;
    QDoubleSpinBox* m_r;
    QVector<TemplateInfo> m_templates;
};

} // namespace lf
