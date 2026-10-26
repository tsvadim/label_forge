// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "print/SheetLayout.h"

#include <QDialog>

class QTreeWidget;
class QLineEdit;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QComboBox;
class QPushButton;
class QLabel;

namespace lf {

class PagePreview;

// Library of label sheets: browse built-in sheets, create and edit your own.
class SheetLayoutDialog : public QDialog {
    Q_OBJECT
public:
    explicit SheetLayoutDialog(const QString& currentId, QWidget* parent = nullptr);
    QString selectedId() const { return m_selectedId; }

private:
    void reloadList(const QString& selectId);
    void showLayout(const SheetLayout& l);
    SheetLayout fromForm() const;
    void setEditable(bool on);
    void updatePreview();
    void onNew();
    void onDuplicate();
    void onSave();
    void onDelete();

    QTreeWidget* m_list;
    QLineEdit* m_name;
    QLineEdit* m_category;
    QComboBox* m_paper;
    QDoubleSpinBox* m_pageW;
    QDoubleSpinBox* m_pageH;
    QCheckBox* m_roll;
    QSpinBox* m_cols;
    QSpinBox* m_rows;
    QDoubleSpinBox* m_labelW;
    QDoubleSpinBox* m_labelH;
    QDoubleSpinBox* m_left;
    QDoubleSpinBox* m_top;
    QDoubleSpinBox* m_pitchX;
    QDoubleSpinBox* m_pitchY;
    QDoubleSpinBox* m_radius;
    QDoubleSpinBox* m_gap;
    QPushButton* m_fit;
    QPushButton* m_save;
    QPushButton* m_delete;
    QLabel* m_info;
    PagePreview* m_preview;
    SheetLayout m_current;
    QString m_selectedId;
    bool m_loading = false;
};

} // namespace lf
