// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/DataSource.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace lf {

// Minimal read-only ZIP reader (stored + deflate), enough for .xlsx files.
class ZipReader {
public:
    bool open(const QString& path, QString* err);
    bool contains(const QString& name) const { return m_entries.contains(name); }
    QByteArray read(const QString& name, QString* err = nullptr) const;
    QStringList names() const { return m_entries.keys(); }

private:
    struct Entry {
        quint16 method;
        quint32 compSize, size, localOffset;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
};

// Reads the sheet names of an .xlsx workbook.
QStringList xlsxSheetNames(const QString& path, QString* err);
// Reads a worksheet into a table (sheet empty = first sheet).
bool readXlsx(const QString& path, const QString& sheet, bool hasHeader, DataTable* out, QString* err);

// Decodes CSV bytes, encoding: auto|UTF-8|Windows-1251|UTF-16LE|System.
QString decodeText(const QByteArray& bytes, const QString& encoding);
bool parseCsv(const QString& text, const QString& delimiter, bool hasHeader, DataTable* out, QString* err);
QString detectDelimiter(const QString& text);

// Loads any configured data source. baseDir resolves relative file paths.
bool loadData(const DataSourceConfig& cfg, const QString& baseDir, DataTable* out, QString* err);

QString columnLetters(int index);  // 0 -> A, 26 -> AA

} // namespace lf
