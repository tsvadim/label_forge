// SPDX-License-Identifier: GPL-3.0-or-later
#include "LabelDocument.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace lf {

QJsonObject PrintSettings::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("layout")] = layoutId;
    o[QStringLiteral("startPosition")] = startPosition;
    o[QStringLiteral("copies")] = copies;
    o[QStringLiteral("quantityField")] = quantityField;
    o[QStringLiteral("recordRange")] = recordRange;
    o[QStringLiteral("printOutlines")] = printOutlines;
    o[QStringLiteral("offsetX")] = offsetX;
    o[QStringLiteral("offsetY")] = offsetY;
    o[QStringLiteral("printer")] = printerName;
    QJsonObject t;
    t[QStringLiteral("lang")] = thermalLang;
    t[QStringLiteral("dpi")] = thermalDpi;
    t[QStringLiteral("darkness")] = darkness;
    t[QStringLiteral("speed")] = speed;
    t[QStringLiteral("gap")] = gapMm;
    t[QStringLiteral("tsplInvert")] = tsplInvert;
    t[QStringLiteral("printer")] = thermalPrinter;
    o[QStringLiteral("thermal")] = t;
    return o;
}

PrintSettings PrintSettings::fromJson(const QJsonObject& o)
{
    PrintSettings p;
    p.layoutId = o.value(QStringLiteral("layout")).toString(QStringLiteral("roll"));
    p.startPosition = std::max(1, o.value(QStringLiteral("startPosition")).toInt(1));
    p.copies = std::max(1, o.value(QStringLiteral("copies")).toInt(1));
    p.quantityField = o.value(QStringLiteral("quantityField")).toString();
    p.recordRange = o.value(QStringLiteral("recordRange")).toString();
    p.printOutlines = o.value(QStringLiteral("printOutlines")).toBool();
    p.offsetX = o.value(QStringLiteral("offsetX")).toDouble();
    p.offsetY = o.value(QStringLiteral("offsetY")).toDouble();
    p.printerName = o.value(QStringLiteral("printer")).toString();
    const QJsonObject t = o.value(QStringLiteral("thermal")).toObject();
    p.thermalLang = t.value(QStringLiteral("lang")).toString(QStringLiteral("zpl"));
    p.thermalDpi = t.value(QStringLiteral("dpi")).toInt(203);
    p.darkness = t.value(QStringLiteral("darkness")).toInt(15);
    p.speed = t.value(QStringLiteral("speed")).toInt(4);
    p.gapMm = t.value(QStringLiteral("gap")).toDouble(2);
    p.tsplInvert = t.value(QStringLiteral("tsplInvert")).toBool(true);
    p.thermalPrinter = t.value(QStringLiteral("printer")).toString();
    return p;
}

QJsonObject LabelDocument::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("format")] = QStringLiteral("LabelForge");
    o[QStringLiteral("version")] = FORMAT_VERSION;
    if (!title.isEmpty())
        o[QStringLiteral("title")] = title;
    if (!category.isEmpty())
        o[QStringLiteral("category")] = category;
    if (!description.isEmpty())
        o[QStringLiteral("description")] = description;
    QJsonObject l;
    l[QStringLiteral("width")] = label.width;
    l[QStringLiteral("height")] = label.height;
    l[QStringLiteral("radius")] = label.radius;
    l[QStringLiteral("background")] = label.background.name(QColor::HexArgb);
    o[QStringLiteral("label")] = l;
    QJsonArray objs;
    for (const auto& obj : objects)
        objs.append(obj->toJson());
    o[QStringLiteral("objects")] = objs;
    o[QStringLiteral("data")] = data.toJson();
    o[QStringLiteral("print")] = print.toJson();
    return o;
}

bool LabelDocument::fromJson(const QJsonObject& o, QString* err)
{
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("LabelForge")) {
        if (err)
            *err = QCoreApplication::translate("LabelDocument", "Файл не является этикеткой LabelForge");
        return false;
    }
    clear();
    title = o.value(QStringLiteral("title")).toString();
    category = o.value(QStringLiteral("category")).toString();
    description = o.value(QStringLiteral("description")).toString();
    const QJsonObject l = o.value(QStringLiteral("label")).toObject();
    label.width = std::max(1.0, l.value(QStringLiteral("width")).toDouble(58));
    label.height = std::max(1.0, l.value(QStringLiteral("height")).toDouble(40));
    label.radius = l.value(QStringLiteral("radius")).toDouble(0);
    label.background = QColor(l.value(QStringLiteral("background")).toString(QStringLiteral("#ffffffff")));
    if (!label.background.isValid())
        label.background = Qt::white;
    for (const QJsonValue& v : o.value(QStringLiteral("objects")).toArray()) {
        auto obj = LabelObject::fromJsonAny(v.toObject());
        if (obj)
            objects.push_back(std::move(obj));
    }
    data = DataSourceConfig::fromJson(o.value(QStringLiteral("data")).toObject());
    print = PrintSettings::fromJson(o.value(QStringLiteral("print")).toObject());
    return true;
}

QByteArray LabelDocument::toBytes() const { return QJsonDocument(toJson()).toJson(QJsonDocument::Indented); }

bool LabelDocument::fromBytes(const QByteArray& bytes, QString* err)
{
    QJsonParseError pe;
    const QJsonDocument d = QJsonDocument::fromJson(bytes, &pe);
    if (!d.isObject()) {
        if (err)
            *err = pe.errorString();
        return false;
    }
    return fromJson(d.object(), err);
}

bool LabelDocument::save(const QString& path, QString* err) const
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (err)
            *err = f.errorString();
        return false;
    }
    f.write(toBytes());
    if (!f.commit()) {
        if (err)
            *err = f.errorString();
        return false;
    }
    return true;
}

bool LabelDocument::load(const QString& path, QString* err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err)
            *err = f.errorString();
        return false;
    }
    return fromBytes(f.readAll(), err);
}

void LabelDocument::copyFrom(const LabelDocument& other) { fromJson(other.toJson()); }

void LabelDocument::clear()
{
    objects.clear();
    label = LabelSetup();
    data = DataSourceConfig();
    print = PrintSettings();
    title.clear();
    category.clear();
    description.clear();
}

int LabelDocument::indexOf(const LabelObject* o) const
{
    for (size_t i = 0; i < objects.size(); ++i)
        if (objects[i].get() == o)
            return int(i);
    return -1;
}

} // namespace lf
