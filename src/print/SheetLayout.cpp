// SPDX-License-Identifier: GPL-3.0-or-later
#include "SheetLayout.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace lf {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("SheetLayout", s); }

SheetLayout sheet(const char* id, const QString& name, int cols, int rows, double lw, double lh,
                  double ml, double mt, double px, double py, double radius = 0,
                  double pw = 210, double ph = 297)
{
    SheetLayout l;
    l.id = QLatin1String(id);
    l.name = name;
    l.category = pw == 210 && ph == 297 ? QStringLiteral("A4") : QStringLiteral("Letter");
    l.builtin = true;
    l.pageW = pw;
    l.pageH = ph;
    l.cols = cols;
    l.rows = rows;
    l.labelW = lw;
    l.labelH = lh;
    l.marginLeft = ml;
    l.marginTop = mt;
    l.pitchX = px;
    l.pitchY = py;
    l.radius = radius;
    return l;
}

SheetLayout roll(const char* id, double w, double h)
{
    SheetLayout l = SheetLayout::rollFor(w, h);
    l.id = QLatin1String(id);
    l.builtin = true;
    l.name = tr("Рулон %1×%2 мм").arg(w).arg(h);
    return l;
}
} // namespace

void SheetLayout::fitGrid(SheetLayout& l, double margin, double gap)
{
    l.pitchX = l.labelW + gap;
    l.pitchY = l.labelH + gap;
    l.cols = l.rows = 1;
    if (l.labelW > 0 && l.labelH > 0) {
        l.cols = std::max(1, int((l.pageW - 2 * margin + gap + 1e-6) / l.pitchX));
        l.rows = std::max(1, int((l.pageH - 2 * margin + gap + 1e-6) / l.pitchY));
    }
    l.marginLeft = std::max(0.0, (l.pageW - (l.cols * l.pitchX - gap)) / 2);
    l.marginTop = std::max(0.0, (l.pageH - (l.rows * l.pitchY - gap)) / 2);
}

// Page filled with as many labels of the current label size as fit: an A4 sheet
// to cut out, or one big roll label (e.g. 100x70) carrying several small stickers.
static SheetLayout autoLayout(const QString& id, const QString& name, const QString& category, double pw, double ph,
                              double margin, double gap)
{
    SheetLayout l;
    l.id = id;
    l.name = name;
    l.category = category;
    l.builtin = true;
    l.autoFit = true;
    l.pageW = pw;
    l.pageH = ph;
    l.autoMargin = margin;
    l.autoGap = gap;
    l.labelW = l.labelH = 0;
    return l;
}

QRectF SheetLayout::labelRect(int i) const
{
    const int c = i % std::max(1, cols);
    const int r = i / std::max(1, cols);
    const double px = pitchX > 0 ? pitchX : labelW;
    const double py = pitchY > 0 ? pitchY : labelH;
    return QRectF(marginLeft + c * px, marginTop + r * py, labelW, labelH);
}

QString SheetLayout::summary() const
{
    if (roll)
        return tr("Рулон, этикетка %1×%2 мм").arg(labelW).arg(labelH);
    return tr("%1×%2 мм, %3 на листе (%4×%5), лист %6×%7 мм")
        .arg(labelW)
        .arg(labelH)
        .arg(perPage())
        .arg(cols)
        .arg(rows)
        .arg(pageW)
        .arg(pageH);
}

QJsonObject SheetLayout::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("category")] = category;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("roll")] = roll;
    o[QStringLiteral("pageW")] = pageW;
    o[QStringLiteral("pageH")] = pageH;
    o[QStringLiteral("cols")] = cols;
    o[QStringLiteral("rows")] = rows;
    o[QStringLiteral("labelW")] = labelW;
    o[QStringLiteral("labelH")] = labelH;
    o[QStringLiteral("marginLeft")] = marginLeft;
    o[QStringLiteral("marginTop")] = marginTop;
    o[QStringLiteral("pitchX")] = pitchX;
    o[QStringLiteral("pitchY")] = pitchY;
    o[QStringLiteral("radius")] = radius;
    return o;
}

SheetLayout SheetLayout::fromJson(const QJsonObject& o)
{
    SheetLayout l;
    l.id = o.value(QStringLiteral("id")).toString();
    l.name = o.value(QStringLiteral("name")).toString();
    l.category = o.value(QStringLiteral("category")).toString();
    l.description = o.value(QStringLiteral("description")).toString();
    l.roll = o.value(QStringLiteral("roll")).toBool();
    l.pageW = o.value(QStringLiteral("pageW")).toDouble(210);
    l.pageH = o.value(QStringLiteral("pageH")).toDouble(297);
    l.cols = std::max(1, o.value(QStringLiteral("cols")).toInt(1));
    l.rows = std::max(1, o.value(QStringLiteral("rows")).toInt(1));
    l.labelW = o.value(QStringLiteral("labelW")).toDouble(58);
    l.labelH = o.value(QStringLiteral("labelH")).toDouble(40);
    l.marginLeft = o.value(QStringLiteral("marginLeft")).toDouble();
    l.marginTop = o.value(QStringLiteral("marginTop")).toDouble();
    l.pitchX = o.value(QStringLiteral("pitchX")).toDouble();
    l.pitchY = o.value(QStringLiteral("pitchY")).toDouble();
    l.radius = o.value(QStringLiteral("radius")).toDouble();
    return l;
}

SheetLayout SheetLayout::rollFor(double w, double h)
{
    SheetLayout l;
    l.id = QStringLiteral("roll");
    l.name = tr("Рулон / термопринтер (по размеру этикетки)");
    l.category = tr("Рулон");
    l.roll = true;
    l.pageW = l.labelW = w;
    l.pageH = l.labelH = h;
    l.pitchX = w;
    l.pitchY = h;
    l.builtin = true;
    return l;
}

QVector<SheetLayout> SheetLayoutLibrary::builtins()
{
    // Avery/Zweckform-compatible A4 sheets; values follow the manufacturers' specs.
    QVector<SheetLayout> v;
    SheetLayout autoRoll = SheetLayout::rollFor(0, 0);
    v << autoRoll;
    v << autoLayout(QStringLiteral("a4-auto"), tr("A4 — сколько поместится (по размеру этикетки)"), QStringLiteral("A4"), 210, 297, 8, 2);
    {
        const double rolls[][2] = {{100, 70}, {100, 100}, {100, 150}, {75, 120}, {58, 40}, {58, 60}};
        for (const auto& r : rolls) {
            v << autoLayout(QStringLiteral("roll-%1x%2-auto").arg(r[0]).arg(r[1]),
                            tr("Рулон %1×%2 — несколько этикеток на одной (сколько поместится)").arg(r[0]).arg(r[1]),
                            tr("Рулон — несколько на одной"), r[0], r[1], 1.5, 1.5);
        }
    }
    v << roll("roll-58x40", 58, 40) << roll("roll-58x30", 58, 30) << roll("roll-40x30", 40, 30)
      << roll("roll-30x20", 30, 20) << roll("roll-43x25", 43, 25) << roll("roll-75x120", 75, 120)
      << roll("roll-100x150", 100, 150) << roll("roll-100x100", 100, 100)
      << roll("roll-100x70", 100, 70);
    v << sheet("avery-l7160", tr("Avery L7160 — 21 шт. 63,5×38,1"), 3, 7, 63.5, 38.1, 7.21, 15.15, 66.04, 38.1, 2);
    v << sheet("avery-l7159", tr("Avery L7159 — 24 шт. 63,5×33,9"), 3, 8, 63.5, 33.9, 6.5, 12.9, 66.0, 33.9, 2);
    v << sheet("avery-l7161", tr("Avery L7161 — 18 шт. 63,5×46,6"), 3, 6, 63.5, 46.6, 7.21, 8.8, 66.04, 46.6, 2);
    v << sheet("avery-l7162", tr("Avery L7162 — 16 шт. 99,1×33,9"), 2, 8, 99.1, 33.9, 4.65, 12.9, 101.6, 33.9, 2);
    v << sheet("avery-l7163", tr("Avery L7163 — 14 шт. 99,1×38,1"), 2, 7, 99.1, 38.1, 4.65, 15.15, 101.6, 38.1, 2);
    v << sheet("avery-l7165", tr("Avery L7165 — 8 шт. 99,1×67,7"), 2, 4, 99.1, 67.7, 4.65, 13.1, 101.6, 67.7, 2);
    v << sheet("avery-l7169", tr("Avery L7169 — 4 шт. 99,1×139"), 2, 2, 99.1, 139, 4.65, 9.5, 101.6, 139, 2);
    v << sheet("avery-l7173", tr("Avery L7173 — 10 шт. 99,1×57"), 2, 5, 99.1, 57, 4.65, 6, 101.6, 57, 2);
    v << sheet("avery-l7651", tr("Avery L7651 — 65 шт. 38,1×21,2"), 5, 13, 38.1, 21.2, 4.75, 10.7, 40.6, 21.2, 1);
    v << sheet("avery-l7168", tr("Avery L7168 — 2 шт. 199,6×143,5"), 1, 2, 199.6, 143.5, 5.2, 5, 199.6, 143.5, 2);
    v << sheet("avery-l7167", tr("Avery L7167 — 1 шт. 199,6×289,1"), 1, 1, 199.6, 289.1, 5.2, 4, 199.6, 289.1, 2);
    v << sheet("a4-3x8-70x37", tr("A4 — 24 шт. 70×37 (без полей)"), 3, 8, 70, 37, 0, 0.5, 70, 37);
    v << sheet("a4-3x7-70x42", tr("A4 — 21 шт. 70×42,3 (без полей)"), 3, 7, 70, 42.3, 0, 0.45, 70, 42.3);
    v << sheet("a4-3x11-70x25", tr("A4 — 33 шт. 70×25,4"), 3, 11, 70, 25.4, 0, 8.8, 70, 25.4);
    v << sheet("a4-4x10-48x25", tr("A4 — 40 шт. 48,5×25,4"), 4, 10, 48.5, 25.4, 8, 21.5, 48.5, 25.4);
    v << sheet("a4-2x8-105x37", tr("A4 — 16 шт. 105×37"), 2, 8, 105, 37, 0, 0.5, 105, 37);
    v << sheet("a4-2x4-105x74", tr("A4 — 8 шт. 105×74"), 2, 4, 105, 74, 0, 0.5, 105, 74);
    v << sheet("a4-2x2-105x148", tr("A4 — 4 шт. 105×148"), 2, 2, 105, 148, 0, 0.5, 105, 148);
    v << sheet("a4-1x1", tr("A4 — целый лист 210×297"), 1, 1, 210, 297, 0, 0, 210, 297);
    v << sheet("letter-5160", tr("Avery 5160 (Letter) — 30 шт. 66,7×25,4"), 3, 10, 66.675, 25.4, 4.76, 12.7,
               69.85, 25.4, 2, 215.9, 279.4);
    v << sheet("letter-5163", tr("Avery 5163 (Letter) — 10 шт. 101,6×50,8"), 2, 5, 101.6, 50.8, 3.97, 12.7,
               104.8, 50.8, 2, 215.9, 279.4);
    for (SheetLayout& l : v)
        if (l.roll && l.category.isEmpty())
            l.category = tr("Рулон");
    return v;
}

QString SheetLayoutLibrary::userDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                      QStringLiteral("/sheets");
    QDir().mkpath(d);
    return d;
}

QVector<SheetLayout> SheetLayoutLibrary::userLayouts()
{
    QVector<SheetLayout> v;
    QDir dir(userDir());
    for (const QString& f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            continue;
        SheetLayout l = SheetLayout::fromJson(doc.object());
        l.builtin = false;
        if (l.id.isEmpty())
            l.id = QFileInfo(f).completeBaseName();
        if (l.category.isEmpty())
            l.category = tr("Мои шаблоны");
        v << l;
    }
    return v;
}

QVector<SheetLayout> SheetLayoutLibrary::all() { return builtins() + userLayouts(); }

bool SheetLayoutLibrary::saveUser(SheetLayout& l, QString* err)
{
    if (l.id.isEmpty() || l.builtin) {
        l.id = QStringLiteral("user-") + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        l.builtin = false;
    }
    QFile f(QDir(userDir()).filePath(l.id + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err)
            *err = f.errorString();
        return false;
    }
    f.write(QJsonDocument(l.toJson()).toJson());
    return true;
}

bool SheetLayoutLibrary::removeUser(const QString& id)
{
    return QFile::remove(QDir(userDir()).filePath(id + QStringLiteral(".json")));
}

SheetLayout SheetLayoutLibrary::find(const QString& id, double labelW, double labelH, bool* found)
{
    if (found)
        *found = true;
    if (id.isEmpty() || id == QLatin1String("roll"))
        return SheetLayout::rollFor(labelW, labelH);
    for (const SheetLayout& l : all())
        if (l.id == id) {
            if (!l.autoFit)
                return l;
            SheetLayout a = l;
            a.labelW = labelW;
            a.labelH = labelH;
            SheetLayout::fitGrid(a, a.autoMargin, a.autoGap);
            return a;
        }
    if (found)
        *found = false;
    return SheetLayout::rollFor(labelW, labelH);
}

} // namespace lf
