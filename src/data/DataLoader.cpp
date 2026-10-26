// SPDX-License-Identifier: GPL-3.0-or-later
#include "DataLoader.h"

#include "Inflate.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStringDecoder>
#include <QUuid>
#include <QXmlStreamReader>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace lf {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("DataLoader", s); }

quint16 le16(const QByteArray& d, qsizetype p)
{
    return quint16(uchar(d.at(p)) | (uchar(d.at(p + 1)) << 8));
}
quint32 le32(const QByteArray& d, qsizetype p)
{
    return quint32(uchar(d.at(p))) | (quint32(uchar(d.at(p + 1))) << 8) |
           (quint32(uchar(d.at(p + 2))) << 16) | (quint32(uchar(d.at(p + 3))) << 24);
}

// Windows-1251 upper half (0x80..0xBF); 0xC0..0xFF map to U+0410..U+044F.
const char16_t CP1251_80[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};

QString decodeCp1251(const QByteArray& b)
{
    QString s;
    s.reserve(b.size());
    for (char ch : b) {
        const uchar c = uchar(ch);
        if (c < 0x80)
            s += QChar(c);
        else if (c < 0xC0)
            s += QChar(CP1251_80[c - 0x80]);
        else
            s += QChar(char16_t(0x0410 + (c - 0xC0)));
    }
    return s;
}

bool isValidUtf8(const QByteArray& b)
{
    QStringDecoder dec(QStringDecoder::Utf8);
    const QString s = dec.decode(b);
    Q_UNUSED(s);
    return !dec.hasError();
}

// Excel serial date -> dd.MM.yyyy
QString excelDate(double serial, bool withTime)
{
    const QDate base(1899, 12, 30);
    const qint64 days = qint64(std::floor(serial));
    const QDate d = base.addDays(days);
    if (!withTime)
        return d.toString(QStringLiteral("dd.MM.yyyy"));
    const int secs = int(std::lround((serial - days) * 86400.0));
    return d.toString(QStringLiteral("dd.MM.yyyy ")) +
           QTime(0, 0).addSecs(secs).toString(QStringLiteral("HH:mm"));
}

int columnIndex(const QString& ref)
{
    int col = 0;
    for (QChar c : ref) {
        if (!c.isLetter())
            break;
        col = col * 26 + (c.toUpper().unicode() - 'A' + 1);
    }
    return col - 1;
}

QString resolvePath(const QString& path, const QString& baseDir)
{
    if (path.isEmpty() || !QFileInfo(path).isRelative() || baseDir.isEmpty())
        return path;
    return QDir(baseDir).filePath(path);
}
} // namespace

QString columnLetters(int index)
{
    QString s;
    ++index;
    while (index > 0) {
        const int r = (index - 1) % 26;
        s.prepend(QChar('A' + r));
        index = (index - 1) / 26;
    }
    return s;
}

// ============================================================ ZIP

bool ZipReader::open(const QString& path, QString* err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *err = f.errorString();
        return false;
    }
    m_data = f.readAll();
    m_entries.clear();
    const qsizetype n = m_data.size();
    qsizetype eocd = -1;
    for (qsizetype p = n - 22; p >= 0 && p >= n - 22 - 65536; --p) {
        if (le32(m_data, p) == 0x06054b50) {
            eocd = p;
            break;
        }
    }
    if (eocd < 0) {
        *err = tr("Файл не является ZIP/XLSX архивом");
        return false;
    }
    const quint16 count = le16(m_data, eocd + 10);
    qsizetype p = le32(m_data, eocd + 16);
    for (int i = 0; i < count; ++i) {
        if (p + 46 > n || le32(m_data, p) != 0x02014b50) {
            *err = tr("Повреждённый каталог ZIP");
            return false;
        }
        Entry e;
        e.method = le16(m_data, p + 10);
        e.compSize = le32(m_data, p + 20);
        e.size = le32(m_data, p + 24);
        const quint16 nameLen = le16(m_data, p + 28);
        const quint16 extraLen = le16(m_data, p + 30);
        const quint16 commentLen = le16(m_data, p + 32);
        e.localOffset = le32(m_data, p + 42);
        const QString name = QString::fromUtf8(m_data.mid(p + 46, nameLen));
        m_entries.insert(name, e);
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

QByteArray ZipReader::read(const QString& name, QString* err) const
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd()) {
        if (err)
            *err = tr("В архиве нет файла %1").arg(name);
        return {};
    }
    const Entry& e = *it;
    const qsizetype p = e.localOffset;
    if (p + 30 > m_data.size() || le32(m_data, p) != 0x04034b50) {
        if (err)
            *err = tr("Повреждённый ZIP");
        return {};
    }
    const qsizetype start = p + 30 + le16(m_data, p + 26) + le16(m_data, p + 28);
    const QByteArray comp = m_data.mid(start, e.compSize);
    if (e.method == 0)
        return comp;
    if (e.method == 8) {
        QByteArray out;
        if (inflateRaw(comp, &out, e.size))
            return out;
        if (err)
            *err = tr("Ошибка распаковки %1").arg(name);
        return {};
    }
    if (err)
        *err = tr("Неподдерживаемый метод сжатия ZIP (%1)").arg(e.method);
    return {};
}

// ============================================================ XLSX

namespace {
struct Workbook {
    ZipReader zip;
    QStringList sheetNames;
    QStringList sheetFiles;
    QStringList shared;
    QSet<int> dateStyles;  // indices in cellXfs with a date number format
};

bool openWorkbook(const QString& path, Workbook& wb, QString* err)
{
    if (!wb.zip.open(path, err))
        return false;
    // relationships rId -> target
    QHash<QString, QString> rels;
    {
        QXmlStreamReader x(wb.zip.read(QStringLiteral("xl/_rels/workbook.xml.rels")));
        while (!x.atEnd()) {
            if (x.readNext() == QXmlStreamReader::StartElement && x.name() == QLatin1String("Relationship")) {
                QString target = x.attributes().value(QStringLiteral("Target")).toString();
                if (target.startsWith(QLatin1Char('/')))
                    target = target.mid(1);
                else
                    target = QStringLiteral("xl/") + target;
                rels.insert(x.attributes().value(QStringLiteral("Id")).toString(), target);
            }
        }
    }
    {
        const QByteArray wbXml = wb.zip.read(QStringLiteral("xl/workbook.xml"), err);
        if (wbXml.isEmpty())
            return false;
        QXmlStreamReader x(wbXml);
        while (!x.atEnd()) {
            if (x.readNext() == QXmlStreamReader::StartElement && x.name() == QLatin1String("sheet")) {
                const auto a = x.attributes();
                QString rid;
                for (const auto& attr : a)
                    if (attr.name() == QLatin1String("id"))
                        rid = attr.value().toString();
                wb.sheetNames << a.value(QStringLiteral("name")).toString();
                wb.sheetFiles << rels.value(rid);
            }
        }
    }
    if (wb.zip.contains(QStringLiteral("xl/sharedStrings.xml"))) {
        QXmlStreamReader x(wb.zip.read(QStringLiteral("xl/sharedStrings.xml")));
        QString cur;
        bool inSi = false, inT = false, inRPh = false;
        while (!x.atEnd()) {
            const auto t = x.readNext();
            if (t == QXmlStreamReader::StartElement) {
                if (x.name() == QLatin1String("si")) {
                    inSi = true;
                    cur.clear();
                } else if (x.name() == QLatin1String("rPh")) {
                    inRPh = true;  // phonetic hints: skip
                } else if (x.name() == QLatin1String("t") && inSi && !inRPh) {
                    inT = true;
                }
            } else if (t == QXmlStreamReader::EndElement) {
                if (x.name() == QLatin1String("si")) {
                    wb.shared << cur;
                    inSi = false;
                } else if (x.name() == QLatin1String("t")) {
                    inT = false;
                } else if (x.name() == QLatin1String("rPh")) {
                    inRPh = false;
                }
            } else if (t == QXmlStreamReader::Characters && inT) {
                cur += x.text();
            }
        }
    }
    if (wb.zip.contains(QStringLiteral("xl/styles.xml"))) {
        QXmlStreamReader x(wb.zip.read(QStringLiteral("xl/styles.xml")));
        QSet<int> customDate;
        bool inXfs = false;
        int xfIndex = 0;
        static const QRegularExpression dateCode(QStringLiteral("[dmyhs]"), QRegularExpression::CaseInsensitiveOption);
        while (!x.atEnd()) {
            const auto t = x.readNext();
            if (t == QXmlStreamReader::StartElement) {
                if (x.name() == QLatin1String("numFmt")) {
                    QString code = x.attributes().value(QStringLiteral("formatCode")).toString();
                    code.remove(QRegularExpression(QStringLiteral("\"[^\"]*\"|\\[[^\\]]*\\]")));
                    if (code.contains(dateCode))
                        customDate.insert(x.attributes().value(QStringLiteral("numFmtId")).toInt());
                } else if (x.name() == QLatin1String("cellXfs")) {
                    inXfs = true;
                } else if (inXfs && x.name() == QLatin1String("xf")) {
                    const int id = x.attributes().value(QStringLiteral("numFmtId")).toInt();
                    if ((id >= 14 && id <= 22) || (id >= 45 && id <= 47) || customDate.contains(id))
                        wb.dateStyles.insert(xfIndex);
                    ++xfIndex;
                }
            } else if (t == QXmlStreamReader::EndElement && x.name() == QLatin1String("cellXfs")) {
                inXfs = false;
            }
        }
    }
    return true;
}
} // namespace

QStringList xlsxSheetNames(const QString& path, QString* err)
{
    Workbook wb;
    if (!openWorkbook(path, wb, err))
        return {};
    return wb.sheetNames;
}

bool readXlsx(const QString& path, const QString& sheet, bool hasHeader, DataTable* out, QString* err)
{
    Workbook wb;
    if (!openWorkbook(path, wb, err))
        return false;
    if (wb.sheetNames.isEmpty()) {
        *err = tr("В книге нет листов");
        return false;
    }
    int idx = sheet.isEmpty() ? 0 : wb.sheetNames.indexOf(sheet);
    if (idx < 0) {
        *err = tr("Лист «%1» не найден").arg(sheet);
        return false;
    }
    const QByteArray xml = wb.zip.read(wb.sheetFiles.at(idx), err);
    if (xml.isEmpty())
        return false;

    QVector<QStringList> grid;
    QXmlStreamReader x(xml);
    int row = -1, col = -1;
    QString type, value, inlineText;
    int style = 0;
    bool inV = false, inIsT = false;
    int maxCol = -1;
    while (!x.atEnd()) {
        const auto t = x.readNext();
        if (t == QXmlStreamReader::StartElement) {
            const auto n = x.name();
            if (n == QLatin1String("row")) {
                const int r = x.attributes().value(QStringLiteral("r")).toInt();
                row = r > 0 ? r - 1 : row + 1;
                col = -1;
            } else if (n == QLatin1String("c")) {
                const QString ref = x.attributes().value(QStringLiteral("r")).toString();
                col = ref.isEmpty() ? col + 1 : columnIndex(ref);
                type = x.attributes().value(QStringLiteral("t")).toString();
                style = x.attributes().value(QStringLiteral("s")).toInt();
                value.clear();
                inlineText.clear();
            } else if (n == QLatin1String("v")) {
                inV = true;
            } else if (n == QLatin1String("t") && col >= 0) {
                inIsT = true;
            }
        } else if (t == QXmlStreamReader::Characters) {
            if (inV)
                value += x.text();
            else if (inIsT)
                inlineText += x.text();
        } else if (t == QXmlStreamReader::EndElement) {
            const auto n = x.name();
            if (n == QLatin1String("v")) {
                inV = false;
            } else if (n == QLatin1String("t")) {
                inIsT = false;
            } else if (n == QLatin1String("c") && row >= 0 && col >= 0) {
                QString cell;
                if (type == QLatin1String("s"))
                    cell = wb.shared.value(value.toInt());
                else if (type == QLatin1String("inlineStr"))
                    cell = inlineText;
                else if (type == QLatin1String("b"))
                    cell = value == QLatin1String("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
                else if (type == QLatin1String("str") || type == QLatin1String("e"))
                    cell = value;
                else if (!value.isEmpty()) {
                    bool ok = false;
                    const double d = value.toDouble(&ok);
                    if (ok && wb.dateStyles.contains(style))
                        cell = excelDate(d, d != std::floor(d));
                    else if (ok)
                        cell = QString::number(d, 'g', 15);
                    else
                        cell = value;
                }
                while (grid.size() <= row)
                    grid.push_back(QStringList());
                QStringList& r = grid[row];
                while (r.size() <= col)
                    r << QString();
                r[col] = cell;
                maxCol = std::max(maxCol, col);
            }
        }
    }
    // drop trailing empty rows
    while (!grid.isEmpty()) {
        bool empty = true;
        for (const QString& s : grid.last())
            empty = empty && s.trimmed().isEmpty();
        if (!empty)
            break;
        grid.removeLast();
    }
    out->columns.clear();
    out->rows.clear();
    const int ncol = maxCol + 1;
    int first = 0;
    if (hasHeader && !grid.isEmpty()) {
        const QStringList h = grid.first();
        for (int c = 0; c < ncol; ++c) {
            QString name = h.value(c).trimmed();
            if (name.isEmpty())
                name = columnLetters(c);
            out->columns << name;
        }
        first = 1;
    } else {
        for (int c = 0; c < ncol; ++c)
            out->columns << columnLetters(c);
    }
    for (int r = first; r < grid.size(); ++r) {
        QStringList row = grid.at(r);
        while (row.size() < ncol)
            row << QString();
        out->rows << row;
    }
    return true;
}

// ============================================================ CSV

QString decodeText(const QByteArray& bytes, const QString& encoding)
{
    QByteArray b = bytes;
    const QString enc = encoding.toLower();
    if (b.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(b.mid(3));
    if (b.startsWith("\xFF\xFE") || enc == QLatin1String("utf-16le")) {
        QStringDecoder d(QStringDecoder::Utf16LE);
        return d.decode(b.startsWith("\xFF\xFE") ? b.mid(2) : b);
    }
    if (enc == QLatin1String("utf-8"))
        return QString::fromUtf8(b);
    if (enc == QLatin1String("windows-1251") || enc == QLatin1String("cp1251"))
        return decodeCp1251(b);
    if (enc == QLatin1String("system")) {
        QStringDecoder d(QStringDecoder::System);
        return d.decode(b);
    }
    // auto
    if (isValidUtf8(b))
        return QString::fromUtf8(b);
    return decodeCp1251(b);
}

QString detectDelimiter(const QString& text)
{
    const QString firstLine = text.section(QLatin1Char('\n'), 0, 0);
    const QChar candidates[] = {QLatin1Char(';'), QLatin1Char(','), QLatin1Char('\t'), QLatin1Char('|')};
    QChar best = QLatin1Char(';');
    int bestCount = -1;
    for (QChar c : candidates) {
        int count = 0;
        bool quoted = false;
        for (QChar ch : firstLine) {
            if (ch == QLatin1Char('"'))
                quoted = !quoted;
            else if (!quoted && ch == c)
                ++count;
        }
        if (count > bestCount) {
            bestCount = count;
            best = c;
        }
    }
    return QString(best);
}

bool parseCsv(const QString& text, const QString& delimiter, bool hasHeader, DataTable* out, QString* err)
{
    Q_UNUSED(err);
    QString dl = delimiter;
    if (dl.isEmpty() || dl == QLatin1String("auto"))
        dl = detectDelimiter(text);
    else if (dl == QLatin1String("tab") || dl == QLatin1String("\\t"))
        dl = QStringLiteral("\t");
    const QChar d = dl.at(0);

    QVector<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool fieldStarted = false;
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && !fieldStarted) {
            quoted = true;
            fieldStarted = true;
        } else if (c == d) {
            row << field;
            field.clear();
            fieldStarted = false;
        } else if (c == QLatin1Char('\r')) {
            continue;
        } else if (c == QLatin1Char('\n')) {
            row << field;
            rows << row;
            row.clear();
            field.clear();
            fieldStarted = false;
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !field.isEmpty() || !row.isEmpty()) {
        row << field;
        rows << row;
    }
    // remove empty lines
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const QStringList& r) { return r.size() == 1 && r.first().trimmed().isEmpty(); }),
               rows.end());
    int ncol = 0;
    for (const QStringList& r : rows)
        ncol = std::max(ncol, int(r.size()));
    out->columns.clear();
    out->rows.clear();
    int first = 0;
    if (hasHeader && !rows.isEmpty()) {
        for (int c = 0; c < ncol; ++c) {
            QString name = rows.first().value(c).trimmed();
            out->columns << (name.isEmpty() ? columnLetters(c) : name);
        }
        first = 1;
    } else {
        for (int c = 0; c < ncol; ++c)
            out->columns << columnLetters(c);
    }
    for (int r = first; r < rows.size(); ++r) {
        QStringList rr = rows.at(r);
        while (rr.size() < ncol)
            rr << QString();
        out->rows << rr;
    }
    return true;
}

// ============================================================ dispatcher

bool loadData(const DataSourceConfig& cfg, const QString& baseDir, DataTable* out, QString* err)
{
    out->columns.clear();
    out->rows.clear();
    switch (cfg.kind) {
    case DataKind::None:
        return true;
    case DataKind::Inline:
        *out = cfg.table;
        return true;
    case DataKind::Csv: {
        QFile f(resolvePath(cfg.path, baseDir));
        if (!f.open(QIODevice::ReadOnly)) {
            *err = tr("Не удалось открыть %1: %2").arg(cfg.path, f.errorString());
            return false;
        }
        return parseCsv(decodeText(f.readAll(), cfg.encoding), cfg.delimiter, cfg.hasHeader, out, err);
    }
    case DataKind::Excel:
        return readXlsx(resolvePath(cfg.path, baseDir), cfg.sheet, cfg.hasHeader, out, err);
    case DataKind::Odbc: {
        if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QODBC"))) {
            *err = tr("Драйвер Qt ODBC (QODBC) недоступен");
            return false;
        }
        const QString connName = QStringLiteral("lf-") + QUuid::createUuid().toString();
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"), connName);
            db.setDatabaseName(cfg.connection);
            if (!db.open()) {
                *err = db.lastError().text();
            } else {
                QSqlQuery q(db);
                q.setForwardOnly(true);
                if (!q.exec(cfg.query)) {
                    *err = q.lastError().text();
                } else {
                    const QSqlRecord rec = q.record();
                    for (int i = 0; i < rec.count(); ++i)
                        out->columns << rec.fieldName(i);
                    while (q.next()) {
                        QStringList row;
                        for (int i = 0; i < rec.count(); ++i)
                            row << q.value(i).toString();
                        out->rows << row;
                    }
                    ok = true;
                }
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(connName);
        return ok;
    }
    }
    return false;
}

} // namespace lf
