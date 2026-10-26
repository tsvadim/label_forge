// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace lf {

struct DataTable {
    QStringList columns;
    QVector<QStringList> rows;

    int rowCount() const { return rows.size(); }
    bool isEmpty() const { return rows.isEmpty(); }
    QJsonObject toJson() const;
    static DataTable fromJson(const QJsonObject& o);
};

enum class DataKind { None = 0, Inline, Csv, Excel, Odbc };

struct DataSourceConfig {
    DataKind kind = DataKind::None;
    QString path;                              // CSV / Excel file
    QString sheet;                             // Excel sheet name (empty = first)
    QString delimiter = QStringLiteral("auto");  // CSV: auto , ; tab |
    QString encoding = QStringLiteral("auto");   // CSV: auto, UTF-8, Windows-1251, UTF-16LE, System
    bool hasHeader = true;
    QString connection;                        // ODBC connection string
    QString query;                             // SQL query
    DataTable table;                           // inline table, stored in the label file

    QJsonObject toJson() const;
    static DataSourceConfig fromJson(const QJsonObject& o);
    QString describe() const;
};

} // namespace lf
