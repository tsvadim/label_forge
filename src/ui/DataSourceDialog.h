// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/DataSource.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QPlainTextEdit;
class QStackedWidget;
class QTableWidget;
class QLabel;

namespace lf {

class DataSourceDialog : public QDialog {
    Q_OBJECT
public:
    DataSourceDialog(const DataSourceConfig& cfg, const DataTable& current, const QString& baseDir,
                     QWidget* parent = nullptr);
    DataSourceConfig config() const;

private:
    void updatePage();
    void preview();
    void browse(QLineEdit* target, const QString& filter);
    void reloadSheets();

    DataSourceConfig m_cfg;
    DataTable m_current;
    QString m_baseDir;
    QComboBox* m_kind;
    QStackedWidget* m_pages;
    QLineEdit* m_csvPath;
    QComboBox* m_csvDelim;
    QComboBox* m_csvEnc;
    QCheckBox* m_csvHeader;
    QLineEdit* m_xlsPath;
    QComboBox* m_xlsSheet;
    QCheckBox* m_xlsHeader;
    QLineEdit* m_odbcConn;
    QPlainTextEdit* m_odbcQuery;
    QLineEdit* m_inlineCols;
    QCheckBox* m_inlineFromCurrent;
    QCheckBox* m_embed;
    QTableWidget* m_preview;
    QLabel* m_status;
    DataTable m_previewTable;
};

} // namespace lf
