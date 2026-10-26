// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DataSource.h"
#include "LabelObject.h"

#include <QColor>
#include <QJsonObject>
#include <memory>
#include <vector>

namespace lf {

struct LabelSetup {
    double width = 58;
    double height = 40;
    double radius = 0;
    QColor background = Qt::white;
};

struct PrintSettings {
    QString layoutId = QStringLiteral("roll");
    int startPosition = 1;       // first free position on a partly used sheet
    int copies = 1;              // copies of every record
    QString quantityField;       // optional column with per-record quantity
    QString recordRange;         // "1-5, 8, 10-12"; empty = all
    bool printOutlines = false;  // draw label outlines (test prints)
    double offsetX = 0, offsetY = 0;  // printer calibration, mm
    QString printerName;

    // thermal printers (raw)
    QString thermalLang = QStringLiteral("zpl");  // zpl | tspl
    int thermalDpi = 203;
    int darkness = 15;
    int speed = 4;
    double gapMm = 2;
    bool tsplInvert = true;
    QString thermalPrinter;

    QJsonObject toJson() const;
    static PrintSettings fromJson(const QJsonObject& o);
};

class LabelDocument {
public:
    LabelDocument() = default;
    LabelDocument(const LabelDocument&) = delete;
    LabelDocument& operator=(const LabelDocument&) = delete;

    LabelSetup label;
    std::vector<std::unique_ptr<LabelObject>> objects;
    DataSourceConfig data;
    PrintSettings print;

    // template metadata
    QString title;
    QString category;
    QString description;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& o, QString* err = nullptr);
    QByteArray toBytes() const;
    bool fromBytes(const QByteArray& data, QString* err = nullptr);
    bool save(const QString& path, QString* err = nullptr) const;
    bool load(const QString& path, QString* err = nullptr);
    void copyFrom(const LabelDocument& other);
    void clear();

    int indexOf(const LabelObject* o) const;
    static constexpr int FORMAT_VERSION = 1;
    static QString fileExtension() { return QStringLiteral("lfl"); }
};

} // namespace lf
