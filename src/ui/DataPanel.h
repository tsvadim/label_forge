// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/DataSource.h"

#include <QWidget>

class QTableWidget;
class QLabel;
class QCheckBox;
class QToolButton;
class QSpinBox;

namespace lf {

// Shows the records of the data source, selects the record used for the
// on-screen preview and edits the built-in (inline) table.
class DataPanel : public QWidget {
    Q_OBJECT
public:
    explicit DataPanel(QWidget* parent = nullptr);

    void setData(const DataSourceConfig& cfg, const DataTable& table);
    int currentRecord() const { return m_current; }
    void setCurrentRecord(int row);
    bool showData() const;

signals:
    void currentRecordChanged(int row);
    void showDataToggled(bool on);
    void editSourceRequested();
    void reloadRequested();
    void inlineTableEdited(const DataTable& table);

private:
    void rebuildTable();
    void emitInlineEdit();
    DataTable tableFromWidget() const;

    DataSourceConfig m_cfg;
    DataTable m_table;
    int m_current = 0;
    bool m_loading = false;
    QLabel* m_source;
    QTableWidget* m_view;
    QCheckBox* m_showData;
    QToolButton* m_addRow;
    QToolButton* m_delRow;
    QToolButton* m_addCol;
    QToolButton* m_delCol;
    QToolButton* m_reload;
    QLabel* m_nav;
};

} // namespace lf
