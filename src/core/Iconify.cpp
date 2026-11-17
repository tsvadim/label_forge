// SPDX-License-Identifier: GPL-3.0-or-later
#include "Iconify.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

namespace lf::iconify {

namespace {
constexpr int LETTER_SPLIT_MIN = 300;  // smaller sets stay in one folder
constexpr int FOLDER_MAX = 500;        // bigger folders are split into parts

Collection collectionFrom(const QString& prefix, const QJsonObject& o)
{
    Collection c;
    c.prefix = prefix;
    c.name = o.value(QLatin1String("name")).toString(prefix);
    c.total = o.value(QLatin1String("total")).toInt();
    const QJsonObject a = o.value(QLatin1String("author")).toObject();
    c.author = a.value(QLatin1String("name")).toString();
    c.authorUrl = a.value(QLatin1String("url")).toString();
    const QJsonObject l = o.value(QLatin1String("license")).toObject();
    c.license = l.value(QLatin1String("title")).toString();
    c.licenseSpdx = l.value(QLatin1String("spdx")).toString();
    c.licenseUrl = l.value(QLatin1String("url")).toString();
    c.category = o.value(QLatin1String("category")).toString();
    c.palette = o.value(QLatin1String("palette")).toBool();
    return c;
}

QString num(double v) { return QString::number(v, 'g', 10); }
} // namespace

bool isFreeLicense(const QString& spdx)
{
    static const QSet<QString> ok = {
        QStringLiteral("MIT"),          QStringLiteral("Apache-2.0"),   QStringLiteral("ISC"),
        QStringLiteral("BSD-2-Clause"), QStringLiteral("BSD-3-Clause"), QStringLiteral("CC0-1.0"),
        QStringLiteral("OFL-1.1"),      QStringLiteral("Unlicense"),
    };
    return ok.contains(spdx);
}

QList<Collection> parseCollections(const QByteArray& json)
{
    QList<Collection> out;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        if (o.isEmpty() || o.value(QLatin1String("hidden")).toBool())
            continue;
        out << collectionFrom(it.key(), o);
    }
    std::sort(out.begin(), out.end(), [](const Collection& a, const Collection& b) {
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });
    return out;
}

SearchResult parseSearch(const QByteArray& json)
{
    SearchResult r;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    r.total = root.value(QLatin1String("total")).toInt();
    const QJsonObject cols = root.value(QLatin1String("collections")).toObject();
    for (auto it = cols.begin(); it != cols.end(); ++it)
        r.collections.insert(it.key(), collectionFrom(it.key(), it.value().toObject()));
    for (const QJsonValue& v : root.value(QLatin1String("icons")).toArray()) {
        const QString id = v.toString();
        const QString prefix = id.section(QLatin1Char(':'), 0, 0);
        if (id.contains(QLatin1Char(':')) && isFreeLicense(r.collections.value(prefix).licenseSpdx))
            r.icons << id;
    }
    return r;
}

QMap<QString, QStringList> parseCollectionFolders(const QByteArray& json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    QSet<QString> hidden;
    for (const QJsonValue& v : root.value(QLatin1String("hidden")).toArray())
        hidden.insert(v.toString());

    // folder -> names, before the style filter
    QMap<QString, QStringList> folders;
    const QJsonObject cats = root.value(QLatin1String("categories")).toObject();
    for (auto it = cats.begin(); it != cats.end(); ++it)
        for (const QJsonValue& v : it.value().toArray())
            folders[safeFileName(it.key())] << v.toString();
    for (const QJsonValue& v : root.value(QLatin1String("uncategorized")).toArray())
        folders[cats.isEmpty() ? QString() : QStringLiteral("Другое")] << v.toString();

    // style variants: keep the plain style, or the most common one if there is no plain style
    QStringList suffixes;
    const QJsonObject sfx = root.value(QLatin1String("suffixes")).toObject();
    for (auto it = sfx.begin(); it != sfx.end(); ++it)
        if (!it.key().isEmpty())
            suffixes << it.key();
    std::sort(suffixes.begin(), suffixes.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    auto styleOf = [&suffixes](const QString& name) {
        for (const QString& s : suffixes)
            if (name.endsWith(QLatin1Char('-') + s))
                return s;
        return QString();
    };
    QString keepStyle;
    if (!suffixes.isEmpty()) {
        QHash<QString, int> count;
        for (const QStringList& names : folders)
            for (const QString& n : names)
                ++count[styleOf(n)];
        if (!count.value(QString())) {
            int best = -1;
            for (auto it = count.cbegin(); it != count.cend(); ++it)
                if (it.value() > best || (it.value() == best && it.key() < keepStyle)) {
                    best = it.value();
                    keepStyle = it.key();
                }
        }
    }

    QMap<QString, QStringList> out;
    QSet<QString> seen;
    int total = 0;
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        for (const QString& n : it.value()) {
            if (n.isEmpty() || hidden.contains(n) || seen.contains(n))
                continue;
            if (!suffixes.isEmpty() && styleOf(n) != keepStyle)
                continue;
            seen.insert(n);
            out[it.key()] << n;
            ++total;
        }
    }
    if (cats.isEmpty() && total >= LETTER_SPLIT_MIN) {
        // no categories: split a big set by first letter
        const QStringList names = out.value(QString());
        out.clear();
        for (const QString& n : names) {
            const QChar c = n.at(0).toUpper();
            const QString folder = c.isDigit() ? QStringLiteral("0-9")
                                   : (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ? QString(c)
                                                                                       : QStringLiteral("#");
            out[folder] << n;
        }
    }
    for (QStringList& names : out)
        names.sort();

    // no folder above FOLDER_MAX: split into equal parts named by their range, "A (a-b–al)"
    QMap<QString, QStringList> split;
    for (auto it = out.cbegin(); it != out.cend(); ++it) {
        const QStringList& names = it.value();
        if (names.size() <= FOLDER_MAX) {
            split.insert(it.key(), names);
            continue;
        }
        const int parts = int((names.size() + FOLDER_MAX - 1) / FOLDER_MAX);
        const int per = int((names.size() + parts - 1) / parts);
        for (int i = 0; i < names.size(); i += per) {
            const QStringList chunk = names.mid(i, per);
            // enough letters to tell the first and the last name apart
            const QString& a = chunk.first();
            const QString& b = chunk.last();
            int common = 0;
            while (common < a.size() && common < b.size() && a.at(common) == b.at(common))
                ++common;
            const int n = std::clamp(common + 1, 2, 16);
            const QString range = a.left(n) + QChar(0x2013) + b.left(n);
            QString key = safeFileName(it.key().isEmpty() ? range : QStringLiteral("%1 (%2)").arg(it.key(), range));
            for (int k = 2; split.contains(key); ++k)
                key = safeFileName(QStringLiteral("%1 (%2) %3").arg(it.key(), range).arg(k));
            split.insert(key, chunk);
        }
    }
    return split;
}

QByteArray makeSvg(const QString& body, double left, double top, double width, double height, int rotate, bool hFlip,
                   bool vFlip)
{
    QString b = body;
    b.replace(QLatin1String("currentColor"), QLatin1String("#000000"));
    QStringList tr;
    if (hFlip)
        tr << QStringLiteral("translate(%1 0) scale(-1 1)").arg(num(2 * left + width));
    if (vFlip)
        tr << QStringLiteral("translate(0 %1) scale(1 -1)").arg(num(2 * top + height));
    rotate = ((rotate % 4) + 4) % 4;
    double vl = left, vt = top, vw = width, vh = height;
    if (rotate) {
        const double cx = left + width / 2, cy = top + height / 2;
        tr.prepend(QStringLiteral("rotate(%1 %2 %3)").arg(rotate * 90).arg(num(cx), num(cy)));
        if (rotate % 2) {
            vw = height;
            vh = width;
            vl = cx - vw / 2;
            vt = cy - vh / 2;
        }
    }
    if (!tr.isEmpty())
        b = QStringLiteral("<g transform=\"%1\">%2</g>").arg(tr.join(QLatin1Char(' ')), b);
    const QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                                       "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                                       "width=\"%1\" height=\"%2\" viewBox=\"%3 %4 %1 %2\">%5</svg>\n")
                            .arg(num(vw), num(vh), num(vl), num(vt), b);
    return svg.toUtf8();
}

QHash<QString, QByteArray> parseIconSet(const QByteArray& json)
{
    QHash<QString, QByteArray> out;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const double dl = root.value(QLatin1String("left")).toDouble(0);
    const double dt = root.value(QLatin1String("top")).toDouble(0);
    const double dw = root.value(QLatin1String("width")).toDouble(16);
    const double dh = root.value(QLatin1String("height")).toDouble(16);
    const QJsonObject icons = root.value(QLatin1String("icons")).toObject();
    const QJsonObject aliases = root.value(QLatin1String("aliases")).toObject();

    auto add = [&](const QString& name, const QJsonObject& icon, int rotate, bool hFlip, bool vFlip) {
        const QString body = icon.value(QLatin1String("body")).toString();
        if (body.isEmpty())
            return;
        rotate += icon.value(QLatin1String("rotate")).toInt();
        hFlip ^= icon.value(QLatin1String("hFlip")).toBool();
        vFlip ^= icon.value(QLatin1String("vFlip")).toBool();
        out.insert(name, makeSvg(body, icon.value(QLatin1String("left")).toDouble(dl),
                                 icon.value(QLatin1String("top")).toDouble(dt),
                                 icon.value(QLatin1String("width")).toDouble(dw),
                                 icon.value(QLatin1String("height")).toDouble(dh), rotate, hFlip, vFlip));
    };
    for (auto it = icons.begin(); it != icons.end(); ++it)
        add(it.key(), it.value().toObject(), 0, false, false);
    for (auto it = aliases.begin(); it != aliases.end(); ++it) {
        if (out.contains(it.key()))
            continue;
        int rotate = 0;
        bool hFlip = false, vFlip = false;
        QJsonObject a = it.value().toObject();
        for (int depth = 0; depth < 8; ++depth) {
            rotate += a.value(QLatin1String("rotate")).toInt();
            hFlip ^= a.value(QLatin1String("hFlip")).toBool();
            vFlip ^= a.value(QLatin1String("vFlip")).toBool();
            const QString parent = a.value(QLatin1String("parent")).toString();
            if (icons.contains(parent)) {
                add(it.key(), icons.value(parent).toObject(), rotate, hFlip, vFlip);
                break;
            }
            if (!aliases.contains(parent))
                break;
            a = aliases.value(parent).toObject();
        }
    }
    return out;
}

QString licenseNote(const Collection& c)
{
    QString s;
    s += c.name + QLatin1Char('\n');
    s += QStringLiteral("Iconify: %1 (https://icon-sets.iconify.design/%1/)\n").arg(c.prefix);
    if (!c.author.isEmpty())
        s += QStringLiteral("Author: %1%2\n").arg(c.author, c.authorUrl.isEmpty() ? QString() : QStringLiteral(" — ") + c.authorUrl);
    s += QStringLiteral("License: %1%2\n")
             .arg(c.license.isEmpty() ? c.licenseSpdx : c.license,
                  c.licenseUrl.isEmpty() ? QString() : QStringLiteral(" — ") + c.licenseUrl);
    return s;
}

QString safeFileName(const QString& s)
{
    QString r;
    r.reserve(s.size());
    for (const QChar c : s)
        r += (c.unicode() < 32 || QStringLiteral("<>:\"/\\|?*").contains(c)) ? QLatin1Char('_') : c;
    r = r.trimmed();
    while (r.endsWith(QLatin1Char('.')))
        r.chop(1);
    if (r.isEmpty())
        r = QStringLiteral("_");
    static const QStringList reserved = {QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
                                         QStringLiteral("NUL")};
    const QString base = r.section(QLatin1Char('.'), 0, 0).toUpper();
    if (reserved.contains(base) ||
        ((base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))) && base.size() == 4 &&
         base.at(3).isDigit()))
        r += QLatin1Char('_');
    return r;
}

} // namespace lf::iconify
