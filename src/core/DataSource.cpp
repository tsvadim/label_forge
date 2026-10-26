// SPDX-License-Identifier: GPL-3.0-or-later
#include "DataSource.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>

namespace lf {

QJsonObject DataTable::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("columns")] = QJsonArray::fromStringList(columns);
    QJsonArray rs;
    for (const QStringList& r : rows)
        rs.append(QJsonArray::fromStringList(r));
    o[QStringLiteral("rows")] = rs;
    return o;
}

DataTable DataTable::fromJson(const QJsonObject& o)
{
    DataTable t;
    for (const QJsonValue& v : o.value(QStringLiteral("columns")).toArray())
        t.columns << v.toString();
    for (const QJsonValue& r : o.value(QStringLiteral("rows")).toArray()) {
        QStringList row;
        for (const QJsonValue& v : r.toArray())
            row << v.toString();
        while (row.size() < t.columns.size())
            row << QString();
        t.rows << row;
    }
    return t;
}

static const char* const KIND_NAMES[] = {"none", "inline", "csv", "excel", "odbc"};

QJsonObject DataSourceConfig::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("kind")] = QLatin1String(KIND_NAMES[int(kind)]);
    if (!path.isEmpty())
        o[QStringLiteral("path")] = path;
    if (!sheet.isEmpty())
        o[QStringLiteral("sheet")] = sheet;
    o[QStringLiteral("delimiter")] = delimiter;
    o[QStringLiteral("encoding")] = encoding;
    o[QStringLiteral("hasHeader")] = hasHeader;
    if (!connection.isEmpty())
        o[QStringLiteral("connection")] = connection;
    if (!query.isEmpty())
        o[QStringLiteral("query")] = query;
    if (!table.columns.isEmpty())
        o[QStringLiteral("table")] = table.toJson();
    return o;
}

DataSourceConfig DataSourceConfig::fromJson(const QJsonObject& o)
{
    DataSourceConfig c;
    const QString k = o.value(QStringLiteral("kind")).toString();
    for (int i = 0; i < 5; ++i)
        if (k == QLatin1String(KIND_NAMES[i]))
            c.kind = DataKind(i);
    c.path = o.value(QStringLiteral("path")).toString();
    c.sheet = o.value(QStringLiteral("sheet")).toString();
    c.delimiter = o.value(QStringLiteral("delimiter")).toString(QStringLiteral("auto"));
    c.encoding = o.value(QStringLiteral("encoding")).toString(QStringLiteral("auto"));
    c.hasHeader = o.value(QStringLiteral("hasHeader")).toBool(true);
    c.connection = o.value(QStringLiteral("connection")).toString();
    c.query = o.value(QStringLiteral("query")).toString();
    c.table = DataTable::fromJson(o.value(QStringLiteral("table")).toObject());
    return c;
}

QString DataSourceConfig::describe() const
{
    auto tr = [](const char* s) { return QCoreApplication::translate("DataSource", s); };
    switch (kind) {
    case DataKind::None: return tr("Нет источника данных");
    case DataKind::Inline: return tr("Встроенная таблица");
    case DataKind::Csv: return tr("CSV: %1").arg(QFileInfo(path).fileName());
    case DataKind::Excel:
        return tr("Excel: %1%2").arg(QFileInfo(path).fileName(),
                                     sheet.isEmpty() ? QString() : QStringLiteral(" [%1]").arg(sheet));
    case DataKind::Odbc: return tr("База данных (ODBC)");
    }
    return QString();
}

} // namespace lf
